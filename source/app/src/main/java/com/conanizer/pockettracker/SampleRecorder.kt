package com.conanizer.pockettracker

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.media.AudioDeviceCallback
import android.media.AudioDeviceInfo
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioRecord
import android.media.MediaRecorder
import android.media.audiofx.AcousticEchoCanceler
import android.media.audiofx.AudioEffect
import android.media.audiofx.AutomaticGainControl
import android.media.audiofx.NoiseSuppressor
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicReference
import kotlin.math.abs
import kotlin.math.max

/** Foreground-only input capture; no monitoring path is connected to the audio output. */
internal class SampleRecorder(context: Context) {
    private val app = context.applicationContext
    private val audio = app.getSystemService(Context.AUDIO_SERVICE) as AudioManager
    private val lock = Any()
    private val stopRequested = AtomicBoolean(false)
    private val failureRequested = AtomicReference<String?>(null)
    private var worker: Thread? = null
    private var closed = false
    private data class Status(
        val state: String = "idle", val message: String = "Select an input and press Record",
        val path: String = "", val frames: Long = 0, val rate: Int = 48000, val channels: Int = 1,
        val peakL: Float = 0f, val peakR: Float = 0f, val requestedId: Int = 0,
        val routedId: Int = 0, val routedName: String = ""
    )
    @Volatile private var status = Status()
    @Volatile private var stateJson = encode(status)
    @Volatile private var inputJson = "[]"

    private val devicesChanged = object : AudioDeviceCallback() {
        override fun onAudioDevicesAdded(addedDevices: Array<out AudioDeviceInfo>) = refreshInputs()
        override fun onAudioDevicesRemoved(removedDevices: Array<out AudioDeviceInfo>) {
            refreshInputs()
            val active = status
            if (active.state == "starting" || active.state == "recording") {
                if (removedDevices.any { it.id == active.requestedId || it.id == active.routedId }) {
                    failureRequested.compareAndSet(null, "Input disconnected; partial recording kept")
                    stopRequested.set(true)
                }
            }
        }
    }

    init {
        refreshInputs()
        try { audio.registerAudioDeviceCallback(devicesChanged, Handler(Looper.getMainLooper())) }
        catch (_: Exception) {} // Read-time route checks remain active if callbacks are unavailable.
    }

    private fun encode(s: Status): String = JSONObject().apply {
        put("state", s.state); put("message", s.message); put("path", s.path)
        put("frames", s.frames); put("rate", s.rate); put("channels", s.channels)
        put("peakL", s.peakL.toDouble()); put("peakR", s.peakR.toDouble())
        put("requestedId", s.requestedId); put("routedId", s.routedId); put("routedName", s.routedName)
    }.toString()

    private fun update(change: (Status) -> Status) = synchronized(lock) {
        status = change(status)
        stateJson = encode(status)
    }

    fun stateJson(): String = stateJson
    fun inputsJson(): String = inputJson

    private fun isUsb(device: AudioDeviceInfo): Boolean = device.type == AudioDeviceInfo.TYPE_USB_DEVICE ||
        device.type == AudioDeviceInfo.TYPE_USB_HEADSET || device.type == AudioDeviceInfo.TYPE_USB_ACCESSORY

    private fun refreshInputs() {
        try {
            val entries = JSONArray()
            entries.put(JSONObject().put("id", 0).put("name", "System default").put("kind", "default")
                .put("channels", JSONArray(listOf(1, 2))).put("rates", JSONArray(listOf(44100, 48000))))
            for (device in audio.getDevices(AudioManager.GET_DEVICES_INPUTS)) {
                if (device.type != AudioDeviceInfo.TYPE_BUILTIN_MIC && !isUsb(device)) continue
                entries.put(JSONObject().put("id", device.id).put("name", device.productName.toString())
                    .put("kind", if (isUsb(device)) "usb" else "mic")
                    // Client capture formats can differ from the hardware profile: Android converts.
                    .put("channels", JSONArray(listOf(1, 2))).put("rates", JSONArray(listOf(44100, 48000)))
                    .put("deviceChannels", JSONArray(device.channelCounts.toList()))
                    .put("deviceRates", JSONArray(device.sampleRates.toList())))
            }
            inputJson = entries.toString()
        } catch (_: Exception) {
            inputJson = "[{\"id\":0,\"name\":\"System default\",\"kind\":\"default\",\"channels\":[1,2],\"rates\":[44100,48000]}]"
        }
    }

    fun permissionNeeded() = update {
        if (worker != null) it else Status(state = "permission", message = "Allow microphone access, then press Record again")
    }

    fun permissionResult(granted: Boolean) = update {
        if (worker != null) it else it.copy(state = if (granted) "idle" else "error",
            message = if (granted) "Access allowed; press Record to start" else "Microphone access denied; recording has not started")
    }

    fun unavailable(message: String) = update { if (worker != null) it else Status(state = "error", message = message) }

    fun start(deviceId: Int, channels: Int, rate: Int): Boolean = synchronized(lock) {
        if (closed || worker != null) return false
        if (deviceId < 0 || channels !in 1..2 || rate !in listOf(44100, 48000)) {
            unavailable("Choose mono/stereo and 44.1 or 48 kHz")
            return false
        }
        if (app.checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            permissionNeeded()
            return false
        }
        stopRequested.set(false); failureRequested.set(null)
        status = Status(state = "starting", message = "Opening input and checking its actual route",
            rate = rate, channels = channels, requestedId = deviceId)
        stateJson = encode(status)
        val thread = Thread({ record(deviceId, channels, rate) }, "PocketStudioCapture")
        worker = thread
        thread.start()
        true
    }

    /** Signals the nonblocking reader; the worker alone stops/releases its AudioRecord. */
    fun stop() = synchronized(lock) {
        if (worker != null) {
            stopRequested.set(true)
            update { it.copy(state = "stopping", message = "Finishing WAV recording") }
        }
    }

    fun close() {
        synchronized(lock) { closed = true }
        stop()
        try { audio.unregisterAudioDeviceCallback(devicesChanged) } catch (_: Exception) {}
    }

    private fun disableVoiceEffects(session: Int): List<AudioEffect> {
        val effects = mutableListOf<AudioEffect>()
        fun add(make: () -> AudioEffect?) {
            try { make()?.let { effect -> effects.add(effect); effect.enabled = false } } catch (_: Exception) {}
        }
        add { if (AutomaticGainControl.isAvailable()) AutomaticGainControl.create(session) else null }
        add { if (NoiseSuppressor.isAvailable()) NoiseSuppressor.create(session) else null }
        add { if (AcousticEchoCanceler.isAvailable()) AcousticEchoCanceler.create(session) else null }
        return effects
    }

    private fun record(deviceId: Int, channels: Int, rate: Int) {
        var recorder: AudioRecord? = null
        var writer: SampleWavWriter? = null
        var effects = emptyList<AudioEffect>()
        var failure: String? = null
        var finishMessage = "Recording ready"
        try {
            val device = if (deviceId == 0) null else (
                audio.getDevices(AudioManager.GET_DEVICES_INPUTS)
                    .firstOrNull { it.id == deviceId && (it.type == AudioDeviceInfo.TYPE_BUILTIN_MIC || isUsb(it)) }
                    ?: throw IllegalStateException("Selected input is no longer connected"))
            // The reported device profile is informational. AudioRecord negotiates the client PCM
            // format, including framework resampling/downmix; initialization and actual routing decide.
            val directory = File(app.filesDir, "Recordings")
            if (!directory.isDirectory && !directory.mkdirs()) throw IllegalStateException("Cannot create recording folder")
            if (directory.usableSpace < DISK_RESERVE) throw IllegalStateException("Not enough free recording space")
            val mask = if (channels == 1) AudioFormat.CHANNEL_IN_MONO else AudioFormat.CHANNEL_IN_STEREO
            val minBuffer = AudioRecord.getMinBufferSize(rate, mask, AudioFormat.ENCODING_PCM_16BIT)
            if (minBuffer <= 0) throw IllegalStateException("Audio input format is unavailable")
            val unprocessed = audio.getProperty(AudioManager.PROPERTY_SUPPORT_AUDIO_SOURCE_UNPROCESSED) == "true"
            val source = if (device == null || isUsb(device)) MediaRecorder.AudioSource.DEFAULT
                else if (unprocessed) MediaRecorder.AudioSource.UNPROCESSED
                else MediaRecorder.AudioSource.VOICE_RECOGNITION
            val record = AudioRecord.Builder().setAudioSource(source)
                .setAudioFormat(AudioFormat.Builder().setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                    .setSampleRate(rate).setChannelMask(mask).build())
                .setBufferSizeInBytes(max(minBuffer * 4, 4096 * channels)).build()
            recorder = record
            if (record.state != AudioRecord.STATE_INITIALIZED) throw IllegalStateException("Audio input could not initialize")
            if (device != null && !record.setPreferredDevice(device)) throw IllegalStateException("Input selection was refused")
            if (record.sampleRate != rate || record.channelCount != channels)
                throw IllegalStateException("Audio input changed the requested format")
            effects = disableVoiceEffects(record.audioSessionId)
            if (stopRequested.get()) return
            record.startRecording()
            if (record.recordingState != AudioRecord.RECORDSTATE_RECORDING) throw IllegalStateException("Audio input did not start")
            val samples = ShortArray(1024 * channels)
            val routeDeadline = SystemClock.elapsedRealtime() + 500
            var verifiedRoute = 0
            var candidateRoute = 0
            var routeSince = 0L
            var captureStartedAt = 0L
            var lastFramesAt = SystemClock.elapsedRealtime()
            var samplesWritten = 0L
            var diskCheck = SystemClock.elapsedRealtime()
            while (!stopRequested.get()) {
                val count = record.read(samples, 0, samples.size, AudioRecord.READ_NON_BLOCKING)
                if (count < 0) throw IllegalStateException("Audio input read failed ($count); partial recording kept")
                val actual = record.routedDevice
                if (verifiedRoute == 0) {
                    // Drain startup buffers while the selected route settles; preference is not proof.
                    val now = SystemClock.elapsedRealtime()
                    if (CapturePolicy.routeVerified(deviceId, actual?.id)) {
                        if (candidateRoute != actual!!.id) { candidateRoute = actual.id; routeSince = now }
                        if (now - routeSince < 100 || count == 0) {
                            if (now >= routeDeadline && count == 0)
                                throw IllegalStateException("Audio input delivered no frames")
                            Thread.sleep(4)
                            continue
                        }
                        verifiedRoute = actual!!.id
                        val file = File.createTempFile("capture_${System.currentTimeMillis()}_", ".wav", directory)
                        writer = SampleWavWriter(file, record.sampleRate, record.channelCount)
                        captureStartedAt = now
                        lastFramesAt = now
                        update { it.copy(state = if (stopRequested.get()) "stopping" else "recording",
                            message = "Recording PCM16; no input monitoring", path = file.absolutePath,
                            rate = record.sampleRate, channels = record.channelCount,
                            routedId = actual.id, routedName = actual.productName.toString()) }
                        continue
                    } else {
                        candidateRoute = 0
                        if (SystemClock.elapsedRealtime() >= routeDeadline)
                            throw IllegalStateException("Selected input was not routed; nothing was recorded")
                        Thread.sleep(4)
                        continue
                    }
                } else if (CapturePolicy.routeChanged(verifiedRoute, actual?.id)) {
                    throw IllegalStateException("Input route changed; partial recording kept")
                }
                if (count == 0) {
                    if (SystemClock.elapsedRealtime() - lastFramesAt > 2000)
                        throw IllegalStateException("Input stopped delivering frames; partial recording kept")
                    Thread.sleep(4)
                    continue
                }
                lastFramesAt = SystemClock.elapsedRealtime()
                val output = writer ?: throw IllegalStateException("Recording file is unavailable")
                val remaining = rate.toLong() * MAX_SECONDS * channels - samplesWritten
                val used = minOf(count.toLong(), remaining).toInt()
                if (used > 0) {
                    output.writeSamples(samples, used)
                    var left = 0f; var right = 0f
                    for (i in 0 until used) {
                        val peak = abs(samples[i].toInt()) / 32768f
                        if (channels == 1 || (samplesWritten + i) % channels == 0L) left = max(left, peak)
                        else right = max(right, peak)
                    }
                    samplesWritten += used
                    update { it.copy(frames = output.frames, peakL = left, peakR = if (channels == 1) left else right) }
                }
                if (samplesWritten >= rate.toLong() * MAX_SECONDS * channels ||
                    SystemClock.elapsedRealtime() - captureStartedAt >= MAX_SECONDS * 1000) {
                    finishMessage = "10 minute limit reached; recording kept"
                    break
                }
                if (SystemClock.elapsedRealtime() - diskCheck >= 1000) {
                    diskCheck = SystemClock.elapsedRealtime()
                    if (directory.usableSpace < DISK_RESERVE) {
                        finishMessage = "Low free space; recording stopped and kept"
                        break
                    }
                }
            }
        } catch (e: Exception) {
            failure = e.message ?: "Audio recording failed"
        } finally {
            try { recorder?.stop() } catch (_: Exception) {}
            try { recorder?.release() } catch (_: Exception) {}
            for (effect in effects) try { effect.release() } catch (_: Exception) {}
            try { writer?.close() } catch (e: Exception) { failure = "Cannot finalize WAV: ${e.message}" }
            val finalFailure = failureRequested.get() ?: failure
            synchronized(lock) {
                val frames = writer?.frames ?: 0
                val next = if (finalFailure != null) "error" else if (frames > 0) "ready" else "idle"
                update { it.copy(state = next, frames = frames, peakL = 0f, peakR = 0f,
                    path = if (frames > 0) writer?.file?.absolutePath.orEmpty() else "",
                    message = finalFailure ?: if (frames > 0) finishMessage else "Recording cancelled; no audio captured") }
                worker = null
            }
        }
    }

    companion object {
        private const val MAX_SECONDS = 600L
        private const val DISK_RESERVE = 2L * 1024 * 1024
    }
}
