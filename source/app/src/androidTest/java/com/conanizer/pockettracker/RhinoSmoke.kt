package com.conanizer.pockettracker

import android.app.Activity
import android.app.Instrumentation
import android.os.Bundle
import org.schabi.newpipe.extractor.utils.JavaScript

/** Exercises the production NewPipe interpreter entry point, including in release builds. */
class RhinoSmoke : Instrumentation() {
    override fun onCreate(arguments: Bundle?) {
        super.onCreate(arguments)
        start()
    }

    override fun onStart() {
        val result = Bundle()
        var success = false
        try {
            val function = "function decipher(s) { return s.split('').reverse().join(''); }"
            JavaScript.compileOrThrow(function)
            check(JavaScript.run(function, "decipher", "abc123") == "321cba")
            check(JavaScript.run(
                "function describe(s) { return JSON.stringify({tag:s, value:Math.pow(3,4)}); }",
                "describe", "Pocket"
            ) == "{\"tag\":\"Pocket\",\"value\":81}")
            check(JavaScript.run(
                "function decode(s) { return decodeURIComponent(s).toUpperCase(); }",
                "decode", "mint%20wave"
            ) == "MINT WAVE")
            var rejected = false
            try { JavaScript.compileOrThrow("function broken( {") } catch (_: Exception) { rejected = true }
            check(rejected)
            result.putString("stream", "\nPASS: NewPipe interpreted JavaScript decipher/JSON/URI and invalid-script rejection\n")
            success = true
        } catch (failure: Throwable) {
            result.putString("error", failure.toString())
            result.putString("stream", "\nFAIL: $failure\n")
        }
        result.putBoolean("success", success)
        result.putBoolean("networkAccessed", false)
        finish(if (success) Activity.RESULT_OK else Activity.RESULT_CANCELED, result)
    }
}
