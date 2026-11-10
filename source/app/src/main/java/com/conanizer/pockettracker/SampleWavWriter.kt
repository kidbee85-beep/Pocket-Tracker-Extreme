package com.conanizer.pockettracker

import java.io.Closeable
import java.io.File
import java.io.RandomAccessFile

/** Streaming PCM16 WAV. Memory usage is bounded by the caller's read buffer. */
internal class SampleWavWriter(val file: File, val rate: Int, val channels: Int) : Closeable {
    private val output: RandomAccessFile
    private var bytes = ByteArray(0)
    private var closed = false
    var frames: Long = 0
        private set

    init {
        require(rate in 8000..192000 && channels in 1..2)
        output = RandomAccessFile(file, "rw")
        try {
            output.setLength(0)
            output.write(ByteArray(44))
            writeHeader(0)
            output.seek(44)
        } catch (e: Exception) {
            output.close()
            throw e
        }
    }

    fun writeSamples(samples: ShortArray, count: Int) {
        check(!closed)
        require(count in 0..samples.size)
        if (bytes.size < count * 2) bytes = ByteArray(count * 2)
        for (i in 0 until count) {
            val sample = samples[i].toInt()
            bytes[i * 2] = sample.toByte()
            bytes[i * 2 + 1] = (sample shr 8).toByte()
        }
        output.write(bytes, 0, count * 2)
        frames = (output.filePointer - 44) / (channels * 2)
    }

    private fun short(value: Int) {
        output.write(value and 255)
        output.write(value ushr 8 and 255)
    }

    private fun int(value: Long) {
        repeat(4) { output.write((value ushr (it * 8) and 255).toInt()) }
    }

    private fun writeHeader(dataBytes: Long) {
        require(dataBytes in 0..0xffffffffL - 36)
        output.seek(0)
        output.writeBytes("RIFF"); int(dataBytes + 36); output.writeBytes("WAVEfmt ")
        int(16); short(1); short(channels); int(rate.toLong()); int(rate.toLong() * channels * 2)
        short(channels * 2); short(16); output.writeBytes("data"); int(dataBytes)
    }

    override fun close() {
        if (closed) return
        closed = true
        try {
            // A short read or disk error may end between frames; retain only complete PCM frames.
            val frameBytes = channels * 2L
            val dataBytes = ((output.length() - 44).coerceAtLeast(0) / frameBytes) * frameBytes
            output.setLength(44 + dataBytes)
            frames = dataBytes / frameBytes
            writeHeader(dataBytes)
        } finally {
            output.close()
        }
    }
}

/** Route policy shared with the JVM checks: a preference alone never confirms an input. */
internal object CapturePolicy {
    fun supports(reported: IntArray, requested: Int): Boolean = reported.isEmpty() || requested in reported
    fun routeVerified(requestedId: Int, actualId: Int?): Boolean =
        actualId != null && actualId > 0 && (requestedId == 0 || requestedId == actualId)
    fun routeChanged(verifiedId: Int, actualId: Int?): Boolean = verifiedId > 0 && actualId != verifiedId
}
