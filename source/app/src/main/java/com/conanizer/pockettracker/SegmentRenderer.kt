package com.conanizer.pockettracker

import android.media.MediaCodec
import android.media.MediaDataSource
import android.media.MediaExtractor
import android.media.MediaFormat
import android.media.AudioFormat
import android.os.SystemClock
import java.io.File
import java.io.IOException
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.ceil
import kotlin.math.roundToInt
import kotlin.math.roundToLong

/** Bounded random HTTP reads: MediaExtractor can seek without saving an entire album or video. */
internal class SourceRangeDataSource(private val url: String, private val cancel: SourceCancel) : MediaDataSource() {
    private var knownLength = -1L
    private var offset = -1L
    private var cached = ByteArray(0)
    // Ogg's extractor revisits the file header and tail while finding granule positions. Retaining
    // only the last block turns that normal seeking into repeated HTTP requests and throttling.
    private val blocks = object : LinkedHashMap<Long, ByteArray>(12, .75f, true) {
        override fun removeEldestEntry(eldest: MutableMap.MutableEntry<Long, ByteArray>?): Boolean = size > 8
    }
    @Volatile private var readFailure: IOException? = null
    private var initialTransferChecked = false
    private var wholeFile: ByteArray? = null
    private var received = 0L
    private var closed = false

    // Commons serves many short Ogg recordings. One bounded ordinary transfer is friendlier than
    // its extractor's header/tail range dance. The strategy is selected before any range request;
    // a server refusal never causes a switch of URL, user agent or transfer mode.
    private fun preloadSmallCommons() {
        if (initialTransferChecked) return
        initialTransferChecked = true
        if (java.net.URI(url).host != "upload.wikimedia.org") return
        val connection = SourceNet.open(url, cancel)
        try {
            val status = connection.responseCode
            if (status == 429 || status == 503)
                throw IOException("Audio source returned HTTP $status (temporarily busy). Try again later.")
            if (status != 200) throw IOException("Audio source returned HTTP $status")
            if (connection.contentType.orEmpty().startsWith("video/", true))
                throw IOException("This source does not provide audio-only content")
            val length = connection.contentLengthLong
            if (length !in 1..16L * 1024 * 1024) return // Close without saving a large/unknown body.
            val data = ByteArray(length.toInt())
            var position = 0
            connection.inputStream.use { input ->
                while (position < data.size) {
                    cancel.check()
                    val read = input.read(data, position, minOf(65536, data.size - position))
                    if (read < 0) throw IOException("The public audio transfer ended early")
                    if (read == 0) continue
                    position += read
                }
            }
            cancel.check(); received += data.size
            wholeFile = data; knownLength = length
        } finally { connection.disconnect(); cancel.connection = null }
    }

    private fun rememberFailure(problem: Exception): IOException {
        val error = if (problem is IOException) problem else IOException(problem.message ?: "Audio source read failed", problem)
        readFailure = error; cancel.connection?.disconnect(); cancel.connection = null
        return error
    }

    @Synchronized private fun block(position: Long) {
        cancel.check(); check(!closed)
        val start = position / BLOCK * BLOCK
        blocks[start]?.let { offset = start; cached = it; return }
        val last = if (knownLength >= 0) minOf(start + BLOCK - 1, knownLength - 1) else start + BLOCK - 1
        val connection = openWithBackoff("bytes=$start-$last")
        try {
            val code = connection.responseCode
            if (code == 416) { offset = start; cached = ByteArray(0); return }
            if (code == 206) {
                val range = Regex("bytes (\\d+)-(\\d+)/(\\d+|\\*)", RegexOption.IGNORE_CASE)
                    .matchEntire(connection.getHeaderField("Content-Range").orEmpty())
                    ?: throw IOException("Invalid source byte range")
                if (range.groupValues[1].toLong() != start) throw IOException("Source returned another byte range")
                knownLength = range.groupValues[3].toLongOrNull() ?: knownLength
            } else if (code == 200 && start == 0L) {
                knownLength = connection.contentLengthLong
            } else if (code == 200) throw IOException("This source does not support seeking by byte range")
            else throw IOException("Audio source returned HTTP $code")
            if (knownLength > 8L * 1024 * 1024 * 1024) throw IOException("Audio source exceeds the supported size")
            val buffer = ByteArray(BLOCK)
            var count = 0
            connection.inputStream.use { input ->
                while (count < buffer.size) {
                    cancel.check(); val read = input.read(buffer, count, buffer.size - count)
                    if (read < 0) break
                    if (read == 0) continue
                    count += read; received += read
                    if (received > 192L * 1024 * 1024) throw IOException("Source read limit reached; choose a nearer or shorter region, or import the file locally")
                }
            }
            cached = buffer.copyOf(count); offset = start; blocks[start] = cached
            if (knownLength < 0 && count < BLOCK) knownLength = start + count
        } finally { connection.disconnect(); cancel.connection = null }
    }

    private fun openWithBackoff(range: String): java.net.HttpURLConnection {
        for (attempt in 0..1) {
            cancel.check()
            val connection = SourceNet.open(url, cancel, range)
            val status = connection.responseCode
            if (status != 429 && status != 503) return connection
            val retrySeconds = connection.getHeaderField("Retry-After")?.toLongOrNull()
            connection.disconnect(); cancel.connection = null
            if (attempt == 1 || (retrySeconds != null && retrySeconds > 3))
                throw IOException("Audio source returned HTTP $status (temporarily busy). Try again later.")
            val waitMs = retrySeconds?.times(1000)?.coerceIn(500, 3000) ?: 1000
            val until = SystemClock.elapsedRealtime() + waitMs
            while (SystemClock.elapsedRealtime() < until) { cancel.check(); Thread.sleep(50) }
        }
        throw IOException("Audio source is temporarily unavailable")
    }

    // MediaDataSource's JNI adapter can turn IOException into an EOF return. Keep the actual
    // transport error so the renderer reports HTTP/cancellation rather than an empty-audio error.
    fun checkFailure() { readFailure?.let { throw it } }

    @Synchronized override fun readAt(position: Long, buffer: ByteArray, bufferOffset: Int, size: Int): Int {
        cancel.check(); check(!closed)
        if (position < 0 || bufferOffset < 0 || size < 0 || size > buffer.size - bufferOffset) throw IOException("Invalid audio read")
        if (size == 0) return 0
        try { preloadSmallCommons() } catch (problem: Exception) { throw rememberFailure(problem) }
        if (knownLength >= 0 && position >= knownLength) return -1
        wholeFile?.let { data ->
            val count = minOf(size.toLong(), data.size.toLong() - position).toInt()
            if (count <= 0) return -1
            data.copyInto(buffer, bufferOffset, position.toInt(), position.toInt() + count)
            return count
        }
        var copied = 0
        while (copied < size) {
            val at = position + copied
            if (at < offset || at >= offset + cached.size) try { block(at) } catch (problem: Exception) {
                throw rememberFailure(problem)
            }
            val index = (at - offset).toInt()
            if (index !in cached.indices) break
            val count = minOf(size - copied, cached.size - index)
            cached.copyInto(buffer, bufferOffset + copied, index, index + count); copied += count
        }
        return if (copied == 0) -1 else copied
    }

    @Synchronized override fun getSize(): Long {
        try { preloadSmallCommons() } catch (problem: Exception) { throw rememberFailure(problem) }
        if (knownLength < 0) try { block(0) } catch (problem: Exception) {
            throw rememberFailure(problem)
        }
        return knownLength
    }
    @Synchronized override fun close() { closed = true; cached = ByteArray(0); blocks.clear(); wholeFile = null }
    companion object { private const val BLOCK = 256 * 1024 }
}

internal data class RenderedSourceClip(val file: File, val frames: Long, val rate: Int, val channels: Int)

internal object SegmentRenderer {
    fun render(track: SourceTrack, startMs: Long, endMs: Long, directory: File, cancel: SourceCancel,
               progress: (Float) -> Unit): RenderedSourceClip {
        require(SourceNet.allowed(track.audioUrl)) { "Only a public provider audio URL can be sampled" }
        val data = SourceRangeDataSource(track.audioUrl, cancel)
        return extract(startMs, endMs, directory, cancel, progress, { it.setDataSource(data) }, { data.close() }, { data.checkFailure() })
    }

    // Tests use owned local fixtures; the web command API never accepts local paths.
    fun renderLocal(file: File, startMs: Long, endMs: Long, directory: File, cancel: SourceCancel,
                    progress: (Float) -> Unit = {}): RenderedSourceClip =
        extract(startMs, endMs, directory, cancel, progress, { it.setDataSource(file.absolutePath) }, {})

    private fun extract(startMs: Long, endMs: Long, directory: File, cancel: SourceCancel,
                        progress: (Float) -> Unit, source: (MediaExtractor) -> Unit, closeSource: () -> Unit,
                        checkSource: () -> Unit = {}): RenderedSourceClip {
        require(startMs >= 0 && startMs <= Int.MAX_VALUE && endMs > startMs && endMs - startMs <= 180000) {
            "Choose a clip between 1 ms and 180 seconds"
        }
        if (!directory.isDirectory && !directory.mkdirs()) throw IOException("Cannot create source sample folder")
        if (directory.usableSpace < 2L * 1024 * 1024) throw IOException("Not enough free space")
        val extractor = MediaExtractor()
        var codec: MediaCodec? = null
        var writer: SampleWavWriter? = null
        var file: File? = null
        var success = false
        val startUs = startMs * 1000; val endUs = endMs * 1000
        val deadline = SystemClock.elapsedRealtime() + 180000
        var lastDiskCheck = SystemClock.elapsedRealtime()
        var rate = 0; var channels = 0; var encoding = AudioFormat.ENCODING_PCM_16BIT
        var mp3FrameCursor: Long? = null
        fun pcm(buffer: ByteBuffer, offset: Int, size: Int, pts: Long) {
            cancel.check()
            if (rate !in 8000..192000 || channels !in 1..8) throw IOException("Unsupported decoded audio format")
            val sampleBytes = when (encoding) {
                AudioFormat.ENCODING_PCM_16BIT -> 2
                AudioFormat.ENCODING_PCM_FLOAT -> 4
                AudioFormat.ENCODING_PCM_8BIT -> 1
                AudioFormat.ENCODING_PCM_24BIT_PACKED -> 3
                AudioFormat.ENCODING_PCM_32BIT -> 4
                else -> throw IOException("Unsupported decoded PCM encoding")
            }
            val frames = size / (channels * sampleBytes)
            // Extractor timestamps are integer microseconds; a packet at frame 1024 can be stamped
            // 21333us rather than 21333.333us. Quantize its absolute frame origin once, instead of
            // repeatedly rounding relative cuts and occasionally writing an extra WAV frame.
            // Android's MP3 SkipCutBuffer removes encoder delay and retains end padding without
            // retiming each returned buffer. Those packet PTS no longer describe the PCM bytes.
            // Count the actual gapless PCM frames instead; never fill missing time with silence.
            val originFrame = mp3FrameCursor ?: (pts.toDouble() * rate / 1000000).roundToLong()
            if (mp3FrameCursor != null) mp3FrameCursor = originFrame + frames
            val startFrame = ceil(startUs.toDouble() * rate / 1000000).toLong()
            val endFrame = ceil(endUs.toDouble() * rate / 1000000).toLong()
            val from = (startFrame - originFrame).coerceIn(0L, frames.toLong()).toInt()
            val until = (endFrame - originFrame).coerceIn(0L, frames.toLong()).toInt()
            if (until <= from) return
            val outputChannels = minOf(channels, 2)
            var output = writer
            if (output == null) {
                file = File.createTempFile("Clip_${System.currentTimeMillis()}_", ".wav", directory)
                output = SampleWavWriter(file!!, rate, outputChannels); writer = output
            }
            if (output.rate != rate || output.channels != outputChannels) throw IOException("Source changed its PCM format")
            val view = buffer.duplicate().order(ByteOrder.LITTLE_ENDIAN)
            val samples = ShortArray(1024 * outputChannels)
            var frame = from
            while (frame < until) {
                cancel.check(); val count = minOf(until - frame, 1024)
                for (i in 0 until count) for (channel in 0 until outputChannels) {
                    val at = offset + ((frame + i) * channels + channel) * sampleBytes
                    val value = when (encoding) {
                        AudioFormat.ENCODING_PCM_FLOAT -> {
                            val v = view.getFloat(at); if (v.isFinite()) (v.coerceIn(-1f, 1f) * 32767).roundToInt() else 0
                        }
                        AudioFormat.ENCODING_PCM_8BIT -> ((view.get(at).toInt() and 255) - 128) shl 8
                        AudioFormat.ENCODING_PCM_24BIT_PACKED ->
                            ((view.get(at + 1).toInt() and 255) or (view.get(at + 2).toInt() shl 8))
                        AudioFormat.ENCODING_PCM_32BIT -> view.getInt(at) shr 16
                        else -> view.getShort(at).toInt()
                    }
                    samples[i * outputChannels + channel] = value.coerceIn(-32768, 32767).toShort()
                }
                output.writeSamples(samples, count * outputChannels); frame += count
            }
            progress(((pts + frames * 1000000L / rate - startUs).toDouble() / (endUs - startUs)).toFloat().coerceIn(0f, 1f))
            if (SystemClock.elapsedRealtime() - lastDiskCheck > 1000) {
                lastDiskCheck = SystemClock.elapsedRealtime()
                if (directory.usableSpace < 2L * 1024 * 1024) throw IOException("Not enough free space for this clip")
            }
        }
        try {
            source(extractor); cancel.check(); checkSource()
            val track = (0 until extractor.trackCount).firstOrNull {
                extractor.getTrackFormat(it).getString(MediaFormat.KEY_MIME)?.startsWith("audio/") == true
            } ?: throw IOException("No readable audio track")
            val format = extractor.getTrackFormat(track)
            val mime = format.getString(MediaFormat.KEY_MIME) ?: throw IOException("Audio format is missing")
            rate = format.getInteger(MediaFormat.KEY_SAMPLE_RATE); channels = format.getInteger(MediaFormat.KEY_CHANNEL_COUNT)
            val isMp3 = mime == MediaFormat.MIMETYPE_AUDIO_MPEG
            // Android's MP3 byte-rate seeker is approximate, and frames share a bit reservoir.
            // Decode from the original beginning for an exact gapless sample timeline; discard
            // PCM before IN. The existing time, network-byte and cancellation bounds still apply.
            extractor.selectTrack(track)
            extractor.seekTo(if (isMp3) 0 else startUs, MediaExtractor.SEEK_TO_PREVIOUS_SYNC)
            if (isMp3) mp3FrameCursor = 0L
            if (mime == "audio/raw") {
                if (format.containsKey(MediaFormat.KEY_PCM_ENCODING)) encoding = format.getInteger(MediaFormat.KEY_PCM_ENCODING)
                val buffer = ByteBuffer.allocateDirect(1024 * 1024)
                while (true) {
                    cancel.check(); checkSource(); if (SystemClock.elapsedRealtime() > deadline) throw IOException("Clip rendering timed out")
                    val pts = extractor.sampleTime; if (pts < 0 || pts >= endUs) break
                    buffer.clear(); val count = extractor.readSampleData(buffer, 0); if (count < 0) break
                    pcm(buffer, 0, count, pts); extractor.advance()
                }
            } else {
                format.setInteger(MediaFormat.KEY_PCM_ENCODING, AudioFormat.ENCODING_PCM_16BIT)
                val decoder = MediaCodec.createDecoderByType(mime); codec = decoder
                decoder.configure(format, null, null, 0); decoder.start()
                val info = MediaCodec.BufferInfo(); var inputEnded = false; var outputEnded = false
                while (!outputEnded) {
                    cancel.check(); checkSource(); if (SystemClock.elapsedRealtime() > deadline) throw IOException("Clip rendering timed out")
                    if (!inputEnded) {
                        val index = decoder.dequeueInputBuffer(10000)
                        if (index >= 0) {
                            val buffer = decoder.getInputBuffer(index) ?: throw IOException("Decoder input unavailable")
                            buffer.clear(); val time = extractor.sampleTime
                            val size = if (time < 0 || time >= endUs + 1000000) -1 else extractor.readSampleData(buffer, 0)
                            if (size < 0) { decoder.queueInputBuffer(index, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM); inputEnded = true }
                            else { decoder.queueInputBuffer(index, 0, size, time, 0); extractor.advance() }
                        }
                    }
                    val index = decoder.dequeueOutputBuffer(info, 10000)
                    if (index == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                        val actual = decoder.outputFormat
                        rate = actual.getInteger(MediaFormat.KEY_SAMPLE_RATE); channels = actual.getInteger(MediaFormat.KEY_CHANNEL_COUNT)
                        encoding = if (actual.containsKey(MediaFormat.KEY_PCM_ENCODING)) actual.getInteger(MediaFormat.KEY_PCM_ENCODING) else AudioFormat.ENCODING_PCM_16BIT
                    } else if (index >= 0) {
                        try {
                            if (info.size > 0) decoder.getOutputBuffer(index)?.let { pcm(it, info.offset, info.size, info.presentationTimeUs) }
                            val reachedEnd = mp3FrameCursor?.let { it >= ceil(endUs.toDouble() * rate / 1000000).toLong() }
                                ?: (info.presentationTimeUs >= endUs)
                            outputEnded = info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0 || reachedEnd
                        } finally { decoder.releaseOutputBuffer(index, false) }
                    }
                }
            }
            cancel.check(); checkSource(); writer?.close()
            val output = writer ?: throw IOException("Selected region contains no decodable audio")
            if (output.frames <= 0) throw IOException("Selected region is empty")
            success = true; progress(1f)
            return RenderedSourceClip(output.file, output.frames, output.rate, output.channels)
        } catch (problem: Exception) {
            checkSource(); throw problem
        } finally {
            try { codec?.stop() } catch (_: Exception) {}
            try { codec?.release() } catch (_: Exception) {}
            try { extractor.release() } catch (_: Exception) {}
            try { closeSource() } catch (_: Exception) {}
            try { writer?.close() } catch (_: Exception) {}
            if (!success) file?.delete()
        }
    }
}
