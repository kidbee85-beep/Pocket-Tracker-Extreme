package com.conanizer.pockettracker

import android.os.SystemClock
import androidx.annotation.Keep
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.io.Closeable
import java.io.EOFException
import java.io.File
import java.io.IOException
import java.io.InputStream
import java.net.HttpURLConnection
import java.net.URI
import java.util.Locale
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicReference

@Keep
internal fun interface AndroidRadioPcm {
    fun pcm(samples: ShortArray, frames: Int, rate: Int, channels: Int)
}

@Keep
internal object AndroidRadioNative {
    external fun catalog(): Array<String>
    external fun create(): Long
    external fun feed(handle: Long, input: ByteArray, count: Int, output: ShortArray, target: AndroidRadioPcm)
    external fun destroy(handle: Long)
}

internal data class RadioStation(
    val id: String, val title: String, val creator: String, val tags: List<String>,
    val page: String, val url: String, val playlist: Boolean
) {
    fun item() = SourceItem(id, title, creator, "LIVE MP3", page, tags = tags)
    fun track() = SourceTrack(id, title, creator, url, page, tags = tags, live = true)
}

internal object AndroidRadioCatalog {
    val stations: List<RadioStation> by lazy {
        AndroidRadioNative.catalog().asList().chunked(7).map { row ->
            RadioStation(row[0], row[1], row[2], row[3].split(',').map(String::trim),
                row[4], row[5], row[6] == "1")
        }
    }

    fun search(query: String, genre: String, random: Boolean): List<SourceItem> {
        val matches = stations.filter { station ->
            (genre.isBlank() || station.tags.any { it.equals(genre, ignoreCase = true) }) &&
                (query.isBlank() || listOf(station.title, station.creator, station.tags.joinToString(" "))
                    .any { it.contains(query, ignoreCase = true) })
        }
        return (if (random) matches.shuffled() else matches).map(RadioStation::item)
    }
}

internal object AndroidRadioNetwork {
    private val playlistEntry = Regex("""(?im)^File[1-9][0-9]?\s*=\s*([^\r\n]+)""")
    private val audioTypes = setOf("audio/mpeg", "audio/mp3", "audio/x-mpeg", "audio/x-mp3",
        "audio/mpeg3", "application/octet-stream")
    private val swissNodes = Regex("""livestreaming-node-[0-9]+\.srg-ssr\.ch""")
    private val ntsNodes = Regex("""audio-edge-[a-z0-9-]+\.[a-z0-9-]+\.h\.radiomast\.io""")

    private fun host(uri: URI): String = uri.host.orEmpty().lowercase(Locale.ROOT)

    fun permits(uri: URI, station: RadioStation): Boolean {
        val target = host(uri)
        if (uri.scheme != "https" || uri.userInfo != null || target.isEmpty() ||
            (uri.port != -1 && uri.port != 443)) return false
        val initial = host(URI(station.url))
        if (target == initial) return true
        return when {
            station.playlist && initial == "somafm.com" -> target.endsWith(".somafm.com")
            initial == "stream.srg-ssr.ch" && station.id in
                setOf("direct:swissjazz", "direct:swissclassic", "direct:swisspop") -> swissNodes.matches(target)
            initial == "stream-relay-geo.ntslive.net" && station.id in
                setOf("direct:nts1", "direct:nts2") -> target == "streams.radiomast.io" || ntsNodes.matches(target)
            else -> false
        }
    }

    private fun connection(start: String, station: RadioStation, check: () -> Unit): HttpURLConnection {
        var next = URI(start)
        repeat(5) {
            check()
            require(permits(next, station)) {
                val targetPort = if (next.port == -1) "" else ":${next.port}"
                "Unsupported station redirect (${next.scheme}://${host(next)}$targetPort)"
            }
            val opened = next.toURL().openConnection() as HttpURLConnection
            try {
                opened.instanceFollowRedirects = false
                opened.connectTimeout = 7000
                opened.readTimeout = 4000
                opened.setRequestProperty("User-Agent", SourceNet.USER_AGENT)
                opened.setRequestProperty("Accept-Encoding", "identity")
                opened.setRequestProperty("Icy-MetaData", "0")
                when (val status = opened.responseCode) {
                    200 -> {
                        check()
                        return opened
                    }
                    301, 302, 303, 307, 308 -> {
                        val location = opened.getHeaderField("Location") ?: throw IOException("Missing station redirect")
                        next = next.resolve(location)
                    }
                    else -> throw IOException("Station returned HTTP $status")
                }
            } catch (error: Exception) {
                opened.disconnect()
                throw error
            }
            opened.disconnect()
        }
        throw IOException("Too many station redirects")
    }

    private fun playlist(station: RadioStation, check: () -> Unit): List<String> {
        val source = connection(station.url, station, check)
        try {
            val output = ByteArrayOutputStream()
            source.inputStream.use { input ->
                val block = ByteArray(2048)
                while (true) {
                    check()
                    val count = input.read(block)
                    if (count < 0) break
                    if (output.size() + count > 16384) throw IOException("Station playlist is too large")
                    output.write(block, 0, count)
                }
            }
            return playlistEntry.findAll(output.toString("UTF-8")).map { it.groupValues[1].trim() }.filter { url ->
                val uri = runCatching { URI(url) }.getOrNull() ?: return@filter false
                uri.scheme == "https" && host(uri).endsWith(".somafm.com") && uri.path.orEmpty().contains("-mp3")
            }.distinct().take(3).toList().also {
                if (it.isEmpty()) throw IOException("Station playlist has no HTTPS MP3 stream")
            }
        } finally {
            source.disconnect()
        }
    }

    fun urls(station: RadioStation, check: () -> Unit): List<String> =
        if (station.playlist) playlist(station, check) else listOf(station.url)

    fun receive(url: String, station: RadioStation, check: () -> Unit, decode: (InputStream) -> Unit) {
        val source = connection(url, station, check)
        try {
            val type = source.contentType.orEmpty().substringBefore(';').trim().lowercase(Locale.ROOT)
            require(type.isBlank() || type in audioTypes) { "This station does not provide a direct MP3 stream" }
            val interval = source.getHeaderField("icy-metaint")?.toIntOrNull()?.takeIf { it in 1..1048576 }
            source.inputStream.use { decode(if (interval == null) it else IcyAudioStream(it, interval)) }
        } finally {
            source.disconnect()
        }
    }
}

private class IcyAudioStream(private val input: InputStream, private val interval: Int) : InputStream() {
    private var remaining = interval
    private val metadata = ByteArray(255 * 16)
    override fun read(): Int {
        val one = ByteArray(1)
        return if (read(one, 0, 1) < 0) -1 else one[0].toInt() and 255
    }
    override fun read(output: ByteArray, offset: Int, size: Int): Int {
        if (size == 0) return 0
        if (remaining == 0) {
            val length = input.read()
            if (length < 0) return -1
            var at = 0
            while (at < length * 16) {
                val count = input.read(metadata, at, length * 16 - at)
                if (count <= 0) throw EOFException("Station metadata ended early")
                at += count
            }
            remaining = interval
        }
        val count = input.read(output, offset, minOf(remaining, size))
        if (count > 0) remaining -= count
        return count
    }
}

internal val liveRadioTakeLengths = setOf(10, 20, 30, 40, 45, 60)

internal data class RadioTake(val clip: RenderedSourceClip, val requestId: Long)
internal data class RadioEvents(
    val ready: () -> Unit,
    val progress: (Long, Float) -> Unit,
    val complete: (RadioTake?, String) -> Unit,
    val ended: (String) -> Unit
)

/** The stream and timed take use the decoder worker. Preview has its own cursor over the PCM history. */
internal class AndroidLiveRadio(
    private val station: RadioStation,
    private val directory: File,
    private val events: RadioEvents,
    val historySeconds: Int
) {
    private data class Request(val seconds: Int, val id: Long, @Volatile var finish: Boolean = false)
    private val stopped = AtomicBoolean(false)
    private val request = AtomicReference<Request?>()
    private val audioError = AtomicReference<Exception?>()
    @Volatile private var playback: AndroidRadioPlayback? = null
    @Volatile private var captureProgress = 0f
    @Volatile private var history: AndroidRadioHistory? = null
    private var writer: SampleWavWriter? = null
    private var part: File? = null
    private var totalFrames = 0L
    private var receivedAt = 0L
    private var rate = 0
    private var channels = 0

    fun cancel() { stopped.set(true); playback?.stop() }
    fun window() = playback?.window()
    fun command(command: RadioPlaybackCommand) = playback?.send(command) == true
    fun startTake(seconds: Int, requestId: Long): Boolean {
        require(seconds in liveRadioTakeLengths && requestId in 0..0xffffffffL)
        return !stopped.get() && request.compareAndSet(null, Request(seconds, requestId))
    }
    fun finishTake(): Boolean {
        val active = request.get() ?: return false
        active.finish = true
        return true
    }

    private fun check() {
        if (stopped.get() || Thread.currentThread().isInterrupted) throw IOException("Cancelled")
        audioError.get()?.let { throw IOException(it.message ?: "Android live audio output failed", it) }
    }

    private fun startHistory(sampleRate: Int, channelCount: Int): AndroidRadioHistory {
        rate = sampleRate
        channels = channelCount
        receivedAt = SystemClock.elapsedRealtime()
        val ring = AndroidRadioHistory(rate, channels, historySeconds)
        val output = AndroidRadioPlayback(ring, events.ready,
            { at -> events.progress(at, captureProgress) },
            { error -> audioError.compareAndSet(null, error) })
        history = ring
        playback = output
        output.start()
        return ring
    }

    private fun paceInput() {
        // Keep the station's normal startup burst without letting a fast socket
        // fill the rolling window faster than real time during rewind.
        val earliest = receivedAt + totalFrames * 1000 / rate - 1250
        var wait = earliest - SystemClock.elapsedRealtime()
        while (wait > 0) {
            check()
            Thread.sleep(minOf(wait, 20))
            wait = earliest - SystemClock.elapsedRealtime()
        }
    }

    fun writeHistory(from: Long, until: Long, cancel: SourceCancel): RenderedSourceClip {
        cancel.check()
        check()
        val ring = checkNotNull(history) { "Wait for the radio buffer to fill" }
        val samples = ring.snapshot(from, until)
        check(directory.isDirectory || directory.mkdirs()) { "Cannot create the radio sample folder" }
        val temporary = File.createTempFile("Radio-Rewind-", ".part", directory)
        val destination = File(directory, temporary.nameWithoutExtension + ".wav")
        val credits = File(destination.path + ".source.json")
        var published = false
        try {
            SampleWavWriter(temporary, ring.rate, ring.channels).use { output ->
                val block = ShortArray(8192)
                var offset = 0
                while (offset < samples.size) {
                    cancel.check()
                    check()
                    val count = minOf(block.size, samples.size - offset)
                    samples.copyInto(block, 0, offset, offset + count)
                    output.writeSamples(block, count)
                    offset += count
                }
            }
            cancel.check()
            check()
            if (destination.exists() || credits.exists() || !temporary.renameTo(destination))
                throw IOException("Cannot publish the radio rewind WAV")
            published = true
            credits.writeText(JSONObject().put("version", 1).put("provider", "radio_browser")
                .put("title", station.title).put("creator", station.creator).put("source_url", station.page)
                .put("live", true).put("rewind", true).put("frames", until - from)
                .put("rate", ring.rate).put("channels", ring.channels)
                .put("in_ms", from * 1000 / ring.rate).put("out_ms", until * 1000 / ring.rate)
                .put("stream_start_frame", from).put("stream_end_frame", until)
                .put("retrieved_at_ms", System.currentTimeMillis())
                .put("rights", "Station discovery does not change broadcast reuse rights.").toString(2))
            cancel.check()
            check()
            return RenderedSourceClip(destination, until - from, ring.rate, ring.channels)
        } catch (error: Exception) {
            if (published) { destination.delete(); credits.delete() }
            throw error
        } finally {
            temporary.delete()
        }
    }

    private fun capture(active: Request, samples: ShortArray, frames: Int): Float {
        if (active.finish) {
            complete(active)
            return 0f
        }
        if (writer == null) {
            check(directory.isDirectory || directory.mkdirs()) { "Cannot create the radio sample folder" }
            val file = File.createTempFile("Radio-", ".part", directory)
            part = file
            writer = SampleWavWriter(file, rate, channels)
        }
        val output = checkNotNull(writer)
        val maximum = active.seconds.toLong() * rate
        val count = minOf(frames.toLong(), maximum - output.frames).toInt()
        output.writeSamples(samples, count * channels)
        if (output.frames >= maximum) {
            complete(active)
            return 1f
        }
        return output.frames.toFloat() / maximum
    }

    private fun complete(active: Request) {
        val output = writer
        val temporary = part
        if (output == null || temporary == null || output.frames == 0L) {
            discard()
            request.compareAndSet(active, null)
            events.complete(null, "No live audio was captured")
            return
        }
        writer = null
        output.close()
        val destination = File(directory, temporary.nameWithoutExtension + ".wav")
        val credits = File(destination.path + ".source.json")
        var published = false
        try {
            check()
            if (destination.exists() || credits.exists() || !temporary.renameTo(destination))
                throw IOException("Cannot publish the radio WAV")
            published = true
            credits.writeText(JSONObject().put("version", 1).put("provider", "radio_browser")
                .put("title", station.title).put("creator", station.creator).put("source_url", station.page)
                .put("live", true).put("frames", output.frames).put("rate", rate).put("channels", channels)
                .put("retrieved_at_ms", System.currentTimeMillis())
                .put("rights", "Station discovery does not change broadcast reuse rights.").toString(2))
            check()
            part = null
            request.compareAndSet(active, null)
            events.complete(RadioTake(RenderedSourceClip(destination, output.frames, rate, channels), active.id),
                "Live WAV ready")
        } catch (error: Exception) {
            if (published) {
                destination.delete()
                credits.delete()
            }
            throw error
        }
    }

    private fun discard() {
        runCatching { writer?.close() }
        writer = null
        part?.delete()
        part = null
    }

    private fun decode(input: InputStream) {
        val native = AndroidRadioNative.create()
        check(native != 0L)
        try {
            val compressed = ByteArray(8192)
            val decoded = ShortArray(2304)
            val startupDeadline = SystemClock.elapsedRealtime() + 20000
            val callback = AndroidRadioPcm { samples, frames, sampleRate, channelCount ->
                check()
                val ring = history ?: startHistory(sampleRate, channelCount)
                paceInput()
                ring.append(samples, frames)
                totalFrames += frames
                captureProgress = request.get()?.let { capture(it, samples, frames) } ?: 0f
            }
            while (true) {
                check()
                request.get()?.takeIf { it.finish }?.let(::complete)
                if (totalFrames == 0L && SystemClock.elapsedRealtime() >= startupDeadline)
                    throw IOException("Station did not provide MP3 audio in time")
                val count = input.read(compressed)
                AndroidRadioNative.feed(native, compressed, count, decoded, callback)
                if (count < 0) break
            }
            if (totalFrames == 0L) throw IOException("Station returned no playable MP3 audio")
        } finally {
            AndroidRadioNative.destroy(native)
        }
    }

    fun run(fixture: InputStream? = null) {
        var message = "Live stream ended"
        try {
            if (fixture != null) {
                fixture.use(::decode)
            } else {
                val urls = AndroidRadioNetwork.urls(station, ::check)
                for ((index, url) in urls.withIndex()) {
                    try {
                        AndroidRadioNetwork.receive(url, station, ::check, ::decode)
                        break
                    } catch (error: Exception) {
                        check()
                        if (history != null || index == urls.lastIndex) throw error
                    }
                }
            }
            request.get()?.takeIf { it.finish }?.let(::complete)
            if (request.get() != null) message = "Live audio ended before the take was complete"
        } catch (error: Exception) {
            message = error.message ?: "Live radio failed"
            // A user-requested early finish can save the frames already captured if the next
            // network read fails. Stop/close still discard the unfinished take.
            val manual = request.get()?.takeIf { it.finish && writer != null && !stopped.get() }
            if (manual != null) {
                try { complete(manual) }
                catch (finalizeError: Exception) { message = finalizeError.message ?: "Cannot finish live WAV" }
            }
        } finally {
            discard()
            playback?.stop()
            if (stopped.compareAndSet(false, true)) events.ended(message)
        }
    }
}
