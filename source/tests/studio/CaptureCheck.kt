package com.conanizer.pockettracker
// kotlinc app/src/main/java/com/conanizer/pockettracker/SampleWavWriter.kt \
//   tests/studio/CaptureCheck.kt -include-runtime -d capture-check.jar
// java -jar capture-check.jar /tmp/pocketstudio-capture-check
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

fun main(args: Array<String>) {
    val out = File(args[0]); out.mkdirs()
    fun inspect(name: String, rate: Int, channels: Int, samples: ShortArray, frames: Long) {
        val file = File(out, name)
        val writer = SampleWavWriter(file, rate, channels)
        writer.writeSamples(samples, samples.size)
        writer.close(); writer.close()
        val bytes = file.readBytes()
        val data = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        check(String(bytes, 0, 4) == "RIFF" && String(bytes, 8, 8) == "WAVEfmt ")
        check(data.getInt(4) == bytes.size - 8)
        check(data.getInt(16) == 16 && data.getShort(20).toInt() == 1)
        check(data.getShort(22).toInt() == channels && data.getInt(24) == rate)
        check(data.getInt(28) == rate * channels * 2 && data.getShort(32).toInt() == channels * 2)
        check(data.getShort(34).toInt() == 16 && String(bytes,36,4) == "data")
        check(data.getInt(40) == frames.toInt() * channels * 2)
        check(bytes.size.toLong() == 44 + frames * channels * 2 && writer.frames == frames)
        for (i in 0 until frames.toInt() * channels) check(data.getShort(44+i*2) == samples[i])
        check(runCatching { writer.writeSamples(samples, 1) }.isFailure)
    }
    inspect("mono.wav", 48000, 1, shortArrayOf(-32768,-1,0,1,32767), 5)
    inspect("stereo.wav", 44100, 2, shortArrayOf(-30000,15000,-1,1,32767,-32768), 3)
    inspect("trim-partial-frame.wav", 48000, 2, shortArrayOf(200,-200,900), 1)
    inspect("empty.wav", 48000, 1, shortArrayOf(), 0)
    val fragmented = File(out, "fragmented.wav")
    SampleWavWriter(fragmented,48000,2).use { w ->
        w.writeSamples(shortArrayOf(1,2,3),3); w.writeSamples(shortArrayOf(4,5,6),3)
        check(w.frames==3L)
    }
    val f = ByteBuffer.wrap(fragmented.readBytes()).order(ByteOrder.LITTLE_ENDIAN)
    for (i in 1..6) check(f.getShort(44+(i-1)*2).toInt()==i)
    val streamed = File(out,"streamed.wav")
    val buffer = ShortArray(2048) { (it - 1024).toShort() }
    SampleWavWriter(streamed,48000,2).use { w -> repeat(256) { w.writeSamples(buffer,buffer.size) }; check(w.frames==262144L) }
    check(streamed.length()==44L+262144L*4)
    check(runCatching { SampleWavWriter(File(out,"bad.wav"),0,2) }.isFailure)
    check(!CapturePolicy.routeVerified(7,1)); check(CapturePolicy.routeVerified(7,7))
    check(!CapturePolicy.routeVerified(7,null)); check(!CapturePolicy.routeVerified(0,null))
    check(CapturePolicy.routeVerified(0,1)); check(CapturePolicy.routeChanged(7,1))
    check(CapturePolicy.routeChanged(7,null)); check(!CapturePolicy.routeChanged(7,7))
    check(CapturePolicy.supports(intArrayOf(),48000)); check(CapturePolicy.supports(intArrayOf(44100,48000),48000))
    check(!CapturePolicy.supports(intArrayOf(44100),48000))
    println("PASS: PCM16 mono/stereo headers, exact bytes, fragmented reads, partial frame trim, empty/idempotent close, bounded streaming, actual-route policy")
}
