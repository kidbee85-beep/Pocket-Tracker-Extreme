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
    private var token = SourceCancel()
    private var generation = 0
    @Volatile private var closed = false
    @Volatile private var cached = "{\"status\":\"idle\",\"clip_ready\":false,\"busy\":false}"
    private var provider = "archive"; private var genre = "Jazz"; private var query = ""
    private var status = "idle"; private var message = "Choose a genre or search for music"; private var error = ""
    private var results = emptyList<SourceItem>(); private var tracks = emptyList<SourceTrack>()
    private var sourceResults = emptyList<SourceItem>(); private var sourceTracks = emptyList<SourceTrack>()
    private var sortTitle = false
    private var selected = -1; private var trackIndex = -1; private var activeTrack: SourceTrack? = null
    private var player: MediaPlayer? = null
    private var prepared = false; private var playing = false; private var recording = false
    private var autoPlayRequested = false
    private var pendingSeek: Long? = null
    private var duration = 0L; private var position = 0L; private var inMs = 0L; private var outMs = 0L
    private var clip: RenderedSourceClip? = null; private var progress = 0f
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
    private fun publish() {
        val current = activeTrack
        val item = results.getOrNull(selected)
        cached = JSONObject().apply {
            put("status", status); put("provider", provider); put("genre", genre); put("query", query)
            put("busy", status in listOf("searching", "loading", "rendering")); put("message", message); put("error", error)
            put("providers", JSONArray().put(JSONObject().put("id", "archive").put("name", "Internet Archive").put("available", true))
                .put(JSONObject().put("id", "commons").put("name", "Wikimedia Commons").put("available", true))
                .put(JSONObject().put("id", "youtube").put("name", "YouTube").put("available", true)))
            put("results", JSONArray(results.map { itemJson(it) })); put("selected_index", selected)
            put("tracks", JSONArray(tracks.map { trackJson(it) })); put("sort_order",if(sortTitle) "title" else "source")
            put("track_index", trackIndex); put("title", current?.title ?: item?.title.orEmpty())
            put("creator", current?.creator ?: item?.creator.orEmpty())
            put("source_url", current?.itemUrl ?: item?.itemUrl.orEmpty()); put("license_url", current?.licenseUrl ?: item?.licenseUrl.orEmpty())
            put("description",current?.description ?: item?.description.orEmpty())
            put("tags",JSONArray(current?.tags ?: item?.tags.orEmpty()))
            put("instruments",JSONArray(current?.instruments ?: item?.instruments.orEmpty()))
            put("position_ms", position); put("duration_ms", duration); put("in_ms", inMs); put("out_ms", outMs)
            put("playing", playing); put("recording", recording); put("rendering", status == "rendering")
            put("clip_path", clip?.file?.absolutePath.orEmpty()); put("clip_ready", clip != null)
            put("clip_frames", clip?.frames ?: 0); put("clip_rate", clip?.rate ?: 0); put("clip_channels", clip?.channels ?: 0)
            put("progress", progress.toDouble())
        }.toString()
    }
    private fun clearClip() { clip = null; progress = 0f }
    private fun failure(problem: Throwable) {
        releasePlayer()
        status = "error"; error = problem.message?.take(400) ?: "Source operation failed"; message = error; playing = false; recording = false
        publish()
    }
    private fun releasePlayer() {
        main.removeCallbacks(ticker); playing = false; prepared = false; recording = false; autoPlayRequested = false; pendingSeek = null
        val old = player; player = null
        try { old?.release() } catch (_: Exception) {}
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
        require(nextProvider in listOf("archive", "youtube", "commons")) { "Unknown source provider" }
        provider = nextProvider; query = if(discover) "" else args.optString("query", query).take(240)
        genre = args.optString("genre", genre).take(80).ifBlank { if(discover) "music" else "" }
        require(query.isNotBlank() || genre.isNotBlank()) { "Enter a search or select a genre" }
        releasePlayer(); clearClip(); activeTrack = null; results = emptyList(); tracks = emptyList()
        sourceResults=emptyList();sourceTracks=emptyList();if(discover) sortTitle=false
        selected = -1; trackIndex = -1; position = 0; duration = 0; inMs = 0; outMs = 0
        val term = if(discover&&genre.equals("library",true)) "library music" else genre
        val text = (query + " " + term).trim(); val selectedProvider = provider
        message = "Searching ${if (provider == "archive") "Internet Archive" else if (provider == "commons") "Wikimedia Commons" else "YouTube"}"
        background("searching", { cancel -> when (selectedProvider) {
            "youtube" -> YouTubeFacade.search(text)
            "commons" -> CommonsSource.search(text)
            else -> WebSourceArchive.search(query, term, cancel, discover)
        }.let { if(discover&&selectedProvider!="archive") it.shuffled() else it } }) { found ->
            sourceResults=found.take(60);results=sourceResults;reorder(sortTitle)
            status = "results"; message = if (results.isEmpty()) "No matching public audio found" else
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
            else -> WebSourceArchive.open(item, cancel)
        } }) { found -> sourceTracks=found.take(150);tracks=sourceTracks;reorder(sortTitle)
            status = "tracks"; message = "${tracks.size} tracks; Right enters the list, Up / Down browses" }
    }
    private fun chooseTrack(index: Int, autoPlay: Boolean = false) {
        val track = tracks.getOrNull(index) ?: throw IllegalArgumentException("Choose a track")
        releasePlayer(); clearClip(); activeTrack = null; trackIndex = index; autoPlayRequested = autoPlay
        position = 0; duration = 0; inMs = 0; outMs = 0; message = "Preparing audio preview"
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
    private fun play() {
        if (!prepared || player == null) {
            check(trackIndex in tracks.indices) { "Select an audio track first" }
            chooseTrack(trackIndex, true); return
        }
        check(prepared && player != null) { "Select a prepared audio track first" }
        beforePlay(); player!!.start(); playing = true; status = "playing"; message = if (recording) "Marking source range; press Record again to end" else "Audio preview"
        main.removeCallbacks(ticker); main.post(ticker)
    }
    private fun pause() {
        position = currentPosition(); if (playing) player?.pause(); playing = false; main.removeCallbacks(ticker)
        status = if (clip != null) "clip_ready" else "ready"
    }
    private fun range(start: Long, end: Long) {
        check(prepared) { "Select a prepared audio track first" }
        clearClip(); inMs = start.coerceIn(0, maxOf(0, duration - 1)); outMs = end.coerceIn(inMs + 1, minOf(duration, inMs + 180000))
        message = "Selected ${(outMs - inMs) / 1000.0} seconds"
    }
    private fun render() {
        val track = activeTrack ?: throw IllegalStateException("Select an audio track first")
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
                        clip = output; progress = 1f; status = "clip_ready"; message = "Clip rendered; Import loads it into the selected instrument"; publish()
                    } else { output.file.delete(); sidecar.delete() }
                }
            } catch (e: Exception) {
                rendered?.file?.delete(); sidecar?.delete()
                main.post { if (generation == id && !closed && !cancel.cancelled.get()) failure(e) }
            }
        }
    }
    private fun dispatch(action: String, args: JSONObject) {
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
            "pause" -> { if (recording) { outMs = minOf(currentPosition(), inMs + 180000); recording = false }; pause() }
            "seek" -> {
                check(prepared) { "Select audio before seeking" }; recording = false
                position = args.optLong("position_ms", position).coerceIn(0, duration)
                pendingSeek = position
                player?.seekTo(position, MediaPlayer.SEEK_CLOSEST)
            }
            "mark_in" -> range(args.optLong("position_ms", currentPosition()), outMs)
            "mark_out" -> { val end = args.optLong("position_ms", currentPosition()); check(end > inMs) { "OUT must be after IN" }; range(inMs, end) }
            "set_region" -> range(args.optLong("in_ms", inMs), args.optLong("out_ms", outMs))
            "record" -> {
                check(prepared) { "Select audio before marking a range" }; clearClip()
                val enabled = if (args.has("enabled")) args.optBoolean("enabled") else !recording
                if (enabled) { inMs = currentPosition().coerceAtMost(maxOf(0, duration - 1)); outMs = inMs; recording = true; if (!playing) play() }
                else { outMs = minOf(currentPosition(), inMs + 180000); recording = false; pause(); render() }
            }
            "render" -> render()
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
        closed = true; token.cancel(); work.shutdownNow()
        if (Looper.myLooper() == Looper.getMainLooper()) releasePlayer() else main.post { releasePlayer() }
    }
    companion object {
        private val ACTIONS = setOf("search", "discover", "sort", "listen", "query_dialog", "open", "track", "play", "pause", "seek", "mark_in", "mark_out", "set_region", "record", "render", "cancel", "stop", "info")
    }
}
