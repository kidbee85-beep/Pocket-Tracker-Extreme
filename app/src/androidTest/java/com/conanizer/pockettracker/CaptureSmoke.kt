package com.conanizer.pockettracker

import android.Manifest
import android.app.Activity
import android.app.Instrumentation
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.os.SystemClock
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.nio.ByteOrder

/** Framework-only smoke runner. Run only on an emulator launched with -no-audio. */
class CaptureSmoke : Instrumentation() {
    override fun onCreate(arguments: Bundle?) {
        super.onCreate(arguments)
        start()
    }

    private fun waitState(recorder: SampleRecorder, millis: Long, ready: (JSONObject) -> Boolean): JSONObject {
        val until = SystemClock.elapsedRealtime() + millis
        var value = JSONObject(recorder.stateJson())
        while (!ready(value) && SystemClock.elapsedRealtime() < until) {
            Thread.sleep(25)
            value = JSONObject(recorder.stateJson())
        }
        check(ready(value)) { "Input state timed out: $value" }
        return value
    }

    override fun onStart() {
        val result = Bundle()
        var activity: Activity? = null
        var recorder: SampleRecorder? = null
        var success = false
        try {
            check(Build.HARDWARE in listOf("ranchu", "goldfish") || Build.PRODUCT.contains("sdk")) {
                "Virtual-input smoke is restricted to an emulator"
            }
            check(targetContext.checkSelfPermission(Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED) {
                "Grant RECORD_AUDIO to the emulator target before this smoke test"
            }
            activity = startActivitySync(Intent(targetContext, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
            waitForIdleSync()
            val input = SampleRecorder(targetContext)
            recorder = input
            val inputs = JSONArray(input.inputsJson())
            check((0 until inputs.length()).any { inputs.getJSONObject(it).getInt("id") == 0 })
            result.putString("inputs", inputs.toString())

            // An absent preferred input must become an error, never a substituted microphone take.
            check(input.start(Int.MAX_VALUE, 1, 48000))
            val rejected = waitState(input, 2000) { it.optString("state") == "error" }
            check(rejected.optLong("frames") == 0L && rejected.optString("path").isEmpty())
            result.putString("absentInput", "rejected without captured audio")

            check(input.start(0, 1, 48000))
            val running = waitState(input, 5000) {
                it.optLong("frames") > 0L || it.optString("state") == "error"
            }
            check(running.optString("state") == "recording" && running.optLong("frames") > 0) { running.toString() }
            check(running.optInt("routedId") > 0 && running.optString("routedName").isNotEmpty())
            input.stop()
            val finished = waitState(input, 3000) {
                it.optString("state") in listOf("ready", "error", "idle")
            }
            check(finished.optString("state") == "ready") { finished.toString() }
            val file = File(finished.getString("path"))
            check(file.isFile && file.length() > 44)
            val header = ByteArray(44)
            RandomAccessFile(file, "r").use { it.readFully(header) }
            val wav = ByteBuffer.wrap(header).order(ByteOrder.LITTLE_ENDIAN)
            check(String(header, 0, 4) == "RIFF" && String(header, 8, 8) == "WAVEfmt ")
            check(wav.getInt(4).toLong() == file.length() - 8)
            check(wav.getShort(20).toInt() == 1 && wav.getShort(22).toInt() == 1)
            check(wav.getInt(24) == 48000 && wav.getShort(34).toInt() == 16)
            check(String(header, 36, 4) == "data")
            check(wav.getInt(40).toLong() == file.length() - 44)
            check((file.length() - 44) / 2 == finished.getLong("frames"))
            result.putLong("frames", finished.getLong("frames"))
            result.putLong("wavBytes", file.length())
            result.putString("path", file.absolutePath)
            result.putInt("rate", finished.getInt("rate"))
            result.putInt("channels", finished.getInt("channels"))
            result.putInt("routedId", finished.getInt("routedId"))
            result.putString("routedName", finished.getString("routedName"))
            result.putString("stream", "\nPASS: virtual AudioRecord input, actual route, PCM16 WAV and missing-input rejection\n")
            success = true
        } catch (failure: Throwable) {
            result.putString("error", failure.toString())
            result.putString("stream", "\nFAIL: $failure\n")
        } finally {
            recorder?.stop()
            recorder?.close()
            if (recorder != null) try {
                waitState(recorder, 3000) { it.optString("state") !in listOf("starting", "recording", "stopping") }
            } catch (_: Throwable) {}
            activity?.let { screen -> runOnMainSync { screen.finish() } }
            result.putBoolean("success", success)
            result.putBoolean("virtualEmulatorOnly", true)
            finish(if (success) Activity.RESULT_OK else Activity.RESULT_CANCELED, result)
        }
    }
}
