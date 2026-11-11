package com.conanizer.pockettracker

import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.media.AudioAttributes
import android.media.MediaPlayer
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.widget.EditText
import android.text.InputFilter
import android.view.WindowManager
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.util.concurrent.ArrayBlockingQueue
import java.util.concurrent.ThreadPoolExecutor
import java.util.concurrent.TimeUnit

/** Public-source browsing and range sampling. It never opens a microphone or a screen capture. */
internal class SourceSampler(private val activity: Activity, private val beforePlay: () -> Unit) {
    private val main = Handler(Looper.getMainLooper())
    private val work = ThreadPoolExecutor(2, 2, 0L, TimeUnit.MILLISECONDS, ArrayBlockingQueue(4),
        { task -> Thread(task, "PocketSourceSampler") })
    private val radioWork = ThreadPoolExecutor(1, 1, 0L, TimeUnit.MILLISECONDS, ArrayBlockingQueue(1),
        { task -> Thread(task, "TrackWerkLiveRadio") })
    private val radioAvailable by lazy { runCatching { AndroidRadioCatalog.stations.isNotEmpty() }.getOrDefault(false) }
    private var token = SourceCancel()
    private var generation = 0
    @Volatile private var closed = false
    @Volatile private var cached = "{\"status\":\"idle\",\"clip_ready\":false,\"busy\":false}"
    private var provider = "archive"; private var genre = "Jazz"; private var query = ""; private var collection = ""
    private var status = "idle"; private var message = "Choose a genre or search for music"; private var error = ""
    private var results = emptyList<SourceItem>(); private var tracks = emptyList<SourceTrack>()
    private var sourceResults = emptyList<SourceItem>(); private var sourceTracks = emptyList<SourceTrack>()
    private var sortTitle = false
    private var selected = -1; private var trackIndex = -1; private var activeTrack: SourceTrack? = null
    private var player: MediaPlayer? = null
    private var liveSession: AndroidLiveRadio? = null
    private var liveSeconds = 30
    private val liveHistorySeconds = if (Runtime.getRuntime().maxMemory() >= 192L * 1024 * 1024) 60 else 30
    private var liveIn = -1L
    private var liveOut = -1L
    private var prepared = false; private var playing = false; private var recording = false
    private var autoPlayRequested = false
    private var pendingSeek: Long? = null
    private var duration = 0L; private var position = 0L; private var inMs = 0L; private var outMs = 0L
    private var clip: RenderedSourceClip? = null; private var progress = 0f
    private var clipRequest = 0L
    private var recordRequest = 0L
    private val ticker = object : Runnable {
        override fun run() {
            if (closed || !playing) return
            position = currentPosition()
            if (recording) {
                outMs = minOf(position, inMs + 180000)
                if (position >= inMs + 180000) {
                    recording = false; pause(); render()
                }
            }
            publish()
            if (playing) main.postDelayed(this, 100)
        }
    }

    init { main.post { publish() } }
    fun stateJson(): String = cached

    private fun itemJson(item: SourceItem): JSONObject = JSONObject().put("id", item.id).put("title", item.title)
        .put("creator", item.creator).put("kind", item.kind).put("url", item.itemUrl).put("license", item.licenseUrl)
        .put("description",item.description).put("tags",JSONArray(item.tags)).put("instruments",JSONArray(item.instruments))
    private fun trackJson(track: SourceTrack): JSONObject = JSONObject().put("id",track.id).put("title",track.title)
        .put("creator",track.creator).put("url",track.itemUrl).put("license",track.licenseUrl).put("duration_ms",track.durationMs)
        .put("description",track.description).put("tags",JSONArray(track.tags)).put("instruments",JSONArray(track.instruments))
        .put("kind", if (track.live) "LIVE MP3" else "audio")
    private fun publish() {
        val current = activeTrack
        val item = results.getOrNull(selected)
        val window = liveSession?.window()
        cached = JSONObject().apply {
            put("status", status); put("provider", provider); put("genre", genre); put("query", query); put("collection", collection)
            put("capabilities", JSONObject().put("live_mp3", radioAvailable).put("live_history", radioAvailable).put("preview_gain", false)
                .put("favorites", false).put("take_request_id", true))
            put("busy", status in listOf("searching", "loading", "connecting", "rendering")); put("message", message); put("error", error)
            put("providers", JSONArray().put(JSONObject().put("id", "archive").put("name", "Internet Archive").put("available", true))
                .put(JSONObject().put("id", "commons").put("name", "Wikimedia Commons").put("available", true))
                .put(JSONObject().put("id", "youtube").put("name", "YouTube").put("available", true))
                .put(JSONObject().put("id", "radio_browser").put("name", "Curated radio").put("available", radioAvailable)))
            put("results", JSONArray(results.map { itemJson(it) })); put("selected_index", selected)
            put("tracks", JSONArray(tracks.map { trackJson(it) })); put("sort_order",if(sortTitle) "title" else "source")
            put("track_index", trackIndex); put("title", current?.title ?: item?.title.orEmpty())
            put("creator", current?.creator ?: item?.creator.orEmpty())
            put("source_url", current?.itemUrl ?: item?.itemUrl.orEmpty()); put("license_url", current?.licenseUrl ?: item?.licenseUrl.orEmpty())
            put("description",current?.description ?: item?.description.orEmpty())
            put("tags",JSONArray(current?.tags ?: item?.tags.orEmpty()))
            put("instruments",JSONArray(current?.instruments ?: item?.instruments.orEmpty()))
            put("position_ms", window?.ms(window.heard) ?: position); put("duration_ms", duration)
            put("in_ms", window?.ms(liveIn) ?: inMs); put("out_ms", window?.ms(liveOut) ?: outMs)
            put("live", current?.live == true); put("live_capture_seconds", liveSeconds)
            put("live_history_seconds", liveHistorySeconds)
            put("live_window_start_ms", window?.ms(window.first) ?: 0L)
            put("live_window_end_ms", window?.ms(window.end) ?: 0L)
            put("live_playhead_ms", window?.ms(window.heard) ?: 0L)
            put("live_rewound", window?.rewound == true)
            put("live_in_set", window != null && liveIn in window.first..window.end)
            put("live_out_set", window != null && liveOut in window.first..window.end)
            put("playing", playing); put("recording", recording); put("rendering", status == "rendering")
            put("clip_path", clip?.file?.absolutePath.orEmpty()); put("clip_ready", clip != null)
            put("clip_frames", clip?.frames ?: 0); put("clip_rate", clip?.rate ?: 0); put("clip_channels", clip?.channels ?: 0)
            put("clip_request_id", clipRequest)
            put("progress", progress.toDouble())
        }.toString()
    }
    private fun clearClip() { clip = null; progress = 0f; clipRequest = 0L }
    private fun failure(problem: Throwable) {
        releasePlayer()
        status = "error"; error = problem.message?.take(400) ?: "Source operation failed"; message = error; playing = false; recording = false
        publish()
    }
    private fun releasePlayer() {
        main.removeCallbacks(ticker); playing = false; prepared = false; recording = false; autoPlayRequested = false; pendingSeek = null
        releaseLive()
        val old = player; player = null
        try { old?.release() } catch (_: Exception) {}
    }
    private fun releaseLive() {
        liveSession?.cancel()
        liveSession = null
        liveIn = -1
        liveOut = -1
        radioWork.queue.clear()
    }
    private fun newJob(): Pair<Int, SourceCancel> {
        token.cancel(); work.queue.clear(); token = SourceCancel(); ++generation
        return generation to token
    }
    private fun <T> background(mode: String, operation: (SourceCancel) -> T, done: (T) -> Unit) {
        val (id, cancel) = newJob(); status = mode; error = ""; publish()
        work.execute {
            try {
                cancel.check(); val result = operation(cancel); cancel.check()
                main.post { if (!closed && generation == id) try { done(result); publish() } catch (e: Exception) { failure(e) } }
            } catch (e: Exception) {
                main.post { if (!closed && generation == id && !cancel.cancelled.get()) failure(e) }
            }
        }
    }
    fun command(action: String, argsJson: String): Boolean {
        if (closed || action !in ACTIONS || argsJson.length > 8192) return false
        val args = try { JSONObject(argsJson.ifEmpty { "{}" }) } catch (_: Exception) { return false }
        val task = Runnable { if (!closed) try { dispatch(action, args); publish() } catch (e: Exception) { failure(e) } }
        if (Looper.myLooper() == Looper.getMainLooper()) task.run() else main.post(task)
        return true
    }
    private fun search(args: JSONObject, discover: Boolean = false) {
        val nextProvider = args.optString("provider", provider)
        val radio = nextProvider == "radio_browser"
        require(nextProvider in listOf("archive", "youtube", "commons") || (radio && radioAvailable)) { "Unknown source provider" }
        val selectedCollection = when {
            nextProvider == "archive" -> args.optString("collection").take(60)
            radio && args.optString("collection") == "stations" -> "stations"
            else -> ""
        }
        collection = selectedCollection
        provider = nextProvider; query = if(discover) "" else args.optString("query", query).take(240)
        genre = args.optString("genre", genre).take(80).ifBlank { if (discover && !radio) "music" else "" }
        require(radio || query.isNotBlank() || genre.isNotBlank() || selectedCollection.isNotBlank()) { "Enter a search or select a genre" }
        releasePlayer(); clearClip(); activeTrack = null; results = emptyList(); tracks = emptyList()
        sourceResults=emptyList();sourceTracks=emptyList();if(discover) sortTitle=false
        selected = -1; trackIndex = -1; position = 0; duration = 0; inMs = 0; outMs = 0
        val term = if(discover&&genre.equals("library",true)) "library music" else genre
        val text = (query + " " + term).trim(); val selectedProvider = provider
        message = "Searching " + when (selectedProvider) {
            "archive" -> "Internet Archive"
            "commons" -> "Wikimedia Commons"
            "radio_browser" -> "curated radio"
            else -> "YouTube"
        }
        background("searching", { cancel -> when (selectedProvider) {
            "youtube" -> YouTubeFacade.search(text)
            "commons" -> CommonsSource.search(text)
            "radio_browser" -> AndroidRadioCatalog.search(query, genre, discover)
            else -> WebSourceArchive.search(query, term, cancel, discover, selectedCollection)
        }.let { if (discover && selectedProvider in listOf("youtube", "commons")) it.shuffled() else it } }) { found ->
            sourceResults=found.take(60);results=sourceResults;reorder(sortTitle)
            status = "results"; message = if (radio) "${results.size} curated MP3 stations; open a result to listen" else
                if (results.isEmpty()) "No matching public audio found" else
                if(discover) "${results.size} random ${genre} finds; Right enters the list, A opens a result"
                else "${results.size} results; open an item to list its tracks"
        }
    }
    private fun reorder(byTitle: Boolean) {
        val selectedId=results.getOrNull(selected)?.id;val trackId=tracks.getOrNull(trackIndex)?.id
        sortTitle=byTitle
        results=if(byTitle) sourceResults.sortedWith(compareBy(String.CASE_INSENSITIVE_ORDER){it.title}) else sourceResults
        tracks=if(byTitle) sourceTracks.sortedWith(compareBy(String.CASE_INSENSITIVE_ORDER){it.title}) else sourceTracks
        selected=if(selectedId==null) -1 else results.indexOfFirst { it.id==selectedId }
        trackIndex=if(trackId==null) -1 else tracks.indexOfFirst { it.id==trackId }
    }
    private fun open(index: Int) {
        val item = results.getOrNull(index) ?: throw IllegalArgumentException("Choose a search result")
        releasePlayer(); clearClip(); selected = index; trackIndex = -1; tracks = emptyList(); activeTrack = null
        sourceTracks=emptyList()
        position = 0; duration = 0; inMs = 0; outMs = 0; message = "Reading tracks for ${item.title}"
        val selectedProvider = provider
        background("loading", { cancel -> when (selectedProvider) {
            "youtube" -> YouTubeFacade.open(item)
            "commons" -> CommonsSource.open(item.id)
            "radio_browser" -> listOf(AndroidRadioCatalog.stations.first { it.id == item.id }.track())
            else -> WebSourceArchive.open(item, cancel)
        } }) { found -> sourceTracks=found.take(150);tracks=sourceTracks;reorder(sortTitle)
            status = "tracks"; message = "${tracks.size} tracks; Right enters the list, Up / Down browses" }
    }
    private fun chooseTrack(index: Int, autoPlay: Boolean = false) {
        val track = tracks.getOrNull(index) ?: throw IllegalArgumentException("Choose a track")
        releasePlayer(); clearClip(); activeTrack = null; trackIndex = index; autoPlayRequested = autoPlay
        position = 0; duration = 0; inMs = 0; outMs = 0; message = "Preparing audio preview"
        if (track.live) {
            newJob()
            activeTrack = track
            status = "ready"
            error = ""
            message = "LIVE MP3 station. Play or Record to connect."
            if (autoPlay) startLive()
            return
        }
        val selectedProvider = provider
        background("loading", { _ -> when (selectedProvider) {
            "youtube" -> YouTubeFacade.resolve(track)
            "commons" -> CommonsSource.resolve(track)
            else -> track
        } }) { resolved ->
            require(SourceNet.allowed(resolved.audioUrl)) { "No supported public HTTPS audio stream" }
            activeTrack = resolved; prepare(resolved)
        }
    }
    private fun prepare(track: SourceTrack) {
        val media = MediaPlayer(); player = media
        media.setAudioAttributes(AudioAttributes.Builder().setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
            .setUsage(AudioAttributes.USAGE_MEDIA).build())
        media.setVolume(1f, 1f)
        media.setOnPreparedListener {
            if (player !== media || closed) return@setOnPreparedListener
            duration = media.duration.toLong().coerceAtLeast(0)
            if (duration <= 0) { releasePlayer(); failure(IllegalStateException("Live or unbounded streams cannot be range sampled")); return@setOnPreparedListener }
            prepared = true; position = 0; inMs = 0; outMs = minOf(duration, 30000)
            status = "ready"; message = "Audio ready; Play, mark IN / OUT, then Render"
            val start = autoPlayRequested; autoPlayRequested = false
            if (start) play()
            publish()
        }
        media.setOnCompletionListener {
            if (player !== media || closed) return@setOnCompletionListener
            position = duration; playing = false
            val sample = recording
            if (recording) { outMs = minOf(duration, inMs + 180000); recording = false }
            status = if (clip != null) "clip_ready" else "ready"; message = "Preview ended"
            if (sample && outMs > inMs) render()
            publish()
        }
        media.setOnErrorListener { _, what, extra ->
            if (player === media && !closed) { releasePlayer(); failure(IllegalStateException("Audio preview unavailable ($what/$extra); select the track again")) }
            true
        }
        media.setOnSeekCompleteListener {
            if (player === media && !closed) { pendingSeek = null; position = currentPosition(); publish() }
        }
        media.setDataSource(activity, Uri.parse(track.audioUrl), mapOf("User-Agent" to SourceNet.USER_AGENT))
        media.prepareAsync()
    }
    private fun currentPosition(): Long = pendingSeek ?: try { if (prepared) player?.currentPosition?.toLong() ?: position else position } catch (_: Exception) { position }
    private fun startLive() {
        if (liveSession != null) return
        val track = checkNotNull(activeTrack)
        val station = AndroidRadioCatalog.stations.first { it.id == track.id }
        beforePlay()
        status = "connecting"; error = ""; message = "Connecting to LIVE MP3"
        position = 0; prepared = false; playing = false
        liveIn = -1
        liveOut = -1
        lateinit var session: AndroidLiveRadio
        session = AndroidLiveRadio(station, File(activity.filesDir, "SourceSamples"), RadioEvents(
            ready = {
                main.post {
                    if (!closed && liveSession === session) {
                        prepared = true; playing = true
                        if (!recording && status != "rendering") {
                            status = "live"; message = "LIVE MP3. Rewind the buffer, save previous audio, or record now."
                        }
                        publish()
                    }
                }
            },
            progress = { at, ratio ->
                main.post {
                    if (!closed && liveSession === session) {
                        position = at
                        if (recording) progress = ratio
                        publish()
                    }
                }
            },
            complete = { take, detail ->
                main.post {
                    if (!closed && liveSession === session) {
                        clip = take?.clip; clipRequest = take?.requestId ?: 0L
                        recording = false; progress = if (take == null) 0f else 1f
                        status = if (take == null) "error" else "clip_ready"
                        error = if (take == null) detail else ""
                        message = detail
                        publish()
                    } else if (take != null) {
                        take.clip.file.delete()
                        File(take.clip.file.path + ".source.json").delete()
                    }
                }
            },
            ended = { detail ->
                main.post {
                    if (!closed && liveSession === session) {
                        liveSession = null; playing = false; recording = false; prepared = false
                        status = if (clip == null) "error" else "clip_ready"
                        error = if (clip == null) detail else ""
                        message = detail
                        publish()
                    }
                }
            }
        ), liveHistorySeconds)
        liveSession = session
        radioWork.execute { session.run() }
    }
    private fun radioMessage(detail: String, failure: Boolean = false) {
        status = if (playing) "live" else "paused"
        error = if (failure) detail else ""
        message = detail
    }

    private fun exportRadioHistory(session: AndroidLiveRadio, from: Long, until: Long, requestId: Long) {
        require(requestId in 0..0xffffffffL) { "Invalid radio take request" }
        clearClip()
        val (id, cancel) = newJob()
        status = "rendering"
        error = ""
        message = "Saving buffered radio audio to WAV"
        work.execute {
            try {
                val output = session.writeHistory(from, until, cancel)
                main.post {
                    if (!closed && generation == id && liveSession === session) {
                        clip = output
                        clipRequest = requestId
                        progress = 1f
                        status = "clip_ready"
                        message = "Radio rewind WAV ready for the Sample Editor"
                        publish()
                    } else {
                        output.file.delete()
                        File(output.file.path + ".source.json").delete()
                    }
                }
            } catch (problem: Exception) {
                main.post {
                    if (!closed && generation == id && liveSession === session) {
                        radioMessage(problem.message ?: "Cannot save buffered radio audio", true)
                        publish()
                    }
                }
            }
        }
    }

    private fun liveHistoryAction(action: String, args: JSONObject) {
        try {
            check(!recording && status != "rendering") { "Finish the current radio take first" }
            val session = checkNotNull(liveSession) { "Listen for a moment to fill the radio buffer" }
            val window = checkNotNull(session.window()) { "Listen for a moment to fill the radio buffer" }
            check(window.end > window.first) { "Listen for a moment to fill the radio buffer" }
            when (action) {
                "seek", "live_edge" -> {
                    val command = if (action == "live_edge") RadioPlaybackCommand.Edge else {
                        val delta = if (args.has("delta_ms")) args.optLong("delta_ms") else
                            args.optLong("position_ms", window.ms(window.heard)) - window.ms(window.heard)
                        RadioPlaybackCommand.Seek(delta.coerceIn(-60000, 60000))
                    }
                    check(session.command(command)) { "Radio playback is changing, try again" }
                    playing = true
                    radioMessage(if (action == "live_edge") "At the live edge" else "Listening to the radio buffer")
                }
                "mark_in" -> {
                    liveIn = window.heard
                    if (liveOut <= liveIn) liveOut = -1
                    clearClip()
                    radioMessage("Radio IN marker updated")
                }
                "mark_out" -> {
                    require(liveIn >= window.first && window.heard > liveIn) { "Set IN and move playback past it to mark OUT" }
                    liveOut = window.heard
                    clearClip()
                    radioMessage("Radio OUT marker updated")
                }
                "set_region" -> {
                    val fromMs = args.optLong("in_ms", window.ms(liveIn))
                    val untilMs = args.optLong("out_ms", window.ms(liveOut))
                    require(fromMs in 0..window.ms(window.end) && untilMs in 0..window.ms(window.end)) {
                        "Choose an IN/OUT range inside the buffered audio"
                    }
                    val from = if (liveIn >= 0 && fromMs == window.ms(liveIn)) liveIn else window.frame(fromMs)
                    val until = if (liveOut >= 0 && untilMs == window.ms(liveOut)) liveOut else window.frame(untilMs)
                    require(window.contains(from, until)) { "Choose an IN/OUT range inside the buffered audio" }
                    liveIn = from
                    liveOut = until
                    clearClip()
                    radioMessage("Radio markers updated")
                }
                "live_history_take", "render" -> {
                    val seconds = args.optInt("seconds")
                    val saveLast = action == "live_history_take"
                    require(!saveLast || seconds in liveRadioTakeLengths) { "Choose a valid radio take length" }
                    val until = if (saveLast) window.heard else liveOut
                    val from = if (saveLast) maxOf(window.first, until - minOf(seconds, session.historySeconds) * window.rate) else liveIn
                    require(window.contains(from, until)) { "Select a range that is still in the radio buffer" }
                    exportRadioHistory(session, from, until, args.optLong("request_id", 0))
                }
            }
        } catch (problem: IllegalArgumentException) {
            radioMessage(problem.message ?: "Choose a valid radio range", true)
        } catch (problem: IllegalStateException) {
            if (recording || status == "rendering") {
                message = problem.message ?: "Finish the current radio take"
            } else radioMessage(problem.message ?: "Wait for radio audio", true)
        }
    }

    private fun recordLive(args: JSONObject) {
        val enabled = if (args.has("enabled")) args.optBoolean("enabled") else !recording
        if (enabled) {
            if (liveSession?.window()?.rewound == true) {
                radioMessage("Return to LIVE before recording from now")
                return
            }
            startLive()
            if (liveSession?.startTake(liveSeconds, args.optLong("request_id", 0)) != true) {
                message = "A live take is already running or the station has ended"
                return
            }
            clearClip()
            recording = true; status = "recording"; message = "Recording up to ${liveSeconds}s of live audio"
        } else {
            if (!recording) return
            liveSession?.finishTake()
            recording = false; status = "rendering"; message = "Finishing the live WAV"
        }
    }
    private fun play() {
        if (activeTrack?.live == true) {
            val session = liveSession
            if (session == null) startLive()
            else if (session.command(RadioPlaybackCommand.Resume)) {
                playing = true
                radioMessage("Radio playback resumed")
            }
            return
        }
        if (!prepared || player == null) {
            check(trackIndex in tracks.indices) { "Select an audio track first" }
            chooseTrack(trackIndex, true); return
        }
        check(prepared && player != null) { "Select a prepared audio track first" }
        beforePlay(); player!!.start(); playing = true; status = "playing"; message = if (recording) "Marking source range; press Record again to end" else "Audio preview"
        main.removeCallbacks(ticker); main.post(ticker)
    }
    private fun pause() {
        if (activeTrack?.live == true) {
            if (recording || status == "rendering") {
                message = "Finish the live take with Record, or cancel it with Stop"
                return
            }
            if (liveSession?.command(RadioPlaybackCommand.Pause) == true) {
                playing = false
                radioMessage("Radio playback paused. The buffer is still running.")
            }
            return
        }
        position = currentPosition(); if (playing) player?.pause(); playing = false; main.removeCallbacks(ticker)
        status = if (clip != null) "clip_ready" else "ready"
    }
    private fun range(start: Long, end: Long) {
        check(prepared && activeTrack?.live != true) { "Mark IN / OUT on a finite audio track" }
        clearClip(); inMs = start.coerceIn(0, maxOf(0, duration - 1)); outMs = end.coerceIn(inMs + 1, minOf(duration, inMs + 180000))
        message = "Selected ${(outMs - inMs) / 1000.0} seconds"
    }
    private fun render(requestId: Long = recordRequest) {
        val track = activeTrack ?: throw IllegalStateException("Select an audio track first")
        check(!track.live) { "Use Record to capture a timed live take" }
        if (recording) { recording = false; outMs = minOf(currentPosition(), inMs + 180000) }
        pause(); check(outMs > inMs && outMs - inMs <= 180000) { "Mark a region of up to 180 seconds" }
        clearClip(); val from = inMs; val until = outMs; val selectedProvider = provider
        val (id, cancel) = newJob(); status = "rendering"; error = ""; message = "Rendering only the marked audio range"; publish()
        work.execute {
            var rendered: RenderedSourceClip? = null
            var sidecar: File? = null
            try {
                var lastProgress = 0L
                rendered = SegmentRenderer.render(track, from, until, File(activity.filesDir, "SourceSamples"), cancel) { value ->
                    val now = android.os.SystemClock.elapsedRealtime()
                    if (now - lastProgress > 100 || value >= 1f) {
                        lastProgress = now; main.post { if (generation == id && !closed) { progress = value; publish() } }
                    }
                }
                cancel.check(); val output = rendered
                sidecar = File(output.file.absolutePath + ".source.json")
                val credits = JSONObject().put("version", 1).put("provider", selectedProvider).put("title", track.title)
                    .put("creator", track.creator).put("source_url", track.itemUrl).put("license_url", track.licenseUrl)
                    .put("in_ms", from).put("out_ms", until).put("frames", output.frames).put("rate", output.rate)
                    .put("channels", output.channels).put("retrieved_at_ms", System.currentTimeMillis())
                sidecar.writeText(credits.toString(2)); cancel.check()
                main.post {
                    if (generation == id && !closed) {
                        clip = output; clipRequest = requestId; progress = 1f; status = "clip_ready"
                        message = "Clip rendered; Import loads it into the selected instrument"; publish()
                    } else { output.file.delete(); sidecar.delete() }
                }
            } catch (e: Exception) {
                rendered?.file?.delete(); sidecar?.delete()
                main.post { if (generation == id && !closed && !cancel.cancelled.get()) failure(e) }
            }
        }
    }
    private fun dispatch(action: String, args: JSONObject) {
        if (activeTrack?.live == true && action in HISTORY_ACTIONS) {
            liveHistoryAction(action, args)
            return
        }
        when (action) {
            "search" -> search(args)
            "discover" -> search(args,true)
            "sort" -> { reorder(args.optString("order",if(sortTitle) "source" else "title")=="title")
                message=if(sortTitle) "Browser ordered by title A-Z; current audio and markers kept" else "Original source order; current audio and markers kept" }
            "query_dialog" -> {
                val edit = EditText(activity); edit.setSingleLine(true); edit.filters = arrayOf(InputFilter.LengthFilter(240))
                edit.setText(args.optString("query", query))
                val dialog = AlertDialog.Builder(activity).setTitle("Search audio sources").setView(edit)
                    .setNegativeButton("Cancel", null).setPositiveButton("Search") { _, _ ->
                        try { search(JSONObject().put("query", edit.text.toString()).put("provider", args.optString("provider", provider))
                            .put("genre", args.optString("genre", genre))); publish() } catch (e: Exception) { failure(e) }
                    }.create()
                dialog.setOnShowListener { edit.requestFocus(); dialog.window?.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE) }
                dialog.show()
            }
            "open" -> open(args.optInt("index", selected))
            "track" -> chooseTrack(args.optInt("index", trackIndex))
            "listen" -> {
                val index=args.optInt("index",trackIndex)
                if(index==trackIndex&&prepared){if(playing)pause() else play()}
                else chooseTrack(index,true)
            }
            "play" -> play()
            "pause" -> {
                if (recording && activeTrack?.live != true) { outMs = minOf(currentPosition(), inMs + 180000); recording = false }
                pause()
            }
            "seek" -> {
                check(prepared && activeTrack?.live != true) { "Select finite audio before seeking" }; recording = false
                position = args.optLong("position_ms", position).coerceIn(0, duration)
                pendingSeek = position
                player?.seekTo(position, MediaPlayer.SEEK_CLOSEST)
            }
            "mark_in" -> range(args.optLong("position_ms", currentPosition()), outMs)
            "mark_out" -> { val end = args.optLong("position_ms", currentPosition()); check(end > inMs) { "OUT must be after IN" }; range(inMs, end) }
            "set_region" -> range(args.optLong("in_ms", inMs), args.optLong("out_ms", outMs))
            "record" -> {
                if (activeTrack?.live == true) {
                    recordLive(args)
                    return
                }
                check(prepared) { "Select audio before marking a range" }; clearClip()
                val enabled = if (args.has("enabled")) args.optBoolean("enabled") else !recording
                recordRequest = args.optLong("request_id", recordRequest)
                if (enabled) { inMs = currentPosition().coerceAtMost(maxOf(0, duration - 1)); outMs = inMs; recording = true; if (!playing) play() }
                else { outMs = minOf(currentPosition(), inMs + 180000); recording = false; pause(); render(recordRequest) }
            }
            "render" -> render(args.optLong("request_id", 0))
            "live_length" -> {
                val seconds = args.optInt("seconds", 30)
                require(seconds in liveRadioTakeLengths) { "Choose a live take of 10, 20, 30, 40, 45 or 60 seconds" }
                if (recording || status == "rendering") {
                    message = "Finish the current live take before changing its length"
                } else {
                    liveSeconds = seconds
                    message = "Live take length ${seconds}s"
                }
            }
            "info" -> {
                val item = results.getOrNull(selected)
                val urls = (listOf(activeTrack?.itemUrl.orEmpty(), activeTrack?.licenseUrl.orEmpty(), item?.itemUrl.orEmpty(), item?.licenseUrl.orEmpty()) +
                    results.flatMap { listOf(it.itemUrl, it.licenseUrl) } + tracks.flatMap { listOf(it.itemUrl, it.licenseUrl) }).filter { it.startsWith("https://") }
                val url = args.optString("url").ifEmpty { urls.firstOrNull().orEmpty() }
                require(url in urls && Uri.parse(url).host != null) { "No source information URL is available" }
                activity.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(url)))
            }
            "cancel", "stop" -> {
                newJob(); if (recording) outMs = minOf(currentPosition(), inMs + 180000)
                position = currentPosition(); releasePlayer(); status = if (clip != null) "clip_ready" else if (tracks.isNotEmpty()) "tracks" else if (results.isNotEmpty()) "results" else "idle"
                message = "Source playback stopped"; error = ""
            }
        }
    }
    fun close() {
        closed = true; token.cancel(); work.shutdownNow(); radioWork.shutdownNow()
        if (Looper.myLooper() == Looper.getMainLooper()) releasePlayer() else main.post { releasePlayer() }
    }
    companion object {
        private val HISTORY_ACTIONS = setOf("seek", "live_edge", "mark_in", "mark_out", "set_region", "render", "live_history_take")
        private val ACTIONS = HISTORY_ACTIONS + setOf("search", "discover", "sort", "listen", "query_dialog",
            "open", "track", "play", "pause", "record", "live_length", "cancel", "stop", "info")
    }
}
