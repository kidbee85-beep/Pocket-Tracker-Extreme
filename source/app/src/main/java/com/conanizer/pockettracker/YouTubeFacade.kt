package com.conanizer.pockettracker

import org.schabi.newpipe.extractor.NewPipe
import org.schabi.newpipe.extractor.ServiceList
import org.schabi.newpipe.extractor.Page
import org.schabi.newpipe.extractor.MediaFormat
import org.schabi.newpipe.extractor.downloader.Downloader
import org.schabi.newpipe.extractor.downloader.Request
import org.schabi.newpipe.extractor.downloader.Response
import org.schabi.newpipe.extractor.localization.Localization
import org.schabi.newpipe.extractor.playlist.PlaylistInfo
import org.schabi.newpipe.extractor.playlist.PlaylistInfoItem
import org.schabi.newpipe.extractor.search.SearchInfo
import org.schabi.newpipe.extractor.stream.ContentAvailability
import org.schabi.newpipe.extractor.stream.DeliveryMethod
import org.schabi.newpipe.extractor.stream.StreamExtractor
import org.schabi.newpipe.extractor.stream.StreamInfoItem
import org.schabi.newpipe.extractor.stream.StreamType
import java.io.ByteArrayOutputStream
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URI
import java.net.URL
import java.util.zip.GZIPInputStream

/** Blocking public metadata operations. Call only from the source sampler's worker executor.
 * No account, user cookies, API key, authentication or DRM handling is provided.
 * Only audio-only progressive URLs are returned; this class never downloads media bytes.
 */
internal object YouTubeFacade {
    private val service get() = ServiceList.YouTube
    private var initialized = false

    private fun initialize() {
        if (!initialized) {
            NewPipe.init(PublicMetadataDownloader(), Localization("en", "GB"))
            initialized = true
        }
    }

    @Synchronized fun search(query: String): List<SourceItem> {
        initialize()
        val text = query.trim().take(180)
        require(text.isNotEmpty()) { "Enter a genre, artist or playlist." }
        val handler = service.searchQHFactory.fromQuery(text, emptyList(), "")
        return SearchInfo.getInfo(service, handler).relatedItems.mapNotNull { item ->
            when (item) {
                is PlaylistInfoItem -> SourceItem(item.url, item.name, item.uploaderName.orEmpty(), "playlist", item.url)
                is StreamInfoItem -> if (item.streamType == StreamType.VIDEO_STREAM || item.streamType == StreamType.AUDIO_STREAM)
                    SourceItem(item.url, item.name, item.uploaderName.orEmpty(), "stream", item.url) else null
                else -> null
            }
        }.distinctBy { it.itemUrl }.take(48)
    }

    @Synchronized fun open(item: SourceItem): List<SourceTrack> {
        initialize()
        checkYouTubeUrl(item.itemUrl)
        if (item.kind != "playlist") return listOf(SourceTrack(item.id, item.title, item.creator, itemUrl = item.itemUrl))
        val info = PlaylistInfo.getInfo(service, item.itemUrl)
        val tracks = info.relatedItems.toMutableList()
        var next = info.nextPage
        // Bounded metadata pagination: large playlists never trigger a background bulk download.
        var pages = 1
        while (Page.isValid(next) && tracks.size < 200 && pages < 4) {
            val page = PlaylistInfo.getMoreItems(service, item.itemUrl, next)
            tracks.addAll(page.items)
            next = page.nextPage
            pages++
        }
        return tracks.filter { it.streamType == StreamType.VIDEO_STREAM || it.streamType == StreamType.AUDIO_STREAM }
            .distinctBy { it.url }.take(200).map {
                SourceTrack(it.url, it.name, it.uploaderName.orEmpty(), itemUrl = it.url,
                    durationMs = (it.duration.coerceAtLeast(0) * 1000).coerceAtMost(86_400_000))
            }
    }

    @Synchronized fun resolve(track: SourceTrack): SourceTrack {
        initialize()
        checkYouTubeUrl(track.itemUrl)
        val extractor = service.getStreamExtractor(track.itemUrl)
        extractor.fetchPage()
        if (extractor.ageLimit > 0 || extractor.privacy != StreamExtractor.Privacy.PUBLIC ||
            extractor.contentAvailability !in listOf(ContentAvailability.AVAILABLE, ContentAvailability.UNKNOWN)) {
            throw IOException("Only unrestricted public YouTube audio is supported.")
        }
        if (extractor.streamType != StreamType.VIDEO_STREAM && extractor.streamType != StreamType.AUDIO_STREAM)
            throw IOException("Live streams and scheduled streams are not supported.")
        // Never inspect video/video-only streams or fetch their manifests. M4A is preferred for
        // Android MediaPlayer/MediaExtractor; WebM Opus is the audio-only fallback.
        val audio = extractor.audioStreams.filter {
            it.isUrl && it.deliveryMethod == DeliveryMethod.PROGRESSIVE_HTTP &&
                it.format in listOf(MediaFormat.M4A, MediaFormat.WEBMA_OPUS, MediaFormat.WEBMA) &&
                isPublicMediaUrl(it.content)
        }.sortedWith(compareByDescending<org.schabi.newpipe.extractor.stream.AudioStream> { it.format == MediaFormat.M4A }
            .thenByDescending { it.averageBitrate }).firstOrNull()
            ?: throw IOException("No supported public audio-only stream is available.")
        return track.copy(title = extractor.name, creator = extractor.uploaderName,
            audioUrl = audio.content, durationMs = (extractor.length.coerceAtLeast(0) * 1000).coerceAtMost(86_400_000))
    }

    private fun checkYouTubeUrl(url: String) {
        val uri = runCatching { URI(url) }.getOrNull()
        require(uri != null && uri.scheme == "https" && uri.userInfo == null &&
            (uri.host == "youtu.be" || uri.host == "youtube.com" || uri.host?.endsWith(".youtube.com") == true)) {
            "Use a public HTTPS YouTube video or playlist URL."
        }
    }

    private fun isPublicMediaUrl(value: String): Boolean {
        val uri = runCatching { URI(value) }.getOrNull() ?: return false
        return uri.scheme == "https" && uri.userInfo == null &&
            (uri.host == "googlevideo.com" || uri.host?.endsWith(".googlevideo.com") == true)
    }

    private class PublicMetadataDownloader : Downloader() {
        override fun execute(request: Request): Response {
            var url = URL(request.url())
            val method = request.httpMethod()
            require(method in listOf("GET", "POST", "HEAD")) { "Unsupported metadata request." }
            repeat(6) { redirect ->
                val host = url.host.lowercase()
                if (url.protocol != "https" || url.userInfo != null ||
                    !(host == "youtube.com" || host.endsWith(".youtube.com") || host == "youtu.be" ||
                        host == "youtubei.googleapis.com"))
                    throw IOException("Unsupported public metadata host.")
                val connection = (url.openConnection() as HttpURLConnection).apply {
                    connectTimeout = 12_000; readTimeout = 18_000; instanceFollowRedirects = false
                    requestMethod = method
                    setRequestProperty("User-Agent", "Mozilla/5.0 (Android 13; Mobile; rv:140.0) Gecko/140.0 Firefox/140.0")
                    setRequestProperty("Accept-Encoding", "identity")
                    request.headers().forEach { (name, values) ->
                        if (name.equals("Authorization", true)) throw IOException("Authenticated requests are not supported.")
                        if (name.equals("Cookie", true) && values.any { cookie ->
                                cookie.split(';').any { part -> part.trim().substringBefore('=') !in listOf("SOCS", "CONSENT") }
                            }) throw IOException("Account cookies are not supported.")
                        setRequestProperty(name, values.joinToString(", "))
                    }
                }
                try {
                    request.dataToSend()?.let { body -> connection.doOutput = true; connection.outputStream.use { it.write(body) } }
                    val status = connection.responseCode
                    if (status in listOf(301, 302, 303, 307, 308)) {
                        val target = connection.getHeaderField("Location") ?: throw IOException("Missing public redirect target.")
                        if (redirect == 5) throw IOException("Too many public metadata redirects.")
                        url = URL(url, target)
                        return@repeat
                    }
                    if (status == 403 || status == 429) throw IOException("YouTube public access returned HTTP $status. Try again later.")
                    val mime = connection.contentType.orEmpty().lowercase()
                    if (mime.startsWith("audio/") || mime.startsWith("video/"))
                        throw IOException("The metadata extractor does not download audio or video.")
                    val body = if (method == "HEAD") "" else {
                        val input = if (status >= 400) connection.errorStream else connection.inputStream
                        input?.let { raw ->
                            val stream = if (connection.contentEncoding.equals("gzip", true)) GZIPInputStream(raw) else raw
                            stream.use {
                                val bytes = ByteArrayOutputStream()
                                val buffer = ByteArray(8192)
                                while (true) {
                                    val n = it.read(buffer); if (n < 0) break
                                    if (bytes.size() + n > 8 * 1024 * 1024) throw IOException("Public metadata response is too large.")
                                    bytes.write(buffer, 0, n)
                                }
                                bytes.toString("UTF-8")
                            }
                        }.orEmpty()
                    }
                    val headers = connection.headerFields.filterKeys { it != null }.mapKeys { it.key!! }
                    return Response(status, connection.responseMessage.orEmpty(), headers, body, url.toString())
                } finally { connection.disconnect() }
            }
            throw IOException("Public metadata redirect failed.")
        }
    }
}
