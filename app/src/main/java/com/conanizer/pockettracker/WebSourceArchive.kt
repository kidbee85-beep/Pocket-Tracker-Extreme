package com.conanizer.pockettracker

import org.json.JSONArray
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URI
import java.net.URL
import java.net.URLEncoder
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.random.Random

internal class SourceCancel {
    val cancelled = AtomicBoolean(false)
    @Volatile var connection: HttpURLConnection? = null
    fun check() { if (cancelled.get() || Thread.currentThread().isInterrupted) throw IOException("Cancelled") }
    fun cancel() { cancelled.set(true); connection?.disconnect() }
}

/** Provider-owned HTTPS endpoints only; redirects receive the same validation. */
internal object SourceNet {
    const val USER_AGENT = "PocketTrackerExtreme/0.1 (private audio source browser)"
    fun allowed(url: String): Boolean = try {
        val uri = URI(url)
        val host = uri.host?.lowercase().orEmpty()
        uri.scheme == "https" && uri.userInfo == null && (uri.port == -1 || uri.port == 443) &&
            listOf("archive.org", "googlevideo.com", "youtube.com", "youtube-nocookie.com", "wikimedia.org").any { host == it || host.endsWith(".$it") }
    } catch (_: Exception) { false }

    fun open(url: String, cancel: SourceCancel, range: String? = null): HttpURLConnection {
        var next = url
        repeat(5) {
            cancel.check(); require(allowed(next)) { "Unsupported source URL" }
            val connection = URL(next).openConnection() as HttpURLConnection
            cancel.connection = connection
            connection.instanceFollowRedirects = false
            connection.connectTimeout = 10000; connection.readTimeout = 10000
            connection.setRequestProperty("User-Agent", USER_AGENT)
            connection.setRequestProperty("Accept-Encoding", "identity")
            if (range != null) connection.setRequestProperty("Range", range)
            val code = connection.responseCode
            if (code in listOf(301, 302, 303, 307, 308)) {
                val location = connection.getHeaderField("Location") ?: throw IOException("Missing redirect")
                next = URI(next).resolve(location).toString(); connection.disconnect()
            } else return connection
        }
        throw IOException("Too many source redirects")
    }

    fun json(url: String, cancel: SourceCancel): JSONObject {
        val connection = open(url, cancel)
        try {
            if (connection.responseCode != 200) throw IOException("Source service returned HTTP ${connection.responseCode}")
            val output = ByteArrayOutputStream()
            connection.inputStream.use { input ->
                val buffer = ByteArray(16384)
                while (true) {
                    cancel.check(); val count = input.read(buffer); if (count < 0) break
                    if (output.size() + count > 4 * 1024 * 1024) throw IOException("Source metadata is too large")
                    output.write(buffer, 0, count)
                }
            }
            return JSONObject(output.toString("UTF-8"))
        } finally { connection.disconnect(); cancel.connection = null }
    }
}

internal object WebSourceArchive {
    private fun encode(value: String) = URLEncoder.encode(value, "UTF-8").replace("+", "%20")
    private fun text(value: Any?): String = when (value) {
        null, JSONObject.NULL -> ""
        is JSONArray -> (0 until value.length()).map { text(value.opt(it)) }.filter { it.isNotEmpty() }.joinToString(", ")
        else -> value.toString().take(500)
    }
    private fun license(metadata: JSONObject): String {
        val value = text(metadata.opt("licenseurl")).substringBefore(", ")
        return if (value.startsWith("http://creativecommons.org/")) value.replaceFirst("http://", "https://")
               else if (value.startsWith("https://")) value else ""
    }
    private fun description(value: Any?): String = text(value).replace(Regex("<[^>]*>"), " ")
        .replace("&amp;", "&").replace("&quot;", "\"").replace("&#39;", "'")
        .replace(Regex("\\s+"), " ").trim().take(800)
    private fun labels(value: Any?): List<String> = when (value) {
        null, JSONObject.NULL -> emptyList()
        is JSONArray -> (0 until value.length()).flatMap { labels(value.opt(it)) }
        else -> value.toString().split(';', '|').map { description(it).take(80) }.filter { it.isNotBlank() }
    }.distinct().take(12)

    fun search(query: String, genre: String, cancel: SourceCancel, discover: Boolean = false): List<SourceItem> {
        val terms = (query.trim() + " " + genre.trim()).trim().take(240).ifEmpty { if (discover) "music" else "" }
        require(terms.isNotEmpty()) { "Enter a style, artist or recording title" }
        val safeTerms = terms.split(Regex("\\s+")).filter { it.isNotBlank() }.joinToString(" AND ") {
            "\"" + it.replace("\\", "\\\\").replace("\"", "\\\"") + "\""
        }
        val formats = if (discover) " AND (format:\"VBR MP3\" OR format:\"MP3\" OR format:\"Ogg Vorbis\" OR format:\"FLAC\" OR format:\"WAVE\")" else ""
        val q = "mediatype:audio AND ($safeTerms) AND -access-restricted-item:true$formats"
        val sort = if (discover) "&sort%5B%5D=" + encode(listOf("downloads desc", "addeddate desc", "identifier asc").random()) else ""
        val base = "https://archive.org/advancedsearch.php?q=${encode(q)}&fl%5B%5D=identifier&fl%5B%5D=title&fl%5B%5D=creator&fl%5B%5D=licenseurl&fl%5B%5D=description&fl%5B%5D=subject&rows=40&output=json$sort"
        var response = SourceNet.json("$base&page=1", cancel).optJSONObject("response") ?: JSONObject()
        if (discover) {
            val pages = ((response.optLong("numFound") + 39) / 40).coerceIn(1, 12).toInt()
            val page = Random.nextInt(1, pages + 1)
            if (page > 1) response = SourceNet.json("$base&page=$page", cancel).optJSONObject("response") ?: JSONObject()
        }
        val docs = response.optJSONArray("docs") ?: JSONArray()
        return (0 until docs.length()).mapNotNull { index ->
            val doc = docs.optJSONObject(index) ?: return@mapNotNull null
            val id = doc.optString("identifier")
            if (!id.matches(Regex("[A-Za-z0-9_.-]{1,250}"))) return@mapNotNull null
            SourceItem(id, text(doc.opt("title")).ifEmpty { id }, text(doc.opt("creator")), "album",
                "https://archive.org/details/${encode(id)}", license(doc), description(doc.opt("description")), labels(doc.opt("subject")))
        }.let { if (discover) it.shuffled() else it }
    }

    fun open(item: SourceItem, cancel: SourceCancel): List<SourceTrack> {
        require(item.id.matches(Regex("[A-Za-z0-9_.-]{1,250}")))
        val data = SourceNet.json("https://archive.org/metadata/${encode(item.id)}", cancel)
        val metadata = data.optJSONObject("metadata") ?: JSONObject()
        if (text(metadata.opt("access-restricted-item")) == "true") throw IOException("This item is access restricted")
        val creator = text(metadata.opt("creator")).ifEmpty { item.creator }
        val license = license(metadata).ifEmpty { item.licenseUrl }
        val about = description(metadata.opt("description")).ifEmpty { item.description }
        val tags = labels(metadata.opt("subject")).ifEmpty { item.tags }
        val instruments = (labels(metadata.opt("instruments")) + labels(metadata.opt("instrumentation"))).distinct()
        val files = data.optJSONArray("files") ?: JSONArray()
        val priority = mapOf("mp3" to 0, "m4a" to 1, "ogg" to 2, "opus" to 3, "flac" to 4, "wav" to 5, "aac" to 6)
        data class Candidate(val key: String, val priority: Int, val track: SourceTrack)
        val candidates = mutableListOf<Candidate>()
        for (index in 0 until minOf(files.length(), 5000)) {
            cancel.check(); val file = files.optJSONObject(index) ?: continue
            val name = file.optString("name")
            val ext = name.substringAfterLast('.', "").lowercase()
            val rank = priority[ext] ?: continue
            if (file.optString("private") == "true" || name.isEmpty() || name.length > 1000 || name.contains('\u0000')) continue
            val key = file.optString("original").ifEmpty { name }.substringBeforeLast('.').lowercase()
            val length = file.optString("length").toDoubleOrNull()?.let { (it * 1000).toLong() } ?: 0L
            val title = text(file.opt("title")).ifEmpty { name.substringAfterLast('/').substringBeforeLast('.') }
            candidates.add(Candidate(key, rank, SourceTrack(item.id + ":" + name, title, creator,
                "https://archive.org/download/${encode(item.id)}/${encode(name)}", item.itemUrl, license, length, about, tags, instruments)))
        }
        return candidates.groupBy { it.key }.values.map { list -> list.minBy { it.priority }.track }.take(150)
            .also { if (it.isEmpty()) throw IOException("No public, directly readable audio tracks in this item") }
    }
}
