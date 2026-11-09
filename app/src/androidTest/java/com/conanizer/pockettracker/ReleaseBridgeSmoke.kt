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
        try {
            check(Build.HARDWARE in listOf("ranchu", "goldfish") || Build.PRODUCT.contains("sdk"))
            val activity = startActivitySync(Intent(targetContext, MainActivity::class.java)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) as MainActivity
            screen = activity
            waitForIdleSync()
            check(activity.describeInputDevices().isNotEmpty())
            check(activity.safRootCount() >= 0)
            val inputs = JSONArray(activity.captureInputsJson())
            check(inputs.length() > 0)
            check(activity.captureStart(0, 1, 48000))
            var state = JSONObject(activity.captureStateJson())
            val until = SystemClock.elapsedRealtime() + 5000
            while (state.optLong("frames") == 0L && SystemClock.elapsedRealtime() < until) {
                Thread.sleep(30)
                state = JSONObject(activity.captureStateJson())
            }
            check(state.optString("state") == "recording" && state.optLong("frames") > 0)
            activity.captureStop()
            val stopUntil = SystemClock.elapsedRealtime() + 3000
            while (state.optString("state") != "ready" && SystemClock.elapsedRealtime() < stopUntil) {
                Thread.sleep(30)
                state = JSONObject(activity.captureStateJson())
            }
            check(state.optString("state") == "ready")
            check(File(state.getString("path")).length() == 44 + state.getLong("frames") * 2)
            result.putLong("captureFrames", state.getLong("frames"))
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
        }
        result.putBoolean("success", success)
        finish(if (success) Activity.RESULT_OK else Activity.RESULT_CANCELED, result)
    }
}
