#pragma once

#include <string_view>

namespace pt::sources {

enum class ShelfGroup { All, JazzSoul, LibraryVoice, WorldFolk, Beats, HistoricLive, Textures };

inline constexpr const char* shelf_groups[] = {
    "All shelves", "Jazz / Soul", "Library / Voice", "World / Folk",
    "Beats / Electronic", "Historic / Live", "Textures / FX"
};

struct Shelf {
    const char* provider;
    const char* title;
    const char* collection;
    const char* keywords;
    const char* description;
    ShelfGroup group;
};

inline constexpr Shelf source_shelves[] = {
    {"archive", "78 RPM records", "78rpm", "", "Early discs, big bands and vocal recordings", ShelfGroup::HistoricLive},
    {"archive", "Great 78 Project", "georgeblood", "", "Preserved shellac sides from the Great 78 Project", ShelfGroup::HistoricLive},
    {"archive", "Netlabels", "netlabels", "", "Independent digital releases from netlabels", ShelfGroup::Beats},
    {"archive", "Live Music Archive", "etree", "", "Concerts and live sessions", ShelfGroup::HistoricLive},
    {"archive", "Old Time Radio", "oldtimeradio", "", "Drama, introductions and radio production", ShelfGroup::LibraryVoice},
    {"archive", "Community Audio", "opensource_audio", "", "Publicly accessible community uploads", ShelfGroup::Textures},
    {"archive", "Music and Arts", "audio_music", "", "Music collections and performances", ShelfGroup::HistoricLive},
    {"archive", "World audio collections", "audio_foreign", "", "Music and recordings from different regions", ShelfGroup::WorldFolk},
    {"archive", "Folksonomy", "folksoundomy", "", "Community audio across styles and formats", ShelfGroup::Textures},
    {"archive", "LibriVox audio", "librivoxaudio", "", "Narration and dramatic readings", ShelfGroup::LibraryVoice},
    {"archive", "Free Music Archive", "freemusicarchive", "", "Independent recordings with item-level license information", ShelfGroup::Beats},
    {"archive", "Hip-Hop Mixtapes", "hiphopmixtapes", "", "Mixtapes, rap and DJ collections", ShelfGroup::Beats},
    {"archive", "Technology audio", "audio_tech", "", "Recordings about technology and electronic media", ShelfGroup::Textures},
    {"archive", "Jazz and swing", "audio_music", "jazz", "Jazz albums, solos and ensemble recordings", ShelfGroup::JazzSoul},
    {"archive", "Soul and R&B", "audio_music", "soul", "Soul performances and rhythm-and-blues recordings", ShelfGroup::JazzSoul},
    {"archive", "Funk and breaks", "opensource_audio", "funk", "Funk recordings and rhythm sections", ShelfGroup::JazzSoul},
    {"archive", "Blues and gospel", "78rpm", "blues", "Blues and related historic recordings", ShelfGroup::JazzSoul},
    {"archive", "Library music", "opensource_audio", "library music", "Production music and archival compilations", ShelfGroup::LibraryVoice},
    {"archive", "Radio drama", "oldtimeradio", "drama", "Spoken performances and scene transitions", ShelfGroup::LibraryVoice},
    {"archive", "Folk and field sessions", "audio_music", "folk", "Acoustic and traditional performances", ShelfGroup::WorldFolk},
    {"archive", "Latin and tropical", "opensource_audio", "salsa", "Latin recordings with percussion and brass", ShelfGroup::WorldFolk},
    {"archive", "Dub and reggae", "opensource_audio", "reggae", "Reggae recordings and dub productions", ShelfGroup::WorldFolk},
    {"archive", "Ambient netlabels", "netlabels", "ambient", "Atmospheres, drones and long textures", ShelfGroup::Beats},
    {"archive", "House and club music", "opensource_audio", "house music", "House, dance and electronic recordings", ShelfGroup::Beats},
    {"archive", "Drum and bass", "opensource_audio", "drum bass", "Jungle and drum-and-bass recordings", ShelfGroup::Beats},
    {"archive", "Field recordings", "opensource_audio", "field recording", "Environmental recordings and outdoor ambience", ShelfGroup::Textures},
    {"archive", "Sound effects", "folksoundomy", "sound effects", "Production effects and recorded objects", ShelfGroup::Textures},
    {"commons", "Jazz recordings", "", "jazz", "Public audio with Wikimedia file and license pages", ShelfGroup::JazzSoul},
    {"commons", "Blues recordings", "", "blues", "Historic blues and instrumental recordings", ShelfGroup::JazzSoul},
    {"commons", "Spoken voice", "", "speech", "Spoken words and documentary audio", ShelfGroup::LibraryVoice},
    {"commons", "Traditional instruments", "", "folk music", "Traditional performances and acoustic instruments", ShelfGroup::WorldFolk},
    {"commons", "Percussion", "", "percussion", "Percussion demonstrations and recordings", ShelfGroup::Beats},
    {"commons", "Historic cylinders", "", "phonograph cylinder", "Early recordings from phonograph cylinders", ShelfGroup::HistoricLive},
    {"commons", "Classical recordings", "", "classical music", "Instrumental and orchestral recordings", ShelfGroup::HistoricLive},
    {"commons", "Field recordings", "", "field recording", "Location sound, weather and ambience", ShelfGroup::Textures},
    {"commons", "Sound effects", "", "sound effect", "Isolated sounds and small effects", ShelfGroup::Textures},
};

inline bool archive_collection_known(std::string_view id) {
    if (id.empty()) return true;
    for (const auto& shelf : source_shelves) {
        if (id == shelf.collection) return true;
    }
    return false;
}

} // namespace pt::sources
