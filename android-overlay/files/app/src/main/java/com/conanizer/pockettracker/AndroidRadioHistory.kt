package com.conanizer.pockettracker

import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import android.os.SystemClock
import java.io.IOException
import java.util.concurrent.ArrayBlockingQueue
import java.util.concurrent.atomic.AtomicBoolean

internal data class RadioWindow(
    val rate: Int,
    val first: Long,
    val end: Long,
    val heard: Long,
    val rewound: Boolean
) {
    fun ms(frame: Long) = frame.coerceAtLeast(0) * 1000 / rate
    fun frame(ms: Long) = ms * rate / 1000
    fun contains(from: Long, until: Long) = from >= first && until <= end && from < until
}

/** A fixed PCM16 ring shared by the decoder, playback and the occasional WAV export. */
internal class AndroidRadioHistory(val rate: Int, val channels: Int, seconds: Int) {
    private val capacity = rate * seconds
    private val audio = ShortArray(capacity * channels)
    private var received = 0L

    private fun first() = maxOf(0, received - capacity)

    @Synchronized
    fun append(samples: ShortArray, frames: Int) {
        require(frames in 1..capacity && frames * channels <= samples.size)
        val index = (received % capacity).toInt()
        val leading = minOf(frames, capacity - index) * channels
        samples.copyInto(audio, index * channels, 0, leading)
        samples.copyInto(audio, 0, leading, frames * channels)
        received += frames
    }

    @Synchronized
    fun read(frame: Long, output: ShortArray): Int {
        if (frame < first()) return -1
        val count = minOf((received - frame).coerceAtLeast(0), (output.size / channels).toLong()).toInt()
        if (count > 0) copy(frame, count, output)
        return count
    }

    @Synchronized
    fun window(heard: Long, rewound: Boolean) =
        RadioWindow(rate, first(), received, heard.coerceIn(first(), received), rewound)

    @Synchronized
    fun snapshot(first: Long, end: Long): ShortArray {
        require(first >= first() && end <= received && first < end) { "That range has left the radio buffer" }
        val frames = (end - first).toInt()
        return ShortArray(frames * channels).also { copy(first, frames, it) }
    }

    private fun copy(frame: Long, frames: Int, output: ShortArray) {
        val index = (frame % capacity).toInt()
        val leading = minOf(frames, capacity - index) * channels
        audio.copyInto(output, 0, index * channels, index * channels + leading)
        audio.copyInto(output, leading, 0, frames * channels - leading)
    }
}

internal sealed interface RadioPlaybackCommand {
    data class Seek(val deltaMs: Long) : RadioPlaybackCommand
    data object Edge : RadioPlaybackCommand
    data object Pause : RadioPlaybackCommand
    data object Resume : RadioPlaybackCommand
}

/** AudioTrack and its read position belong to one worker. The stream keeps decoding during seek/pause. */
internal class AndroidRadioPlayback(
    private val history: AndroidRadioHistory,
    private val ready: () -> Unit,
    private val progress: (Long) -> Unit,
    private val failed: (Exception) -> Unit
) {
    private val commands = ArrayBlockingQueue<RadioPlaybackCommand>(16)
    private val running = AtomicBoolean(true)
    @Volatile private var heard = 0L
    @Volatile private var rewound = false
    private val thread = Thread({ run() }, "TrackWerkRadioOutput")

    fun start() = thread.start()
    fun stop() { running.set(false); thread.interrupt() }
    fun send(command: RadioPlaybackCommand) = running.get() && commands.offer(command)
    fun window() = history.window(heard, rewound)

    private fun createTrack(): AudioTrack {
        val rate = history.rate
        val channels = history.channels
        val mask = if (channels == 1) AudioFormat.CHANNEL_OUT_MONO else AudioFormat.CHANNEL_OUT_STEREO
        val minimum = AudioTrack.getMinBufferSize(rate, mask, AudioFormat.ENCODING_PCM_16BIT)
        if (minimum <= 0) throw IOException("Android cannot play this station's sample rate")
        return AudioTrack.Builder().setAudioAttributes(AudioAttributes.Builder()
            .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).setUsage(AudioAttributes.USAGE_MEDIA).build())
            .setAudioFormat(AudioFormat.Builder().setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                .setSampleRate(rate).setChannelMask(mask).build())
            .setTransferMode(AudioTrack.MODE_STREAM).setBufferSizeInBytes(maxOf(minimum, rate * channels / 5))
            .build()
    }

    private fun run() {
        var audio: AudioTrack? = null
        try {
            if (!running.get()) return
            val track = createTrack()
            audio = track
            check(track.state == AudioTrack.STATE_INITIALIZED) { "Cannot initialize Android live audio" }
            val buffer = ShortArray((history.rate / 100) * history.channels)
            var base = 0L
            var submitted = 0L
            var played = 0L
            var head = 0L
            var paused = false
            var lastProgress = 0L
            var drainDeadline = 0L

            fun reset(at: Long, play: Boolean) {
                track.pause()
                track.flush()
                base = at
                heard = at
                submitted = 0
                played = 0
                head = 0
                drainDeadline = 0
                paused = !play
                if (play) track.play()
            }

            fun edge(window: RadioWindow) = maxOf(window.first, window.end - history.rate / 8)

            track.play()
            ready()
            while (running.get()) {
                val nextHead = track.playbackHeadPosition.toLong() and 0xffffffffL
                played += (nextHead - head) and 0xffffffffL
                head = nextHead
                heard = base + minOf(submitted, played)

                val command = commands.poll()
                if (command != null) {
                    val window = window()
                    when (command) {
                        is RadioPlaybackCommand.Seek -> {
                            val target = window.heard + command.deltaMs * history.rate / 1000
                            rewound = target < window.end
                            reset(if (rewound) maxOf(window.first, target) else edge(window), true)
                        }
                        RadioPlaybackCommand.Edge -> { rewound = false; reset(edge(window), true) }
                        RadioPlaybackCommand.Pause -> { rewound = true; reset(window.heard, false) }
                        RadioPlaybackCommand.Resume -> reset(window.heard, true)
                    }
                }

                val now = SystemClock.elapsedRealtime()
                if (now - lastProgress >= 100 || command != null) {
                    lastProgress = now
                    progress(heard * 1000 / history.rate)
                }
                if (paused) {
                    Thread.sleep(20)
                    continue
                }

                val count = history.read(base + submitted, buffer)
                if (count < 0) {
                    reset(window().first, true)
                    continue
                }
                if (count == 0) {
                    drainDeadline = 0
                    Thread.sleep(5)
                    continue
                }
                val written = track.write(buffer, 0, count * history.channels, AudioTrack.WRITE_NON_BLOCKING)
                if (written < 0) throw IOException("Android live audio output failed ($written)")
                if (written > 0) {
                    submitted += written / history.channels
                    drainDeadline = now + 3000
                } else {
                    if (drainDeadline == 0L) drainDeadline = now + 3000
                    if (now >= drainDeadline) throw IOException("Android live audio output stopped draining")
                    Thread.sleep(3)
                }
            }
        } catch (error: Exception) {
            if (running.get()) failed(error)
        } finally {
            running.set(false)
            runCatching { audio?.pause() }
            runCatching { audio?.flush() }
            runCatching { audio?.release() }
        }
    }
}
