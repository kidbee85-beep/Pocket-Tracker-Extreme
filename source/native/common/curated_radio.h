#pragma once

#include <string_view>

namespace pt::sources {

struct RadioPreset {
    const char* id;
    const char* title;
    const char* creator;
    const char* genres;
    const char* page;
    const char* url;
    bool playlist;
};

inline bool station_has_genre(std::string_view tags, std::string_view genre) {
    if (genre.empty()) return true;
    while (!tags.empty()) {
        const auto end = tags.find(',');
        if (tags.substr(0, end) == genre) return true;
        if (end == std::string_view::npos) break;
        tags.remove_prefix(end + 1);
    }
    return false;
}

// HTTPS MP3 feeds and the broadcasters' own HTTPS MP3 playlists.
inline constexpr RadioPreset radio_presets[] = {
    {"direct:jazz24", "Jazz24", "KNKX", "jazz",
     "https://www.jazz24.org/streaming-links/", "https://knkx-live-a.edge.audiocdn.com/6285_128k", false},
    {"direct:fipjazz", "FIP Jazz", "Radio France", "jazz",
     "https://embed.radiofrance.fr/fip/player?id_station=65", "https://icecast.radiofrance.fr/fipjazz-midfi.mp3?id=openapi", false},
    {"direct:fipgroove", "FIP Groove", "Radio France", "soul,funk",
     "https://embed.radiofrance.fr/fip/player?id_station=66", "https://icecast.radiofrance.fr/fipgroove-midfi.mp3?id=openapi", false},
    {"direct:fipworld", "FIP Monde", "Radio France", "world",
     "https://embed.radiofrance.fr/fip/player?id_station=69", "https://icecast.radiofrance.fr/fipworld-midfi.mp3?id=openapi", false},
    {"direct:fipreggae", "FIP Reggae", "Radio France", "reggae",
     "https://embed.radiofrance.fr/fip/player?id_station=71", "https://icecast.radiofrance.fr/fipreggae-midfi.mp3?id=openapi", false},
    {"direct:fipelectro", "FIP Electro", "Radio France", "electronic,house",
     "https://embed.radiofrance.fr/fip/player?id_station=74", "https://icecast.radiofrance.fr/fipelectro-midfi.mp3?id=openapi", false},
    {"direct:fiphiphop", "FIP Hip-Hop", "Radio France", "hip hop",
     "https://embed.radiofrance.fr/fip/player?id_station=95", "https://icecast.radiofrance.fr/fiphiphop-midfi.mp3?id=openapi", false},
    {"soma:sonicuniverse", "Sonic Universe", "SomaFM", "jazz",
     "https://somafm.com/sonicuniverse/", "https://somafm.com/sonicuniverse.pls", true},
    {"soma:7soul", "Seven Inch Soul", "SomaFM", "soul,funk",
     "https://somafm.com/7soul/", "https://somafm.com/7soul.pls", true},
    {"soma:illstreet", "Illinois Street Lounge", "SomaFM", "library",
     "https://somafm.com/illstreet/", "https://somafm.com/illstreet.pls", true},
    {"soma:suburbsofgoa", "Suburbs of Goa", "SomaFM", "world,electronic",
     "https://somafm.com/suburbsofgoa/", "https://somafm.com/suburbsofgoa.pls", true},
    {"soma:secretagent", "Secret Agent", "SomaFM", "library,ambient",
     "https://somafm.com/secretagent/", "https://somafm.com/secretagent.pls", true},
    {"soma:bossa", "Bossa Beyond", "SomaFM", "world,jazz",
     "https://somafm.com/bossa/", "https://somafm.com/bossa.pls", true},
    {"soma:groovesalad", "Groove Salad", "SomaFM", "electronic,ambient",
     "https://somafm.com/groovesalad/", "https://somafm.com/groovesalad.pls", true},
    {"soma:beatblender", "Beat Blender", "SomaFM", "electronic,house",
     "https://somafm.com/beatblender/", "https://somafm.com/beatblender.pls", true},
    {"soma:cliqhop", "cliqhop", "SomaFM", "electronic",
     "https://somafm.com/cliqhop/", "https://somafm.com/cliqhop.pls", true},
    {"direct:swissjazz", "Radio Swiss Jazz", "SRG SSR", "jazz",
     "https://www.radioswissjazz.ch/de/empfang/internet", "https://stream.srg-ssr.ch/srgssr/rsj/mp3/128", false},
    {"direct:francemusiquejazz", "France Musique La Jazz", "Radio France", "jazz",
     "https://www.radiofrance.fr/francemusique/radio-la-jazz", "https://icecast.radiofrance.fr/francemusiquelajazz-midfi.mp3", false},
    {"direct:swissclassic", "Radio Swiss Classic", "SRG SSR", "classical",
     "https://www.radioswissclassic.ch/de/empfang/internet", "https://stream.srg-ssr.ch/srgssr/rsc_de/mp3/128", false},
    {"direct:swisspop", "Radio Swiss Pop", "SRG SSR", "pop",
     "https://www.radioswisspop.ch/de/empfang/internet", "https://stream.srg-ssr.ch/srgssr/rsp/mp3/128", false},
    {"direct:fip", "FIP", "Radio France", "jazz,soul,world,pop",
     "https://www.radiofrance.fr/fip", "https://icecast.radiofrance.fr/fip-midfi.mp3?id=openapi", false},
    {"direct:fiprock", "FIP Rock", "Radio France", "rock",
     "https://www.radiofrance.fr/fip/radio-rock", "https://icecast.radiofrance.fr/fiprock-midfi.mp3?id=openapi", false},
    {"direct:fippop", "FIP Pop", "Radio France", "pop",
     "https://www.radiofrance.fr/fip/radio-pop", "https://icecast.radiofrance.fr/fippop-midfi.mp3?id=openapi", false},
    {"direct:fipnouveautes", "FIP Nouveautes", "Radio France", "pop,world,electronic",
     "https://www.radiofrance.fr/fip/radio-nouveautes", "https://icecast.radiofrance.fr/fipnouveautes-midfi.mp3?id=openapi", false},
    {"direct:fipcultes", "FIP Cultes", "Radio France", "rock,pop,soul",
     "https://www.radiofrance.fr/fip", "https://icecast.radiofrance.fr/fipcultes-midfi.mp3?id=openapi", false},
    {"direct:fipmetal", "FIP Metal", "Radio France", "rock",
     "https://www.radiofrance.fr/fip/radio-metal", "https://icecast.radiofrance.fr/fipmetal-midfi.mp3?id=openapi", false},
    {"direct:wfmu", "WFMU Freeform", "WFMU", "rock,soul,library,world,spoken",
     "https://wfmu.org/ssaudionet.shtml", "https://stream0.wfmu.org/freeform-128k.mp3", false},
    {"direct:nts1", "NTS 1", "NTS", "jazz,soul,library,world,electronic,hip hop",
     "https://www.nts.live/", "https://stream-relay-geo.ntslive.net/stream?client=direct", false},
    {"direct:nts2", "NTS 2", "NTS", "funk,hip hop,house,electronic,world,reggae",
     "https://www.nts.live/", "https://stream-relay-geo.ntslive.net/stream2?client=direct", false},
    {"direct:bassdrive", "Bassdrive", "Bassdrive", "drum and bass,electronic",
     "https://www.bassdrive.com/news/welcome-to-bassdrive", "https://bassdrive.radioca.st/stream", false},
    {"soma:folkfwd", "Folk Forward", "SomaFM", "folk",
     "https://somafm.com/folkfwd/", "https://somafm.com/folkfwd.pls", true},
    {"soma:thistle", "ThistleRadio", "SomaFM", "folk,world",
     "https://somafm.com/thistle/", "https://somafm.com/thistle.pls", true},
    {"soma:dubstep", "Dub Step Beyond", "SomaFM", "electronic,drum and bass",
     "https://somafm.com/dubstep/", "https://somafm.com/dubstep.pls", true},
    {"soma:lush", "Lush", "SomaFM", "ambient,electronic",
     "https://somafm.com/lush/", "https://somafm.com/lush.pls", true},
    {"soma:deepspaceone", "Deep Space One", "SomaFM", "ambient,electronic",
     "https://somafm.com/deepspaceone/", "https://somafm.com/deepspaceone.pls", true},
    {"soma:dronezone", "Drone Zone", "SomaFM", "ambient",
     "https://somafm.com/dronezone/", "https://somafm.com/dronezone.pls", true},
    {"soma:spacestation", "Space Station Soma", "SomaFM", "ambient,electronic",
     "https://somafm.com/spacestation/", "https://somafm.com/spacestation.pls", true},
    {"soma:digitalis", "Digitalis", "SomaFM", "electronic,pop",
     "https://somafm.com/digitalis/", "https://somafm.com/digitalis.pls", true},
    {"soma:indiepop", "Indie Pop Rocks", "SomaFM", "rock,pop",
     "https://somafm.com/indiepop/", "https://somafm.com/indiepop.pls", true},
    {"soma:poptron", "PopTron", "SomaFM", "electronic,pop,house",
     "https://somafm.com/poptron/", "https://somafm.com/poptron.pls", true},
    {"soma:defcon", "DEF CON Radio", "SomaFM", "electronic,ambient",
     "https://somafm.com/defcon/", "https://somafm.com/defcon.pls", true},
    {"soma:missioncontrol", "Mission Control", "SomaFM", "ambient,spoken",
     "https://somafm.com/missioncontrol/", "https://somafm.com/missioncontrol.pls", true},
};

} // namespace pt::sources
