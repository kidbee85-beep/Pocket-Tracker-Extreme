package com.conanizer.pockettracker

import android.app.Activity
import android.app.Instrumentation
import android.content.Intent
import android.os.Build
import android.os.Bundle
import android.os.SystemClock
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.nio.file.Files
import java.nio.charset.StandardCharsets
import java.util.regex.Pattern

/** Calls the same retained activity methods used by native JNI in the minified APK. */
class ReleaseBridgeSmoke : Instrumentation() {
    private var args = Bundle()
    override fun onCreate(arguments: Bundle?) {
        super.onCreate(arguments)
        args = arguments ?: Bundle()
        start()
    }
    override fun onStart() {
        val result = Bundle()
        var screen: MainActivity? = null
        var success = false
        val marker = File(targetContext.filesDir, "audio-slow-open")
        var savedMarker: ByteArray? = null
        var forcedLegacy = false
        try {
            check(Build.HARDWARE in listOf("ranchu", "goldfish") || Pattern.compile("sdk").matcher(Build.PRODUCT).find())
            forcedLegacy = args.getString("forceLegacyOutput") == "true"
            if (forcedLegacy) {
                savedMarker = if (marker.exists()) Files.readAllBytes(marker.toPath()) else null
                Files.write(marker.toPath(), byteArrayOf())
            }
            val nativeLog = File(targetContext.filesDir, "pockettracker-log.txt")
            nativeLog.delete() // This runner is explicitly limited to the task's emulator.
            val activity = startActivitySync(Intent(targetContext, MainActivity::class.java)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) as MainActivity
            screen = activity
            waitForIdleSync()
            check(activity.describeInputDevices().isNotEmpty())
            check(activity.safRootCount() >= 0)
            val inputs = JSONArray(activity.captureInputsJson())
            check(inputs.length() > 0)
            fun nativeReady() {
                val deadline = SystemClock.elapsedRealtime() + 20000
                while (!nativeLog.exists() || !Pattern.compile("first SDL_RenderPresent returned").matcher(String(Files.readAllBytes(nativeLog.toPath()), StandardCharsets.UTF_8)).find()) {
                    check(SystemClock.elapsedRealtime() < deadline) { "Native startup did not finish" }
                    Thread.sleep(50)
                }
            }
            fun capture(): Long {
                check(activity.captureStart(0, 1, 48000))
                var state = JSONObject(activity.captureStateJson())
                val until = SystemClock.elapsedRealtime() + 5000
                while (state.optLong("frames") == 0L && SystemClock.elapsedRealtime() < until) {
                    check(state.optString("state") != "error") { state.toString() }
                    Thread.sleep(30)
                    state = JSONObject(activity.captureStateJson())
                }
                check(state.optString("state") == "recording" && state.optLong("frames") > 0)
                Thread.sleep(120)
                activity.captureStop()
                val stopUntil = SystemClock.elapsedRealtime() + 3000
                while (state.optString("state") != "ready" && SystemClock.elapsedRealtime() < stopUntil) {
                    Thread.sleep(30)
                    state = JSONObject(activity.captureStateJson())
                }
                check(state.optString("state") == "ready") { state.toString() }
                check(File(state.getString("path")).length() == 44 + state.getLong("frames") * 2)
                return state.getLong("frames")
            }
            if (args.getString("waitForNativeReady") == "true") nativeReady()
            var totalFrames = capture() // Default deliberately starts during native startup.
            nativeReady()
            // Exercise the actual retained JNI capture-policy flag on the running output backend.
            // OS screen-recording permission itself is outside this focused regression.
            val setCaptured = activity.javaClass.getDeclaredMethod("nativeSetPlaybackCaptured", Boolean::class.javaPrimitiveType)
            setCaptured.isAccessible = true
            repeat(3) {
                setCaptured.invoke(activity, true)
                Thread.sleep(650)
                totalFrames += capture()
                setCaptured.invoke(activity, false)
                Thread.sleep(650)
            }
            // An input vanishes: expose a real error, then permit a successful retry.
            check(activity.captureStart(Int.MAX_VALUE, 1, 48000))
            var failure = JSONObject(activity.captureStateJson())
            val failureUntil = SystemClock.elapsedRealtime() + 3000
            while (failure.optString("state") != "error" && SystemClock.elapsedRealtime() < failureUntil) {
                Thread.sleep(30)
                failure = JSONObject(activity.captureStateJson())
            }
            check(failure.optString("state") == "error") { failure.toString() }
            check(Pattern.compile("no longer connected").matcher(failure.optString("message")).find()) { failure.toString() }
            totalFrames += capture()
            result.putLong("captureFrames", totalFrames)
            result.putInt("successfulCaptureCycles", 5)
            result.putInt("playbackCapturePolicyCycles", 3)
            result.putBoolean("inputFailureRecovery", true)
            result.putBoolean("forcedLegacyOutput", forcedLegacy)
            result.putBoolean("captureDuringStartup", args.getString("waitForNativeReady") != "true")
            result.putBoolean("virtualEmulatorOnly", true)
            runOnMainSync { check(activity.sourceCommand("pause", "{}")) }
            val sources = JSONObject(activity.sourceStateJson())
            check(sources.has("status") && sources.has("providers"))
            check(sources.getJSONArray("providers").length() == 3)
            if (args.getString("network") == "true") {
                runOnMainSync {
                    check(activity.sourceCommand("search", "{\"provider\":\"commons\",\"query\":\"jazz\"}"))
                }
                var online = JSONObject(activity.sourceStateJson())
                val netUntil = SystemClock.elapsedRealtime() + 45000
                while (online.optString("status") != "results" && SystemClock.elapsedRealtime() < netUntil) {
                    check(online.optString("status") != "error") { online.optString("error") }
                    Thread.sleep(100)
                    online = JSONObject(activity.sourceStateJson())
                }
                check(online.optString("status") == "results")
                check(online.getJSONArray("results").length() > 0)
                result.putInt("commonsSearchResults", online.getJSONArray("results").length())
            }
            success = true
            result.putString("stream", "\nPASS: minified release native-callback contract, virtual input WAV, source bridge and storage roots\n")
        } catch (failure: Throwable) {
            result.putString("error", failure.toString())
            result.putString("stream", "\nFAIL: $failure\n")
        } finally {
            screen?.captureStop()
            screen?.let { activity -> runOnMainSync { activity.finish() } }
            if (screen != null) {
                val deadline = SystemClock.elapsedRealtime() + 10000
                while (!screen.isDestroyed && SystemClock.elapsedRealtime() < deadline) Thread.sleep(50)
            }
            if (forcedLegacy) {
                savedMarker?.let { Files.write(marker.toPath(), it) } ?: marker.delete()
            }
        }
        result.putBoolean("success", success)
        finish(if (success) Activity.RESULT_OK else Activity.RESULT_CANCELED, result)
    }
}
