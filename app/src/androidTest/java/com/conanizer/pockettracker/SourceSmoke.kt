package com.conanizer.pockettracker

import android.app.Activity
import android.app.Instrumentation
import android.content.Intent
import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.media.MediaMuxer
import android.media.MediaExtractor
import android.os.Bundle
import android.os.SystemClock
import org.json.JSONObject
import java.io.File
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.sin

/** Local decoder tests; -e network true additionally exercises a public provider through SourceSampler. */
class SourceSmoke : Instrumentation() {
    private var arguments = Bundle()
    override fun onCreate(arguments: Bundle?) {
        super.onCreate(arguments); this.arguments = arguments ?: Bundle(); start()
    }
    private fun fixture(frame: Int, channel: Int): Short =
        (sin(frame * (if (channel == 0) .037 else .051)) * 9000).toInt().toShort()
    private fun aac(file: File) {
        val encoder = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_AUDIO_AAC)
        val muxer = MediaMuxer(file.absolutePath, MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4)
        var muxing = false
        try {
            val format = MediaFormat.createAudioFormat(MediaFormat.MIMETYPE_AUDIO_AAC, 48000, 2)
            format.setInteger(MediaFormat.KEY_AAC_PROFILE, MediaCodecInfo.CodecProfileLevel.AACObjectLC)
            format.setInteger(MediaFormat.KEY_BIT_RATE, 128000)
            encoder.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE); encoder.start()
            val info = MediaCodec.BufferInfo(); var frames = 0; var inputEnded = false; var ended = false; var track = -1
            val until = SystemClock.elapsedRealtime() + 15000
            while (!ended) {
                check(SystemClock.elapsedRealtime() < until) { "AAC fixture encoding timed out" }
                if (!inputEnded) {
                    val index = encoder.dequeueInputBuffer(10000)
                    if (index >= 0) {
                        val input = encoder.getInputBuffer(index)!!.order(ByteOrder.LITTLE_ENDIAN); input.clear()
                        val count = minOf(1024, 96000 - frames, input.capacity() / 4)
                        if (count <= 0) { encoder.queueInputBuffer(index, 0, 0, frames * 1000000L / 48000, MediaCodec.BUFFER_FLAG_END_OF_STREAM); inputEnded = true }
                        else {
                            repeat(count) { i -> input.putShort(fixture(frames + i, 0)); input.putShort(fixture(frames + i, 1)) }
                            encoder.queueInputBuffer(index, 0, count * 4, frames * 1000000L / 48000, 0); frames += count
                        }
                    }
                }
                val index = encoder.dequeueOutputBuffer(info, 10000)
                if (index == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) { track = muxer.addTrack(encoder.outputFormat); muxer.start(); muxing = true }
                else if (index >= 0) {
                    try {
                        if (info.size > 0 && info.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG == 0) {
                            check(muxing); val output = encoder.getOutputBuffer(index)!!
                            output.position(info.offset); output.limit(info.offset + info.size); muxer.writeSampleData(track, output, info)
                        }
                        ended = info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0
                    } finally { encoder.releaseOutputBuffer(index, false) }
                }
            }
        } finally {
            try { encoder.stop() } catch (_: Exception) {}; encoder.release()
            if (muxing) muxer.stop(); muxer.release()
        }
    }
    private fun validate(clip: RenderedSourceClip, frames: Long? = null) {
        check(clip.frames > 0 && clip.rate in 8000..192000 && clip.channels in 1..2)
        val header = ByteArray(44); RandomAccessFile(clip.file, "r").use { it.readFully(header) }
        val data = ByteBuffer.wrap(header).order(ByteOrder.LITTLE_ENDIAN)
        check(String(header,0,4) == "RIFF" && String(header,8,8) == "WAVEfmt ")
        check(data.getShort(20).toInt() == 1 && data.getShort(34).toInt() == 16)
        check(clip.file.length() == 44 + clip.frames * clip.channels * 2)
        check(data.getInt(40).toLong() == clip.frames * clip.channels * 2)
        if (frames != null) check(kotlin.math.abs(clip.frames - frames) <= 2) { "Clip length ${clip.frames}, expected $frames" }
    }
    private fun mp3Trace(file: File): String {
        val extractor = MediaExtractor(); var codec: MediaCodec? = null
        val trace = StringBuilder()
        try {
            extractor.setDataSource(file.absolutePath)
            val format = extractor.getTrackFormat(0)
            trace.append("input=").append(format).append('\n')
            extractor.selectTrack(0); extractor.seekTo(0,MediaExtractor.SEEK_TO_PREVIOUS_SYNC)
            val decoder = MediaCodec.createDecoderByType(format.getString(MediaFormat.KEY_MIME)!!); codec = decoder
            decoder.configure(format,null,null,0);decoder.start()
            val info = MediaCodec.BufferInfo(); var outputs = 0; var inputs = 0
            val deadline = SystemClock.elapsedRealtime()+10000
            while (outputs<12 && SystemClock.elapsedRealtime()<deadline) {
                val index=decoder.dequeueInputBuffer(1000)
                if(index>=0) {
                    val buffer=decoder.getInputBuffer(index)!!;buffer.clear()
                    val pts=extractor.sampleTime;val size=extractor.readSampleData(buffer,0)
                    if(inputs<12)trace.append("in ").append(inputs).append(" pts=").append(pts).append(" bytes=").append(size).append('\n')
                    decoder.queueInputBuffer(index,0,maxOf(size,0),maxOf(pts,0),if(size<0)MediaCodec.BUFFER_FLAG_END_OF_STREAM else 0)
                    inputs++;extractor.advance()
                }
                val out=decoder.dequeueOutputBuffer(info,1000)
                if(out==MediaCodec.INFO_OUTPUT_FORMAT_CHANGED)trace.append("output=").append(decoder.outputFormat).append('\n')
                else if(out>=0) {
                    trace.append("out ").append(outputs).append(" pts=").append(info.presentationTimeUs).append(" bytes=").append(info.size).append(" offset=").append(info.offset).append(" flags=").append(info.flags).append('\n')
                    outputs++;decoder.releaseOutputBuffer(out,false)
                }
            }
        } finally { try{codec?.stop()}catch(_:Exception){};codec?.release();extractor.release() }
        return trace.toString()
    }
    private fun waitState(sampler: SourceSampler, millis: Long, accept: (JSONObject) -> Boolean): JSONObject {
        val until = SystemClock.elapsedRealtime() + millis
        var state = JSONObject(sampler.stateJson())
        while (!accept(state) && state.optString("status") != "error" && SystemClock.elapsedRealtime() < until) {
            Thread.sleep(50); state = JSONObject(sampler.stateJson())
        }
        check(accept(state)) { "Source operation failed or timed out: $state" }
        return state
    }
    override fun onStart() {
        val result = Bundle(); var success = false; var screen: Activity? = null; var sampler: SourceSampler? = null
        try {
            val root = File(targetContext.filesDir,"SourceSmoke"); root.mkdirs()
            val wav = File(root,"fixture.wav")
            SampleWavWriter(wav,48000,2).use { writer ->
                val buffer = ShortArray(2048)
                repeat(94) { block ->
                    val frames = minOf(1024,96000-block*1024)
                    repeat(frames) { i -> buffer[i*2] = fixture(block*1024+i,0); buffer[i*2+1] = fixture(block*1024+i,1) }
                    writer.writeSamples(buffer,frames*2)
                }
            }
            val wavClip = SegmentRenderer.renderLocal(wav,250,650,root,SourceCancel()); validate(wavClip,19200)
            check(wavClip.frames == 19200L) { "PCM WAV range must contain exactly 19200 frames" }
            RandomAccessFile(wavClip.file,"r").use { file ->
                file.seek(44); val first = (file.read() or (file.read() shl 8)).toShort()
                check(first == fixture(12000,0)) { "WAV IN marker did not cut at the expected PCM frame" }
            }
            val compressed = File(root,"fixture.m4a"); aac(compressed)
            val aacClip = SegmentRenderer.renderLocal(compressed,250,650,root,SourceCancel()); validate(aacClip,19200)
            check(runCatching { SegmentRenderer.renderLocal(wav,0,180001,root,SourceCancel()) }.isFailure)
            check(runCatching { SegmentRenderer.renderLocal(wav,0,100,root,SourceCancel().apply { cancel() }) }.isFailure)
            result.putLong("wavFrames",wavClip.frames); result.putLong("aacFrames",aacClip.frames)
            result.putString("wavClip",wavClip.file.absolutePath); result.putString("aacClip",aacClip.file.absolutePath)
            if (arguments.getString("mp3") == "true") {
                val mp3=File(root,"fixture.mp3");check(mp3.isFile)
                result.putString("mp3Trace",mp3Trace(mp3))
                val mp3Zero=SegmentRenderer.renderLocal(mp3,0,1000,root,SourceCancel())
                val mp3Middle=SegmentRenderer.renderLocal(mp3,250,650,root,SourceCancel())
                val mp3Late=SegmentRenderer.renderLocal(mp3,2000,3000,root,SourceCancel())
                val reference=SegmentRenderer.renderLocal(mp3,0,4000,root,SourceCancel())
                validate(mp3Zero,mp3Zero.rate.toLong());validate(mp3Middle,mp3Zero.rate*400L/1000)
                validate(mp3Late,mp3Zero.rate.toLong());validate(reference,mp3Zero.rate*4L)
                fun matches(clip: RenderedSourceClip, startMs: Long) {
                    val bytes=clip.file.readBytes().copyOfRange(44,clip.file.length().toInt())
                    RandomAccessFile(reference.file,"r").use { input ->
                        input.seek(44+startMs*clip.rate/1000*clip.channels*2)
                        val expected=ByteArray(bytes.size);input.readFully(expected)
                        check(bytes.contentEquals(expected)) { "MP3 $startMs ms slice differs from gapless reference" }
                    }
                }
                matches(mp3Zero,0);matches(mp3Middle,250);matches(mp3Late,2000)
                result.putLong("mp3ZeroFrames",mp3Zero.frames);result.putLong("mp3MiddleFrames",mp3Middle.frames)
                result.putLong("mp3LateFrames",mp3Late.frames);result.putBoolean("mp3ReferenceMatch",true)
                result.putInt("mp3Rate",mp3Zero.rate)
            }
            if (arguments.getString("vorbis") == "true") {
                val ogg = File(root,"fixture.ogg"); check(ogg.isFile) { "Push a public Ogg fixture to SourceSmoke/fixture.ogg first" }
                val probe = MediaExtractor()
                val sourceRate: Int
                try {
                    probe.setDataSource(ogg.absolutePath)
                    val format = probe.getTrackFormat(0)
                    check(format.getString(MediaFormat.KEY_MIME) in listOf("audio/vorbis", "audio/opus"))
                    sourceRate = format.getInteger(MediaFormat.KEY_SAMPLE_RATE)
                    result.putString("oggMime",format.getString(MediaFormat.KEY_MIME))
                } finally { probe.release() }
                val oggClip = SegmentRenderer.renderLocal(ogg,250,650,root,SourceCancel()); validate(oggClip,sourceRate*400L/1000)
                val oggZero = SegmentRenderer.renderLocal(ogg,0,1000,root,SourceCancel()); validate(oggZero,sourceRate.toLong())
                result.putLong("oggFrames",oggClip.frames);result.putLong("oggZeroFrames",oggZero.frames)
                result.putInt("oggRate",sourceRate)
            }
            val discovery=arguments.getString("discovery")=="true"
            if (arguments.getString("network") == "true" || discovery) {
                screen = startActivitySync(Intent(targetContext,MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
                waitForIdleSync(); val live = SourceSampler(screen!!) {}; sampler = live
                val provider = arguments.getString("provider") ?: "archive"
                val query = arguments.getString("query") ?: "jazz"
                live.command(if(discovery) "discover" else "search",JSONObject().put("provider",provider)
                    .put("query",if(discovery) "" else query).put("genre",if(discovery) arguments.getString("genre","jazz") else "").toString())
                var state = waitState(live,60000) { it.optString("status") == "results" }
                check(state.getJSONArray("results").length()>0)
                if(discovery){
                    check(!state.getBoolean("playing")&&state.getString("query").isEmpty())
                    val item=state.getJSONArray("results").getJSONObject(0)
                    check(item.getString("url").startsWith("https://")&&item.has("description")&&item.has("tags")&&item.has("instruments"))
                    result.putBoolean("discoveryWithoutQuery",true)
                }
                result.putInt("searchResults",state.getJSONArray("results").length())
                if(discovery){
                    var found=false
                    for(index in 0 until minOf(6,state.getJSONArray("results").length())){
                        runOnMainSync { live.command("open",JSONObject().put("index",index).toString()) }
                        state=waitState(live,30000){it.optString("status") in listOf("tracks","error")}
                        if(state.optString("status")=="tracks"){found=true;break}
                        check(state.optString("error").contains("No public, directly readable audio tracks")) { state.toString() }
                    }
                    check(found){"Discovery results did not provide a directly readable audio item"}
                }else{live.command("open","{\"index\":0}");state=waitState(live,30000){it.optString("status")=="tracks"}}
                check(state.getJSONArray("tracks").length()>0)
                result.putInt("trackResults",state.getJSONArray("tracks").length())
                if(discovery){live.command("listen","{\"index\":0}");state=waitState(live,30000){it.optBoolean("playing")}}
                else{live.command("track","{\"index\":0}");state=waitState(live,30000){it.optString("status")=="ready"};live.command("play","{}")}
                state = waitState(live,15000) { it.optLong("position_ms") > 0 }
                result.putLong("previewPositionMs",state.getLong("position_ms"))
                if(discovery){
                    val markIn=minOf(1000L,maxOf(0L,state.getLong("duration_ms")/4))
                    val markOut=minOf(4000L,state.getLong("duration_ms"))
                    val seekPoint=minOf(5000L,maxOf(0L,state.getLong("duration_ms")/2))
                    runOnMainSync { live.command("set_region",JSONObject().put("in_ms",markIn).put("out_ms",markOut).toString());live.command("sort","{\"order\":\"title\"}") }
                    state=waitState(live,10000){it.optString("sort_order")=="title"}
                    check(state.getLong("in_ms")==markIn&&state.getLong("out_ms")==markOut&&state.getBoolean("playing"))
                    val currentId=state.getJSONArray("tracks").getJSONObject(state.getInt("track_index")).getString("id")
                    runOnMainSync { live.command("listen",JSONObject().put("index",state.getInt("track_index")).toString()) }
                    state=waitState(live,10000){!it.optBoolean("playing")}
                    runOnMainSync { live.command("seek",JSONObject().put("position_ms",seekPoint).toString());live.command("sort","{\"order\":\"source\"}") }
                    state=waitState(live,10000){it.optString("sort_order")=="source"&&it.optLong("position_ms")>=seekPoint}
                    check(state.getLong("in_ms")==markIn&&state.getLong("out_ms")==markOut)
                    check(state.getJSONArray("tracks").getJSONObject(state.getInt("track_index")).getString("id")==currentId)
                    result.putBoolean("browserSortKeptMarkers",true);result.putBoolean("directListenToggle",true)
                }
                live.command("pause","{}")
                live.command("set_region","{\"in_ms\":0,\"out_ms\":1000}")
                // A queued range command and render are processed in order on the main Looper.
                live.command("render","{}")
                state = waitState(live,60000) { it.optBoolean("clip_ready") }
                val clipFile = File(state.getString("clip_path")); check(clipFile.isFile)
                val sidecar = File(clipFile.absolutePath + ".source.json"); check(sidecar.isFile)
                val credits = JSONObject(sidecar.readText())
                check(credits.getString("source_url") == state.getString("source_url"))
                check(credits.getString("title") == state.getString("title"))
                result.putString("networkProvider",provider); result.putString("networkClip",clipFile.absolutePath)
                result.putLong("networkFrames",state.getLong("clip_frames")); result.putString("sourceUrl",state.getString("source_url"))
                val expectedFrames=state.getInt("clip_rate")*(state.getLong("out_ms")-state.getLong("in_ms"))/1000
                check(kotlin.math.abs(state.getLong("clip_frames")-expectedFrames)<=2) {
                    "Public-source clip has ${state.getLong("clip_frames")} frames, expected $expectedFrames"
                }
                result.putInt("networkRate",state.getInt("clip_rate"));result.putLong("networkExpectedFrames",expectedFrames)
                live.command("seek","{\"position_ms\":0}")
                live.command("record","{\"enabled\":true}")
                state = waitState(live,15000) {
                    it.optBoolean("recording") && it.optLong("position_ms") - it.optLong("in_ms") >= 300 ||
                        it.optBoolean("clip_ready") && it.optString("clip_path") != clipFile.absolutePath
                }
                if (!state.optBoolean("clip_ready")) live.command("record","{\"enabled\":false}")
                state = waitState(live,60000) { it.optBoolean("clip_ready") }
                check(state.getLong("clip_frames") > 0 && !state.getBoolean("recording"))
                result.putLong("recordToggleFrames",state.getLong("clip_frames"))
                val recordExpected=state.getInt("clip_rate")*(state.getLong("out_ms")-state.getLong("in_ms"))/1000
                check(kotlin.math.abs(state.getLong("clip_frames")-recordExpected)<=2) {
                    "Recorded clip has ${state.getLong("clip_frames")} frames, expected $recordExpected"
                }
                result.putLong("recordExpectedFrames",recordExpected)
            }
            success = true
            val liveProof = if (arguments.getString("network") == "true" || discovery)
                "\nPASS: ${result.getString("networkProvider")} search=${result.getInt("searchResults")} tracks=${result.getInt("trackResults")} preview=${result.getLong("previewPositionMs")}ms selected=${result.getLong("networkFrames")}frames record=${result.getLong("recordToggleFrames")}frames; WAV and source sidecar saved\n" else ""
            result.putString("stream","\nPASS: local WAV/AAC region decoding, exact markers, limits/cancellation and requested public-source workflow\n" + liveProof)
        } catch (failure: Throwable) {
            result.putString("error",failure.toString()); result.putString("stream","\nFAIL: $failure\n")
        } finally {
            sampler?.close(); screen?.let { activity -> runOnMainSync { activity.finish() } }
            result.putBoolean("success",success); finish(if(success) Activity.RESULT_OK else Activity.RESULT_CANCELED,result)
        }
    }
}
