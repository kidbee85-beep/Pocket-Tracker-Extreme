package com.conanizer.pockettracker

internal data class SourceItem(
    val id: String, val title: String, val creator: String, val kind: String,
    val itemUrl: String, val licenseUrl: String = "", val description: String = "",
    val tags: List<String> = emptyList(), val instruments: List<String> = emptyList()
)

internal data class SourceTrack(
    val id: String, val title: String, val creator: String, val audioUrl: String = "",
    val itemUrl: String, val licenseUrl: String = "", val durationMs: Long = 0,
    val description: String = "", val tags: List<String> = emptyList(), val instruments: List<String> = emptyList(),
    val live: Boolean = false
)
