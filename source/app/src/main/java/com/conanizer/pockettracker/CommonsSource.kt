package com.conanizer.pockettracker

import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder

/** Public Commons audio and its per-file attribution; no account or API key. */
internal object CommonsSource {
    private fun encode(value: String) = URLEncoder.encode(value, "UTF-8")
    private fun clean(value: String): String = value.replace(Regex("<[^>]*>"), " ")
        .replace("&amp;", "&").replace("&quot;", "\"").replace("&#39;", "'")
        .replace(Regex("\\s+"), " ").trim().take(500)

    private fun fetch(params: String): JSONObject {
        val connection = URL("https://commons.wikimedia.org/w/api.php?format=json&formatversion=2&$params")
            .openConnection() as HttpURLConnection
        connection.connectTimeout = 15_000; connection.readTimeout = 20_000
        connection.setRequestProperty("User-Agent", "TrackWerk/0.9 private audio research")
        try {
            check(connection.responseCode == 200) { "Commons returned HTTP ${connection.responseCode}" }
            val output = ByteArrayOutputStream()
            connection.inputStream.use { input ->
                val buffer = ByteArray(8192)
                while (true) {
                    val count = input.read(buffer)
                    if (count < 0) break
                    check(output.size() + count <= 4 * 1024 * 1024) { "Commons response is too large" }
                    output.write(buffer, 0, count)
                }
            }
            val json = JSONObject(output.toString("UTF-8"))
            check(!json.has("error")) { "Commons search is unavailable; try a different query" }
            return json
        } finally { connection.disconnect() }
    }

    private fun details(page: JSONObject): SourceTrack? {
        val info = page.optJSONArray("imageinfo")?.optJSONObject(0) ?: return null
        val mime = info.optString("mime").lowercase()
        val supported = setOf("audio/mpeg", "audio/mp4", "audio/ogg", "audio/opus", "audio/webm",
            "audio/wav", "audio/x-wav", "audio/flac", "audio/x-flac", "audio/aac", "application/ogg")
        if (mime !in supported || info.optString("mediatype") != "AUDIO") return null
        val url = info.optString("url")
        val parsed = runCatching { URL(url) }.getOrNull() ?: return null
        if (parsed.protocol != "https" || parsed.host != "upload.wikimedia.org") return null
        val metadata = info.optJSONObject("extmetadata") ?: JSONObject()
        fun field(key: String) = clean(metadata.optJSONObject(key)?.optString("value") ?: "")
        val fileTitle = page.optString("title")
        val pageUrl = info.optString("descriptionurl").ifBlank {
            "https://commons.wikimedia.org/wiki/" + encode(fileTitle).replace("+", "_")
        }
        val license = metadata.optJSONObject("LicenseUrl")?.optString("value").orEmpty()
            .takeIf { it.startsWith("https://") } ?: pageUrl
        val title = field("ObjectName").ifBlank { fileTitle.removePrefix("File:") }
        val tags = field("Categories").split('|').filter { it.isNotBlank() }.take(12)
        val instruments = field("Instrumentation").split('|', ';').filter { it.isNotBlank() }.take(12)
        return SourceTrack(fileTitle, title, field("Artist").ifBlank { "Wikimedia Commons" }, url,
            pageUrl, license, description = field("ImageDescription"), tags = tags, instruments = instruments)
    }

    fun search(query: String): List<SourceItem> {
        val json = fetch("action=query&generator=search&gsrnamespace=6&gsrlimit=20&gsrsearch=" +
            encode(query.take(160) + " filetype:audio") + "&prop=imageinfo&iiprop=url%7Cmime%7Cmediatype%7Cextmetadata")
        val pages = json.optJSONObject("query")?.optJSONArray("pages") ?: return emptyList()
        val items = ArrayList<SourceItem>()
        for (index in 0 until pages.length()) {
            val track = details(pages.getJSONObject(index)) ?: continue
            items.add(SourceItem(track.id, track.title, track.creator, "audio", track.itemUrl, track.licenseUrl,
                track.description, track.tags, track.instruments))
        }
        return items
    }

    fun open(id: String): List<SourceTrack> {
        val json = fetch("action=query&titles=" + encode(id.take(500)) + "&prop=imageinfo&iiprop=url%7Cmime%7Cmediatype%7Cextmetadata")
        val page = json.optJSONObject("query")?.optJSONArray("pages")?.optJSONObject(0)
            ?: error("Commons audio is no longer available")
        return listOf(details(page) ?: error("This item does not provide a supported public audio file"))
    }

    fun resolve(track: SourceTrack): SourceTrack = track
}
