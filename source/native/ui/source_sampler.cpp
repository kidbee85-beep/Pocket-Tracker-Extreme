#include "ui/source_sampler.h"
#include "ui/canvas.h"
#include "ui/helpers.h"
#include "ui/studio.h"
#include "common/curated_radio.h"
#include "common/live_take_lengths.h"
#include "common/source_shelves.h"
#include "songcore/host.h"
#include "songcore/project_io.h"
#include "audio-engine.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>

namespace pt::ui {
namespace {
using songcore::json;
constexpr const char* genres[] = {"Jazz", "Soul", "Library", "Funk", "All genres", "World", "Hip-Hop",
    "Electronic", "Reggae", "Folk", "Classical", "Rock", "Pop", "Ambient", "House", "Drum & Bass", "Spoken word"};
constexpr const char* genreIds[] = {"jazz", "soul", "library", "funk", "", "world", "hip hop", "electronic",
    "reggae", "folk", "classical", "rock", "pop", "ambient", "house", "drum and bass", "spoken"};
constexpr int genreCount = static_cast<int>(std::size(genres));
static_assert(std::size(genres) == std::size(genreIds));
constexpr const char* providers[] = {"Internet Archive", "Wikimedia Commons", "YouTube / experimental", "Radio Browser / LIVE", "SomaFM / LIVE"};
constexpr const char* providerIds[] = {"archive", "commons", "youtube", "radio_browser", "somafm"};
constexpr int shelfGroupCount = static_cast<int>(std::size(sources::shelf_groups));
constexpr const char* rowNames[] = {"Provider", "Genre", "Search", "Results", "Tracks", "Transport", "Seek",
                               "IN marker", "OUT marker", "Record region", "Render selection", "Target slot", "Open sample editor", "Source / license", "Preview level", "Live take length", "Favorite station", "Quick clip", "Browse sources"};
constexpr int displayRows[] = {SOURCE_COLLECTION_ROW,0,1,2,3,4,5,14,15,6,7,8,SOURCE_QUICK_CLIP_ROW,9,10,11,12,16,13};
constexpr std::array<int, 5> clipLengths = {10,20,30,40,60};
constexpr uint64_t QUICK_CLIP_ACK_MS = 1800;
static_assert(std::size(rowNames) == SOURCE_SAMPLER_ROWS && std::size(displayRows) == SOURCE_SAMPLER_ROWS);
bool station_collection(const SourceSamplerState& s) {
    return s.liveAvailable && (s.collectionOpen ? s.collectionProvider : s.provider) >= 3;
}
bool live_history(const SourceSamplerState& s) { return s.live && s.liveHistoryAvailable; }
bool history_ready(const SourceSamplerState& s) {
    return live_history(s) && s.liveWindowEndMs > s.liveWindowStartMs;
}
bool has_collection(const SourceSamplerState& s, int provider) {
    return s.backend && provider >= 0 && provider < SOURCE_SAMPLER_PROVIDERS &&
           (provider <= 1 || (provider >= 3 && s.liveAvailable)) && s.providerAvailable[provider];
}
struct ShelfList {
    std::array<const sources::Shelf*, std::size(sources::source_shelves)> entries{};
    int count = 0;
};
ShelfList shelves_for(const SourceSamplerState& s, int group) {
    ShelfList list;
    const auto provider = std::string_view(providerIds[s.collectionProvider]);
    for (const auto& shelf : sources::source_shelves) {
        if (provider == shelf.provider && (group == 0 || group == static_cast<int>(shelf.group)))
            list.entries[list.count++] = &shelf;
    }
    return list;
}
bool visible_station(const SourceSamplerState& s, const sources::RadioPreset& station, int genre) {
    return (s.collectionProvider != 4 || station.playlist) && sources::station_has_genre(station.genres, genreIds[genre]);
}
int station_count(const SourceSamplerState& s, int genre) {
    return static_cast<int>(std::count_if(std::begin(sources::radio_presets), std::end(sources::radio_presets),
        [&](const sources::RadioPreset& station) { return visible_station(s, station, genre); }));
}
std::string collection_name(const std::string& id) {
    if (id == "stations") return "Stations";
    for (const auto& shelf : sources::source_shelves) {
        if (id == shelf.collection && *shelf.keywords == '\0') return shelf.title;
    }
    return {};
}
std::vector<int> visibleRows(const SourceSamplerState& s) {
    std::vector<int> visible;
    const bool radio = s.live || station_collection(s);
    for (int row : displayRows) {
        if ((row == 14 && !s.previewGainAvailable) || (row == 15 && (!s.liveAvailable || !radio)) ||
            (row == 16 && (!s.favoritesAvailable || !radio)) ||
            (row == SOURCE_QUICK_CLIP_ROW && (!s.backend || (s.live && !live_history(s)))) ||
            (row == SOURCE_COLLECTION_ROW && !has_collection(s, s.provider))) continue;
        visible.push_back(row);
    }
    return visible;
}
int displayIndex(const std::vector<int>& rows,int row){for(size_t i=0;i<rows.size();++i)if(rows[i]==row)return int(i);return 0;}
constexpr int64_t MAX_SOURCE_MS = 7LL * 24 * 60 * 60 * 1000;
constexpr const char* EDITOR_TAKE_BLOCKED = "Take saved. Save or discard current sample edits, then Open sample editor.";

std::string string(const json& j, const char* key, const std::string& fallback = {}) {
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : fallback;
}
bool flag(const json& j, const char* key) {
    const auto it = j.find(key); return it != j.end() && it->is_boolean() && it->get<bool>();
}
std::string metadata(const json& j,const char* key) {
    const auto it=j.find(key);if(it==j.end())return {};
    if(it->is_string())return it->get<std::string>().substr(0,800);
    std::string result;
    if (it->is_array()) {
        for (const auto& value : *it) {
            if (!value.is_string()) continue;
            if (!result.empty()) result += " / ";
            result += value.get<std::string>();
            if (result.size() > 300) break;
        }
    }
    return result.substr(0,800);
}
int64_t number(const json& j, const char* key, int64_t maximum = MAX_SOURCE_MS) {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_number()) return 0;
    const double value = it->get<double>();
    return std::isfinite(value) ? static_cast<int64_t>(std::clamp(value, 0., static_cast<double>(maximum))) : 0;
}
int index(const json& j, const char* key) {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_number_integer()) return -1;
    const double value = it->get<double>(); return value >= -1 && value < 10000 ? static_cast<int>(value) : -1;
}
std::vector<SourceSamplerItem> items(const json& j, const char* key) {
    std::vector<SourceSamplerItem> out;
    const auto it = j.find(key);
    if (it == j.end() || !it->is_array()) return out;
    for (const auto& item : *it) {
        if (!item.is_object()) continue;
        if (out.size() >= 512) break;
        out.push_back({string(item,"id"),string(item,"title"),string(item,"creator"),string(item,"kind"),
                       string(item,"url"),string(item,"license"),number(item,"duration_ms"),
                       metadata(item,"description"),metadata(item,"tags"),metadata(item,"instruments"),flag(item,"live")});
    }
    return out;
}
int selected(int value, size_t count) { return count ? std::clamp(value, 0, static_cast<int>(count)-1) : -1; }
int preserve_selection(const std::vector<SourceSamplerItem>& oldItems,int oldCursor,
                       const std::vector<SourceSamplerItem>& newItems,int backendIndex) {
    if(oldCursor>=0&&oldCursor<static_cast<int>(oldItems.size())){
        const auto& old=oldItems[oldCursor];
        for(size_t i=0;i<newItems.size();++i)if(old.id==newItems[i].id&&old.url==newItems[i].url&&
            (!old.id.empty()||!old.url.empty()||old.title==newItems[i].title))return static_cast<int>(i);
    }
    return selected(backendIndex,newItems.size());
}
void invalidate_clip(SourceSamplerState& s) {
    if(!s.clipPath.empty())s.invalidatedClipPath=s.clipPath;
    s.clipReady=false;s.importRequested=false;s.openWhenReady=false;s.importContextBound=false;s.importIsAutomatic=false;
}
uint32_t take_request(SourceSamplerState& s){if(++s.nextTakeRequest==0)++s.nextTakeRequest;return s.nextTakeRequest;}
void arm_editor(SourceSamplerState& s,uint32_t id){invalidate_clip(s);s.openWhenReady=true;s.expectedTakeRequest=id;}
bool send(SourceSamplerState& s, const char* action, const json& args = json::object()) {
    if (!s.backend) { s.status = "Offline build: use the Online app on a WiFi device for Internet sources."; return false; }
    try {
        if (!s.backend->command(action, args.dump(-1,' ',true))) { s.status = "Source command was refused. Check source status."; return false; }
        s.hasPolled = false; return true;
    } catch (...) { s.status = "Source service did not respond. Try again."; return false; }
}
void stop_tracker(songcore::SongcoreHost& host, AudioEngine& engine) {
    host.stop(); host.stop_preview(true); engine.studioRack.punch.store(0);
}
bool idle(SourceSamplerState& s) {
    if(s.recording){s.status="Use Record again to finish and render the region.";return false;}
    if(s.quickClipRequest){s.status="Setting clip markers...";return false;}
    if (!s.busy && !s.rendering) return true;
    s.status = "Source is busy. Use Transport > Stop to cancel first."; return false;
}
bool loaded(SourceSamplerState& s) {
    if ((s.durationMs > 0 || s.live) && !s.title.empty()) return true;
    s.status = "Choose a result, then load a track first."; return false;
}
bool region_valid(const SourceSamplerState& s) {
    if (s.live) return history_ready(s) && s.liveInSet && s.liveOutSet &&
        s.inMs >= s.liveWindowStartMs && s.outMs <= s.liveWindowEndMs && s.outMs > s.inMs;
    return s.durationMs > 0 && s.inMs >= 0 && s.outMs > s.inMs && s.outMs <= s.durationMs && s.outMs-s.inMs <= SOURCE_CLIP_MAX_MS;
}
int quick_clip_index(const SourceSamplerState& s) {
    return std::clamp(s.quickClipLengthIndex, 0, static_cast<int>(clipLengths.size()) - 1);
}
int history_last_length_index(const SourceSamplerState& s) {
    const auto& lengths = sources::live_take_lengths;
    const auto end = std::upper_bound(lengths.begin(), lengths.end(), s.liveHistorySeconds);
    return std::max(0, static_cast<int>(end - lengths.begin()) - 1);
}
int history_take_length(const SourceSamplerState& s) {
    return sources::live_take_lengths[std::clamp(s.historyTakeLengthIndex, 0, history_last_length_index(s))];
}
void history_seek(SourceSamplerState& s, int64_t deltaMs) {
    if (!idle(s)) return;
    if (!history_ready(s)) {
        s.status = "Listen for a moment to fill the rewind buffer.";
        return;
    }
    send(s, "seek", {{"delta_ms", deltaMs}});
}
void set_quick_clip(SourceSamplerState& s) {
    if (!idle(s) || !loaded(s)) return;
    if (s.live) {
        if (!history_ready(s) || s.livePlayheadMs <= s.liveWindowStartMs) {
            s.status = "Listen for a moment before saving previous audio.";
            return;
        }
        const auto id = take_request(s);
        if (send(s, "live_history_take", {{"seconds", history_take_length(s)}, {"request_id", id}})) {
            s.busy = true;
            s.rendering = true;
            arm_editor(s, id);
        }
        return;
    }
    const int64_t first = std::clamp<int64_t>(s.positionMs, 0, s.durationMs);
    const int64_t last = std::min(s.durationMs, first + clipLengths[quick_clip_index(s)] * int64_t{1000});
    if (first >= last) {
        s.status = "Seek back from the end, then set Quick clip.";
        return;
    }
    if (s.inMs == first && s.outMs == last) {
        s.status = "This clip is already marked.";
        return;
    }
    if (send(s, "set_region", {{"in_ms", first}, {"out_ms", last}})) {
        s.quickClipRequest = SourceSamplerState::ClipRequest{first, last, s.lastPollMs};
        s.status = "Setting clip markers...";
    }
}
bool clip_valid(const SourceSamplerState& s) {
    const auto limit=s.live?std::min<int64_t>(60000,SOURCE_CLIP_MAX_MS):SOURCE_CLIP_MAX_MS;
    return !s.clipPath.empty()&&s.clipFrames>0&&s.clipRate>0&&(s.clipChannels==1||s.clipChannels==2)&&
           s.clipFrames<=static_cast<int64_t>(s.clipRate)*limit/1000;
}
std::string time(int64_t value) {
    value=std::max<int64_t>(0,value);char text[32];
    std::snprintf(text,sizeof(text),"%lld:%02lld.%03lld",static_cast<long long>(value/60000),
                  static_cast<long long>(value/1000%60),static_cast<long long>(value%1000));return text;
}
std::string history_offset(const SourceSamplerState& s, int64_t position) {
    return "-" + time(s.liveWindowEndMs - position);
}
std::string slot(int value) {
    if(value<0)return "Current instrument";
    char text[16];std::snprintf(text,sizeof(text),"%02X",value);return text;
}
void source_panel(Canvas& c,int x,int y,int w,int h,const Theme& t,Argb base,Argb edge,int radius=6){
    c.panel_rect(x,y+2,w,h,0x27000000u,0x27000000u,radius);
    c.panel_rect(x,y,w,h,base,edge,radius);
    c.fill_rect(x+8,y+1,w-16,1,with_alpha(t.textValue,.07f));
    c.fill_rect(x+8,y+h-2,w-16,1,0x35000000u);
    if(t.surfaceStyle==7||(base&0x00ffffffu)==0)return;
    Canvas::ClipScope clip(c,x+5,y+3,w-10,h-6);
    const auto light=with_alpha(t.textValue,.022f);
    if(t.surfaceStyle==1||t.surfaceStyle==3){
        for(int py=y+5;py<y+h-4;py+=6)c.fill_rect(x+5,py,w-10,1,light);
    }else if(t.surfaceStyle==2||t.surfaceStyle==6){
        for(int px=x-h;px<x+w;px+=17)c.line(px,y+h-3,px+h,y+3,light);
    }else if(t.surfaceStyle==4||t.surfaceStyle==5){
        for(int px=x+14;px<x+w-6;px+=28)c.fill_rect(px,y+3,1,h-6,light);
        for(int py=y+12;py<y+h-3;py+=24)c.fill_rect(x+5,py,w-10,1,light);
    }else for(int py=y+7;py<y+h-5;py+=13)for(int px=x+9;px<x+w-7;px+=17)
        c.fill_rect(px+(px*13+py*7)%5,py,1,1,light);
}
std::string control_value(const SourceSamplerState& s, int row) {
    switch(row){
        case 0:return source_sampler_provider_name(s.provider);
        case 1:return source_sampler_genre_name(s.genre);
        case 2:if(s.col==2)return "FAVORITE STATIONS";return s.col==1?"RANDOM / "+std::string(source_sampler_genre_name(s.genre)):s.query.empty()?"A: enter query":s.query;
        case 3:return std::to_string(s.results.empty()?0:s.selectedResult+1)+" / "+std::to_string(s.results.size());
        case 4:return std::to_string(s.tracks.empty()?0:s.selectedTrack+1)+" / "+std::to_string(s.tracks.size());
        case 5:return s.col==1?"STOP / CANCEL":s.playing?"PAUSE":"PLAY";
        case 6:return live_history(s) ? (s.liveRewound ? history_offset(s,s.livePlayheadMs) + " / A: LIVE" : "LIVE / Left: rewind")
            : s.live ? "LIVE" : time(s.positionMs);
        case 7:return live_history(s) ? (s.liveInSet ? history_offset(s,s.inMs) : "A: mark from playhead") : s.live ? "Files only" : time(s.inMs);
        case 8:return live_history(s) ? (s.liveOutSet ? history_offset(s,s.outMs) : "A: mark from playhead") : s.live ? "Files only" : time(s.outMs);
        case 9:return s.recording?"FINISH + OPEN EDITOR":s.live?std::to_string(s.liveCaptureSeconds)+" s / A: RECORD":"RECORD IN / OUT";
        case 10:return s.rendering?"RENDERING...":s.live&&!live_history(s)?"Use timed Record":"RENDER + OPEN EDITOR";
        case 11:return slot(s.targetInstrument);
        case 12:return s.clipReady?"OPEN READY WAV":"Render or record a take";
        case 14:return s.previewGainAvailable?std::to_string(s.previewGainPercent)+"% / preview only":"Platform playback level";
        case 15:return std::to_string(s.liveCaptureSeconds)+" s / up to 60 s";
        case 16:return s.favoritesAvailable?"A: save / remove station":"Unavailable on this backend";
        case SOURCE_QUICK_CLIP_ROW:return s.live ? "Up to " + std::to_string(history_take_length(s)) + " s / A: save"
            : std::to_string(clipLengths[quick_clip_index(s)]) + "s / A: from playhead";
        case SOURCE_COLLECTION_ROW:return station_collection(s) ? "Browse stations by genre" : "Browse shelves by genre";
        default:return s.col==1?"LICENSE LINK":"SOURCE PAGE";
    }
}
json search_args(const SourceSamplerState& s) {
    return {{"provider",providerIds[std::clamp(s.provider,0,SOURCE_SAMPLER_PROVIDERS-1)]},
            {"genre",genreIds[std::clamp(s.genre,0,genreCount-1)]},{"query",s.query}};
}

void set_collection_provider(SourceSamplerState& s, int provider) {
    s.collectionProvider = provider;
    s.collectionArmed = false;
    s.stationGenre = std::clamp(s.genre, 0, genreCount - 1);
    s.shelfGroup = std::clamp(s.shelfGroup, 0, shelfGroupCount - 1);
    if (station_collection(s)) {
        if (!station_count(s, s.stationGenre)) s.stationGenre = 0;
    } else {
        if (!shelves_for(s, s.shelfGroup).count) s.shelfGroup = 0;
        s.shelfCursor = std::clamp(s.shelfCursor, 0, std::max(0, shelves_for(s, s.shelfGroup).count - 1));
    }
}

void open_collection(SourceSamplerState& s) {
    if (!has_collection(s, s.provider)) return;
    s.collectionOpen = true;
    set_collection_provider(s, s.provider);
}

void choose_collection(SourceSamplerState& s) {
    if (!has_collection(s, s.collectionProvider) || !idle(s)) return;
    auto args = search_args(s);
    args["provider"] = providerIds[s.collectionProvider];
    const bool radio = station_collection(s);
    std::string title;
    if (radio) {
        if (!station_count(s, s.stationGenre)) return;
        args["query"] = "";
        args["collection"] = "stations";
        args["genre"] = genreIds[s.stationGenre];
        title = std::string(genres[s.stationGenre]) + " stations";
    } else {
        const auto list = shelves_for(s, s.shelfGroup);
        if (s.shelfCursor < 0 || s.shelfCursor >= list.count) return;
        const auto& shelf = *list.entries[s.shelfCursor];
        args["collection"] = shelf.collection;
        args["query"] = shelf.keywords;
        args["genre"] = "";
        title = shelf.title;
    }
    if (!send(s, "search", args)) return;
    s.provider = s.collectionProvider;
    if (radio) s.genre = s.stationGenre;
    s.shelfTitle = std::move(title);
    s.collectionOpen = false;
    invalidate_clip(s);
    s.busy = true;
    s.status = "Opening collection...";
    s.row = 3;
    s.listFocused = true;
    s.pendingTrackList = false;
    s.showingTracks = false;
}

bool collection_button(const ButtonEvent& e, SourceSamplerState& s) {
    if (e.button == Button::B || e.button == Button::START) {
        if (e.action == ButtonAction::RELEASED) {
            s.collectionOpen = false;
            s.collectionArmed = false;
        }
        return true;
    }
    if (e.button == Button::L_SHIFT || e.button == Button::R_SHIFT) {
        s.collectionArmed = false;
        if (e.action == ButtonAction::RELEASED) {
            const int delta = e.button == Button::L_SHIFT ? -1 : 1;
            for (int step = 1; step < SOURCE_SAMPLER_PROVIDERS; ++step) {
                const int provider = (s.collectionProvider + SOURCE_SAMPLER_PROVIDERS + step * delta) % SOURCE_SAMPLER_PROVIDERS;
                if (!has_collection(s, provider)) continue;
                set_collection_provider(s, provider);
                break;
            }
        }
        return true;
    }
    if (e.button == Button::A) {
        if (e.action == ButtonAction::PRESSED) s.collectionArmed = !e.mods.b && !e.mods.l && !e.mods.r && !e.mods.select;
        else {
            const bool armed = s.collectionArmed;
            s.collectionArmed = false;
            if (armed) choose_collection(s);
        }
        return true;
    }
    if (e.action != ButtonAction::PRESSED || !is_dpad(e.button)) return true;
    s.collectionArmed = false;
    if (station_collection(s)) {
        if (e.button != Button::DPAD_UP && e.button != Button::DPAD_DOWN) return true;
        const int delta = e.button == Button::DPAD_UP ? -1 : 1;
        for (int i = 1; i < genreCount; ++i) {
            const int next = (s.stationGenre + genreCount + i * delta) % genreCount;
            if (station_count(s, next)) {
                s.stationGenre = next;
                break;
            }
        }
    } else if (e.button == Button::DPAD_LEFT || e.button == Button::DPAD_RIGHT) {
        const int delta = e.button == Button::DPAD_LEFT ? -1 : 1;
        for (int i = 1; i < shelfGroupCount; ++i) {
            const int next = (s.shelfGroup + shelfGroupCount + i * delta) % shelfGroupCount;
            if (shelves_for(s, next).count) {
                s.shelfGroup = next;
                s.shelfCursor = 0;
                break;
            }
        }
    } else {
        const auto list = shelves_for(s, s.shelfGroup);
        const int delta = e.button == Button::DPAD_UP ? -1 : 1;
        s.shelfCursor = std::clamp(s.shelfCursor + delta, 0, std::max(0, list.count - 1));
    }
    return true;
}

struct TimelineSpan { int64_t first, last; };

TimelineSpan detail_span(const SourceSamplerState& s) {
    const int64_t clipMs = std::max<int64_t>(0, s.outMs - s.inMs);
    const int64_t width = std::min(s.durationMs, std::max<int64_t>(20000, clipMs + clipMs / 2));
    const int64_t focus = clipMs ? s.inMs + clipMs / 2 : s.positionMs;
    const int64_t first = std::clamp<int64_t>(focus - width / 2, 0, s.durationMs - width);
    return {first, first + width};
}

constexpr int RAIL_X = 250, RAIL_W = 360;

int timeline_x(TimelineSpan span, int64_t ms) {
    return RAIL_X + static_cast<int>((std::clamp(ms, span.first, span.last) - span.first) *
                                    (RAIL_W - 1) / (span.last - span.first));
}

void draw_file_rail(Canvas& c, const SourceSamplerState& s, const Theme& t, TimelineSpan span,
                    int y, const TimelineSpan* detail = nullptr) {
    if (span.last <= span.first) return;
    c.fill_rect(RAIL_X, y, RAIL_W, 4, with_alpha(t.textParam, .2f));
    for (int tick = 0; tick < 9; ++tick) {
        const int x = RAIL_X + tick * (RAIL_W - 1) / 8;
        c.fill_rect(x, y + 4, 1, tick % 2 ? 2 : 4, with_alpha(t.textParam, .6f));
    }
    if (detail) {
        const int first = timeline_x(span, detail->first), last = timeline_x(span, detail->last);
        c.fill_rect(first, y + 9, last - first + 1, 1, with_alpha(t.rowCursor, .75f));
    }
    if (s.outMs > s.inMs && s.outMs >= span.first && s.inMs <= span.last) {
        const int in = timeline_x(span, s.inMs), out = timeline_x(span, s.outMs);
        c.fill_rect(in, y - 2, out - in + 1, 8, with_alpha(t.rowCursor, .28f));
    }
    const auto marker = [&](int64_t ms, Argb color) {
        if (ms < span.first || ms > span.last) return;
        const int x = timeline_x(span, ms);
        c.fill_rect(x, y - 4, 1, 11, color);
        c.fill_rect(std::clamp(x - 2, RAIL_X, RAIL_X + RAIL_W - 5), y - 5, 5, 2, color);
    };
    marker(s.inMs, t.rowCursor);
    marker(s.outMs, t.textTitle);
    if (s.positionMs >= span.first && s.positionMs <= span.last)
        c.fill_rect(timeline_x(span, s.positionMs), y - 2, 1, 8, t.textValue);
}

void draw_history_timeline(Canvas& c, const SourceSamplerState& s, const Theme& t) {
    if (!history_ready(s)) {
        c.draw_label("Buffer fills while the station plays.", RAIL_X, 381, t.textParam, 12);
        return;
    }
    const TimelineSpan span{s.liveWindowStartMs, s.liveWindowEndMs};
    const auto textWidth = RAIL_X + RAIL_W;
    constexpr int y = 375;
    c.fill_rect(RAIL_X, y, RAIL_W, 4, with_alpha(t.rowCursor, .34f));
    for (int tick = 0; tick <= 6; ++tick)
        c.fill_rect(RAIL_X + tick * (RAIL_W - 1) / 6, y + 4, 1, 4, with_alpha(t.textParam, .72f));
    if (region_valid(s)) {
        const int left = timeline_x(span, s.inMs), right = timeline_x(span, s.outMs);
        c.fill_rect(left, y - 3, right - left + 1, 10, with_alpha(t.rowCursor, .45f));
    }
    if (s.liveInSet) c.fill_rect(timeline_x(span, s.inMs), y - 6, 2, 15, t.rowCursor);
    if (s.liveOutSet) c.fill_rect(timeline_x(span, s.outMs), y - 6, 2, 15, t.textTitle);
    c.fill_rect(timeline_x(span, s.livePlayheadMs), y - 5, 2, 13, t.textValue);
    c.draw_label(history_offset(s, span.first), RAIL_X, 390, t.textParam, 12);
    c.draw_label("LIVE", textWidth - Canvas::label_width("LIVE", 12), 390, t.rowCursor, 12);
    const std::string detail = region_valid(s) ? "Marked: " + time(s.outMs - s.inMs) + " / Render selection"
        : "Rewind, mark IN / OUT or use Save last";
    c.draw_label(detail, RAIL_X, 419, t.textValue, 12);
}

void draw_capture_timeline(Canvas& c, const SourceSamplerState& s, const Theme& t) {
    const double progress = std::isfinite(s.progress) ? std::clamp(s.progress, 0., 1.) : 0.;
    const std::string renderProgress = "RENDER " + std::to_string(static_cast<int>(progress * 100)) + "%";
    const std::string wav = "WAV " + std::to_string(s.clipRate) + "Hz " + std::to_string(s.clipChannels) + "ch";
    const auto status = s.recording ? "RECORDING TAKE" : live_history(s) ? "RADIO HISTORY"
        : s.live ? "LIVE / TIMED TAKE" : "FILE / CLIP TIMELINE";
    c.draw_label(status, RAIL_X, 347, t.rowCursor, 12);
    const std::string transport = live_history(s) && !s.recording ?
        (s.liveRewound ? history_offset(s, s.livePlayheadMs) + " behind" : "LIVE")
        : s.live ? "LIVE  " + std::to_string(s.liveCaptureSeconds) + "s"
                                         : time(s.positionMs) + " / " + time(s.durationMs);
    c.draw_label(transport, RAIL_X + RAIL_W - Canvas::label_width(transport, 12), 347, t.textValue, 12);
    if (s.live) {
        if (live_history(s) && !s.recording) {
            draw_history_timeline(c, s, t);
            return;
        }
        constexpr int y = 375;
        c.fill_rect(RAIL_X, y, RAIL_W, 4, with_alpha(t.textParam, .2f));
        if (s.recording)
            c.fill_rect(RAIL_X, y - 3, int(RAIL_W * progress), 10, with_alpha(t.meterHigh, .7f));
        else if (s.playing) {
            const int x = int((s.positionMs / 25) % RAIL_W);
            c.fill_rect(RAIL_X + x, y - 3, std::min(20, RAIL_W - x), 10, with_alpha(t.rowCursor, .7f));
        }
        c.draw_label("Record captures from now", RAIL_X, 399, t.textValue, 12);
        c.draw_label(s.clipReady ? wav : s.rendering ? renderProgress : "LIVE MP3 / PCM16 WAV",
                     RAIL_X, 419, t.textParam, 12);
        return;
    }
    if (s.durationMs <= 0) {
        c.draw_label("Play a file to mark and render a clip.", RAIL_X, 380, t.textParam, 12);
        return;
    }
    const TimelineSpan full{0, s.durationMs}, zoom = detail_span(s);
    const bool magnified = zoom.first > 0 || zoom.last < full.last;
    draw_file_rail(c, s, t, full, 368, magnified ? &zoom : nullptr);
    if (magnified) {
        const std::string window = "DETAIL   " + time(zoom.first) + " - " + time(zoom.last);
        c.draw_label(window, RAIL_X, 380, t.textParam, 12);
        draw_file_rail(c, s, t, zoom, 400);
    } else {
        const std::string hint = s.row == SOURCE_QUICK_CLIP_ROW ? "A sets IN at the playhead" : "IN / OUT defines the WAV excerpt";
        c.draw_label(hint, RAIL_X, 389, t.textParam, 12);
    }
    const std::string in = "IN " + time(s.inMs), out = "OUT " + time(s.outMs);
    c.draw_label(in, RAIL_X, 419, t.rowCursor, 12);
    c.draw_label(out, RAIL_X + 110, 419, t.textTitle, 12);
    const std::string detail = s.quickClipRequest ? "SETTING..." : s.clipReady ? wav :
        s.rendering ? renderProgress :
        "LEN " + time(std::max<int64_t>(0, s.outMs - s.inMs));
    c.draw_label(detail, RAIL_X + RAIL_W - Canvas::label_width(detail, 12), 419, t.textValue, 12);
}

void draw_collection_picker(Canvas& c, const SourceSamplerState& s, const Theme& t) {
    const bool radio = station_collection(s);
    const auto accent = t.rowCursor, ink = t.textValue, muted = t.textParam, panel = t.rowEvery4th;
    source_panel(c,18,104,604,354,t,panel,with_alpha(accent,.55f),7);
    c.draw_label(radio ? "RADIO GENRES" : "ARCHIVE SHELVES",31,114,accent,18);
    const std::string provider = "L / R  <  " + std::string(source_sampler_provider_name(s.collectionProvider)) + "  >";
    c.draw_label(provider,609-Canvas::label_width(provider,12),119,muted,12);
    c.fill_rect(31,139,578,1,with_alpha(accent,.4f));
    source_panel(c,28,151,192,255,t,darken(panel,.70f),with_alpha(muted,.25f),5);
    source_panel(c,229,151,383,255,t,darken(panel,.74f),with_alpha(muted,.25f),5);
    const auto row = [&](int x,int y,int width,const std::string& text,bool selected) {
        if (selected) c.panel_rect(x,y-2,width,22,panel,with_alpha(accent,.86f),3);
        c.draw_label(Canvas::clip_label(text,width-18,14),x+8,y,selected ? ink : muted,14);
    };
    if (radio) {
        c.draw_label("GENRE",39,158,accent,12);
        c.draw_label("STATIONS",240,158,accent,12);
        constexpr int visible = 9;
        std::array<int, genreCount> available{};
        int count = 0, current = 0;
        for (int i = 0; i < genreCount; ++i) {
            if (!station_count(s, i)) continue;
            if (i == s.stationGenre) current = count;
            available[count++] = i;
        }
        const int first = std::clamp(current - visible / 2, 0, std::max(0, count - visible));
        for (int i = first; i < std::min(count, first + visible); ++i) {
            const int genre = available[i];
            const auto label = std::string(genres[genre]) + "  " + std::to_string(station_count(s, genre));
            row(35,179+(i-first)*24,178,label,genre==s.stationGenre);
        }
        int written=0;
        for (const auto& station : sources::radio_presets) {
            if (!visible_station(s,station,s.stationGenre)) continue;
            if (written==8) break;
            const int y=179+written++*26;
            c.draw_label(Canvas::clip_label(station.title,218,14),242,y,ink,14);
            c.draw_label(Canvas::clip_label(station.creator,123,12),475,y+2,muted,12);
        }
        const int matching = station_count(s, s.stationGenre);
        const std::string summary = std::to_string(matching) + (matching == 1 ? " station in " : " stations in ") + genres[s.stationGenre];
        c.draw_label(Canvas::clip_label(summary,358,12),242,388,accent,12);
    } else {
        c.draw_label("GROUP",39,158,accent,12);
        c.draw_label("COLLECTION",240,158,accent,12);
        int groupY=180;
        for (int i=0;i<shelfGroupCount;++i) {
            if (!shelves_for(s,i).count) continue;
            row(35,groupY,178,sources::shelf_groups[i],i==s.shelfGroup);
            groupY+=27;
        }
        constexpr int visible = 8;
        const auto list=shelves_for(s,s.shelfGroup);
        const int first=std::clamp(s.shelfCursor-visible/2,0,std::max(0,list.count-visible));
        for (int i=first;i<std::min(list.count,first+visible);++i)
            row(236,179+(i-first)*24,369,list.entries[i]->title,i==s.shelfCursor);
        if (s.shelfCursor>=0 && s.shelfCursor<list.count)
            c.draw_label(Canvas::clip_label(list.entries[s.shelfCursor]->description,356,12),243,385,accent,12);
    }
    c.draw_label(radio ? "Up / Down: genre     A: open station list     B: back"
                       : "Left / Right: group     Up / Down: shelf     A: open     B: back",
                 31,421,ink,12);
    if (s.busy || s.recording || s.rendering)
        c.draw_label(Canvas::clip_label(s.status,574,12),31,442,muted,12);
}
} // namespace

const char* source_sampler_genre_name(int genre) { return genres[std::clamp(genre,0,genreCount-1)]; }
const char* source_sampler_provider_name(int provider) { return providers[std::clamp(provider,0,SOURCE_SAMPLER_PROVIDERS-1)]; }

bool source_sampler_poll(SourceSamplerState& s, uint64_t now) {
    if(s.hasPolled&&now>=s.lastPollMs&&now-s.lastPollMs<100)return false;
    const bool forced=!s.hasPolled;
    s.lastPollMs=now;s.hasPolled=true;
    if(!s.backend)return false;
    const bool clipTimedOut = s.quickClipRequest && now >= s.quickClipRequest->startedMs &&
                              now - s.quickClipRequest->startedMs >= QUICK_CLIP_ACK_MS;
    const auto poll_error = [&](const char* message) {
        if (clipTimedOut) s.quickClipRequest.reset();
        s.status = message;
        return true;
    };
    try{
        const std::string blockedTake=s.status==EDITOR_TAKE_BLOCKED?s.clipPath:std::string{};
        const std::string raw=s.backend->state_json();
        const bool unchanged = raw == s.lastSnapshot;
        if(unchanged&&!forced&&!clipTimedOut)return false;
        if(raw.size()>2*1024*1024)return poll_error("Source response is too large.");
        const auto state=json::parse(raw,nullptr,false);
        if(state.is_discarded()||!state.is_object())return poll_error("Source returned an unreadable status.");
        auto results=items(state,"results"),tracks=items(state,"tracks");
        const int opened=index(state,"selected_index"),playing=index(state,"track_index");
        s.selectedResult=preserve_selection(s.results,s.selectedResult,results,opened);
        s.selectedTrack=preserve_selection(s.tracks,s.selectedTrack,tracks,playing);
        s.results=std::move(results);s.tracks=std::move(tracks);s.openedResult=opened;s.playingTrack=playing;
        if(const auto submitted=string(state,"query");s.shelfTitle.empty()&&!submitted.empty())s.query=submitted;
        s.title=string(state,"title");s.creator=string(state,"creator");
        s.description=metadata(state,"description");s.tags=metadata(state,"tags");s.instruments=metadata(state,"instruments");
        s.sortTitle=string(state,"sort_order",s.sortTitle?"title":"source")=="title";
        s.sourceUrl=string(state,"source_url");s.licenseUrl=string(state,"license_url");
        s.backendStatus=string(state,"status","idle");s.error=string(state,"error");
        s.live=flag(state,"live");s.catalogProvider=string(state,"provider");s.catalogGenre=string(state,"genre");s.catalogCollection=string(state,"collection");
        if(auto cap=state.find("capabilities");cap!=state.end()&&cap->is_object()){s.liveAvailable=flag(*cap,"live_mp3");s.previewGainAvailable=flag(*cap,"preview_gain");s.favoritesAvailable=flag(*cap,"favorites");s.takeRequestIdsAvailable=flag(*cap,"take_request_id");s.liveHistoryAvailable=flag(*cap,"live_history");}
        s.previewGainPercent=static_cast<int>(number(state,"preview_gain_percent",200));if(!s.previewGainPercent)s.previewGainPercent=100;
        const auto liveLength = number(state, "live_capture_seconds");
        if (sources::valid_live_take_length(liveLength)) s.liveCaptureSeconds = static_cast<int>(liveLength);
        s.busy=flag(state,"busy");s.playing=flag(state,"playing");s.recording=flag(state,"recording");s.rendering=flag(state,"rendering");
        if(s.pendingTrackList&&!s.busy){
            s.pendingTrackList=false;
            if(!s.tracks.empty()){s.row=4;s.col=0;s.listFocused=true;s.showingTracks=true;}
        }
        s.durationMs=number(state,"duration_ms");s.positionMs=s.live?number(state,"position_ms"):std::min(number(state,"position_ms"),s.durationMs);
        if (live_history(s)) {
            s.liveWindowEndMs = number(state,"live_window_end_ms");
            s.liveWindowStartMs = std::min(number(state,"live_window_start_ms"),s.liveWindowEndMs);
            s.livePlayheadMs = std::clamp(number(state,"live_playhead_ms"),s.liveWindowStartMs,s.liveWindowEndMs);
            s.liveHistorySeconds = static_cast<int>(number(state,"live_history_seconds",60));
            s.liveRewound = flag(state,"live_rewound");
            s.inMs = number(state,"in_ms");
            s.outMs = number(state,"out_ms");
            s.liveInSet = flag(state,"live_in_set") && s.inMs >= s.liveWindowStartMs && s.inMs <= s.liveWindowEndMs;
            s.liveOutSet = flag(state,"live_out_set") && s.outMs >= s.liveWindowStartMs && s.outMs <= s.liveWindowEndMs;
        } else {
            s.liveWindowStartMs = s.liveWindowEndMs = s.livePlayheadMs = 0;
            s.liveHistorySeconds = 0;
            s.liveRewound = s.liveInSet = s.liveOutSet = false;
            s.inMs = std::min(number(state,"in_ms"),s.durationMs);
            s.outMs = std::min(number(state,"out_ms"),s.durationMs);
        }
        bool clipConfirmed = false, clipRejected = false;
        if (s.quickClipRequest) {
            const auto request = *s.quickClipRequest;
            clipRejected = s.live || s.durationMs <= 0 || (!unchanged && !s.error.empty());
            clipConfirmed = !clipRejected && s.inMs == request.inMs && s.outMs == request.outMs;
            if (clipConfirmed) invalidate_clip(s);
            if (clipConfirmed || clipRejected || clipTimedOut) s.quickClipRequest.reset();
        }
        if (s.live && !live_history(s) && s.row == SOURCE_QUICK_CLIP_ROW) {
            s.row = 5;
            s.col = 0;
            s.quickClipArmed = false;
        }
        s.clipPath=string(state,"clip_path");s.clipFrames=number(state,"clip_frames",48000LL*180*8);
        s.clipRate=static_cast<int>(number(state,"clip_rate",384000));s.clipChannels=static_cast<int>(number(state,"clip_channels",32));
        s.clipReady=flag(state,"clip_ready")&&clip_valid(s)&&s.clipPath!=s.invalidatedClipPath;
        if(!s.clipPath.empty()&&s.clipPath!=s.invalidatedClipPath)s.invalidatedClipPath.clear();
        if(s.importRequested&&(!s.clipReady||s.importPath!=s.clipPath)){s.importRequested=false;s.importPath.clear();}
        if(auto it=state.find("progress");it!=state.end()&&it->is_number()){
            const double v=it->get<double>();s.progress=std::isfinite(v)?std::clamp(v,0.,1.):0.;
        }else s.progress=0;
        if(auto it=state.find("providers");it!=state.end()&&it->is_array()){
            s.providerAvailable.fill(false);
            for(const auto& provider:*it)if(provider.is_object())for(int i=0;i<SOURCE_SAMPLER_PROVIDERS;++i)
                if(string(provider,"id")==providerIds[i])s.providerAvailable[i]=flag(provider,"available");
            s.provider=std::clamp(s.provider,0,SOURCE_SAMPLER_PROVIDERS-1);
            if(!s.providerAvailable[s.provider])for(int i=0;i<SOURCE_SAMPLER_PROVIDERS;++i)if(s.providerAvailable[i]){s.provider=i;break;}
        }
        if (s.row == SOURCE_COLLECTION_ROW && !has_collection(s, s.provider)) {
            s.row = 2;
            s.collectionOpen = s.collectionArmed = false;
        }
        const auto message=string(state,"message");
        if(blockedTake.empty()||!s.clipReady||s.clipPath!=blockedTake||!s.error.empty())
            s.status=!s.error.empty()?s.error:!message.empty()?message:s.backendStatus=="idle"?"Choose genre and Search. Results retain their source and license links.":s.backendStatus;
        if (clipConfirmed) s.status = "Clip marked. Render selection to open it in the editor.";
        else if (clipTimedOut && !clipRejected) s.status = "Clip markers did not update. Try Quick clip again.";
        else if (s.quickClipRequest) s.status = "Setting clip markers...";
        if(s.openWhenReady){
            if(!s.error.empty()||s.backendStatus=="stopped"){s.openWhenReady=false;s.importContextBound=false;}
            else if(s.clipReady&&!s.busy&&!s.rendering&&!s.recording&&(!s.takeRequestIdsAvailable||number(state,"clip_request_id",UINT32_MAX)==s.expectedTakeRequest)){
                s.openWhenReady=false;s.importPath=s.clipPath;s.importRequested=true;s.importIsAutomatic=true;s.status="Opening this finished take in the sample editor.";
            }
        }
        s.lastSnapshot=raw;return true;
    }catch(...){return poll_error("Source service could not be read. Try again.");}
}

void source_sampler_stop(SourceSamplerState& s) {
    if(send(s,"stop")){s.playing=false;s.recording=false;s.status="Source stopped.";}
    s.aUsed=false;s.pendingTrackList=false;s.openWhenReady=false;s.importRequested=false;s.importIsAutomatic=false;s.importContextBound=false;s.importPath.clear();
    s.quickClipArmed=false;s.quickClipRequest.reset();
    s.collectionOpen=s.collectionArmed=false;
}

void source_sampler_bind_import_context(AppState& s) {
    auto& v=s.sourceSampler;
    if(!v.openWhenReady||v.importContextBound)return;
    v.importProject=s.project;v.importProjectVersion=s.projectVersion;
    v.importInstrument=std::clamp(v.targetInstrument>=0?v.targetInstrument:s.currentInstrument,0,127);
    v.importContextBound=true;
}

bool source_sampler_apply_import(AppState& s,songcore::SongcoreHost& host,AudioEngine& engine,FileSystem& fs) {
    auto& v=s.sourceSampler;
    if(!s.studioOpen||s.studioTab!=7||s.studioHelp||!v.importRequested)return false;
    v.importRequested=false;
    const bool automatic=v.importIsAutomatic;v.importIsAutomatic=false;
    const bool sameContext=v.importContextBound&&v.importProject==s.project&&v.importProjectVersion==s.projectVersion;
    v.importContextBound=false;
    if(!v.clipReady||!clip_valid(v)||v.importPath!=v.clipPath){v.status="This take is no longer ready. Render or choose a valid WAV again.";return true;}
    if(automatic&&!sameContext){v.status="Take saved. Project changed; choose a slot and Open sample editor to import it.";return true;}
    // Recording may be an overlay over an open editor. Navigating from Record
    // to Sources still leaves that editor's unsaved PCM/markers in place. Even
    // loading a different slot would replace its only editor/save context.
    if(s.currentScreen==ScreenType::SAMPLE_EDITOR&&s.sampleEditor.isModified){v.status=EDITOR_TAKE_BLOCKED;return true;}
    if(s.sourceImportedFor!=v.importPath){s.sourceImportedFor=v.importPath;s.sourceImportedPath.clear();}
    const int target=automatic?v.importInstrument:v.targetInstrument;
    const int previous=s.currentInstrument;
    if(target>=0)s.currentInstrument=std::clamp(target,0,127);
    if(studio_import_take(s,host,engine,fs,v.importPath,s.sourceImportedPath,v.title,"Samples/Clips")){
        source_sampler_stop(v);s.studioOpen=false;s.pendingSampleEditor=true;
    }else{s.currentInstrument=previous;v.status=s.studioMessage;}
    return true;
}

bool source_sampler_submit_query(SourceSamplerState& s, const std::string& query) {
    s.query = query.substr(0, 160);
    s.listFocused = false;s.showingTracks=false;
    s.pendingTrackList = false;
    if (!idle(s)) return false;
    if (send(s, "search", search_args(s))) {
        s.shelfTitle.clear();
        s.busy = true;
        invalidate_clip(s);
        return true;
    }
    return false;
}

bool source_sampler_button(const ButtonEvent& e, SourceSamplerState& s, songcore::SongcoreHost& host, AudioEngine& engine) {
    if (e.cancelled) {
        s.quickClipArmed = false;
        s.collectionArmed = false;
        if (e.button == Button::A) s.aUsed = false;
        return true;
    }
    if(s.showHelp){
        if(e.action==ButtonAction::RELEASED&&(e.button==Button::B||e.button==Button::SELECT))s.showHelp=false;
        return true;
    }
    s.row=std::clamp(s.row,0,SOURCE_SAMPLER_ROWS-1);s.col=std::clamp(s.col,0,s.row==2?2:1);s.provider=std::clamp(s.provider,0,SOURCE_SAMPLER_PROVIDERS-1);
    if (s.collectionOpen) return collection_button(e,s);
    if (s.row == SOURCE_COLLECTION_ROW && !has_collection(s, s.provider)) s.row = 2;
    if(s.row!=3&&s.row!=4)s.listFocused=false;
    if(s.row==3)s.showingTracks=false;else if(s.row==4)s.showingTracks=true;
    if(e.button==Button::START){if(e.action==ButtonAction::PRESSED)source_sampler_stop(s);return false;}
    if(e.button==Button::B){
        if(s.listFocused){if(e.action==ButtonAction::RELEASED)s.listFocused=false;return true;}
        if(e.action==ButtonAction::RELEASED)source_sampler_stop(s);return false;
    }
    if(e.button==Button::SELECT&&e.action==ButtonAction::RELEASED){s.showHelp=true;s.quickClipArmed=false;return true;}
    if(e.button==Button::L_SHIFT){
        if(e.action==ButtonAction::RELEASED&&idle(s))send(s,"sort",{{"order",s.sortTitle?"source":"title"}});
        return true;
    }
    if(e.button==Button::A){
        if(e.action==ButtonAction::PRESSED){
            s.aUsed=false;
            s.quickClipArmed=s.row==SOURCE_QUICK_CLIP_ROW&&!e.mods.r&&!e.mods.l&&!e.mods.b&&!e.mods.select;
            return true;
        }
        const bool quickClipArmed = s.quickClipArmed;
        s.quickClipArmed = false;
        if(s.aUsed){s.aUsed=false;return true;}
        if (s.row == SOURCE_QUICK_CLIP_ROW) {
            if (quickClipArmed) set_quick_clip(s);
            return true;
        }
        if(!s.backend&&((s.row>=2&&s.row<=10)||s.row==13)){s.status="Offline build: use the Online app on a WiFi device for Internet sources.";return true;}
        if(s.row==0||s.row==1){
            if(s.row==1)s.genre=(std::clamp(s.genre,0,genreCount-1)+1)%genreCount;
            else for(int step=1;step<SOURCE_SAMPLER_PROVIDERS;++step){const int other=(s.provider+step)%SOURCE_SAMPLER_PROVIDERS;if(s.providerAvailable[other]){s.provider=other;break;}}
        }
        else if(s.row==SOURCE_COLLECTION_ROW){open_collection(s);}
        else if(s.row==2){if(s.providerAvailable[s.provider]){
            s.listFocused=false;s.pendingTrackList=false;s.showingTracks=false;
            if(s.col==2){if(s.favoritesAvailable&&idle(s)&&send(s,"favorites",search_args(s))){s.shelfTitle.clear();s.busy=true;invalidate_clip(s);}else s.status="Station favorites are unavailable on this backend.";}
            else if(s.col==1){auto args=search_args(s);args["query"]="";if(idle(s)&&send(s,"discover",args)){s.shelfTitle.clear();s.busy=true;invalidate_clip(s);}}
            else if(!s.quickClipRequest&&send(s,"query_dialog",search_args(s)))s.shelfTitle.clear();
        }else s.status="This provider is unavailable on this device.";}
        else if(s.row==3){
            if(!idle(s))return true;
            if(s.selectedResult<0||s.selectedResult>=static_cast<int>(s.results.size()))s.status="Search first, then choose a result.";
            else{stop_tracker(host,engine);if(send(s,"open",{{"index",s.selectedResult},{"auto_listen",true}})){s.busy=true;s.pendingTrackList=true;invalidate_clip(s);}}
        }else if(s.row==4){
            if(!idle(s))return true;
            if(s.selectedTrack<0||s.selectedTrack>=static_cast<int>(s.tracks.size()))s.status="Open a result to view its tracks.";
            else{stop_tracker(host,engine);if(send(s,"listen",{{"index",s.selectedTrack}})){
                s.busy=s.selectedTrack!=s.playingTrack;if(s.busy)invalidate_clip(s);
            }}
        }else if(s.row==5){
            if(s.col==1)source_sampler_stop(s);
            else if(s.playing)send(s,"pause");
            else if(idle(s)&&loaded(s)){stop_tracker(host,engine);send(s,"play");}
        }else if(s.row==6 && live_history(s)){
            if(idle(s)&&history_ready(s))send(s,"live_edge");
        }else if(s.row==7||s.row==8){
            if(s.live&&!history_ready(s))s.status="Play a station with rewind to mark a radio clip.";
            else if(idle(s)&&loaded(s)){invalidate_clip(s);send(s,s.row==7?"mark_in":"mark_out");}
        }
        else if(s.row==9){
            if(s.recording){const auto id=s.openWhenReady?s.expectedTakeRequest:take_request(s);if(send(s,"record",{{"enabled",false},{"request_id",id}})){s.recording=false;s.busy=true;s.rendering=true;if(!s.openWhenReady)arm_editor(s,id);}}
            else if(live_history(s)&&s.liveRewound)s.status="Use Save last or Render selection. On Rewind, A returns to LIVE.";
            else if(idle(s)&&loaded(s)){const auto id=take_request(s);stop_tracker(host,engine);if(send(s,"record",{{"enabled",true},{"request_id",id}})){s.recording=true;if(s.live)arm_editor(s,id);else invalidate_clip(s);}}
        }
        else if(s.row==10){
            if(!idle(s))return true;
            if(!region_valid(s))s.status=s.live?"Set IN then OUT inside the current radio history."
                :"Set IN before OUT; choose at most "+std::to_string(SOURCE_CLIP_MAX_MS/1000)+" seconds.";
            else{const auto id=take_request(s);if(send(s,"render",{{"request_id",id}})){s.busy=true;s.rendering=true;arm_editor(s,id);}}
        }else if(s.row==12){
            if(!idle(s))return true;
            if(!s.clipReady||!clip_valid(s))s.status="Render a valid PCM clip before importing.";
            else{stop_tracker(host,engine);s.importPath=s.clipPath;s.importRequested=true;s.importIsAutomatic=false;s.status="Ready to import this clip into the sample editor.";}
        }else if(s.row==14){s.status="Left / Right: preview level25-200%; rendered samples keep their original gain.";}
        else if(s.row==15){s.status="Left / Right: 10, 20, 30, 40, 45 or 60 seconds.";}
        else if(s.row==16){
            if(!s.favoritesAvailable)s.status="Station favorites are unavailable on this backend.";
            else send(s,"favorite",{{"list",s.showingTracks?"tracks":"results"},{"index",s.showingTracks?s.selectedTrack:s.selectedResult}});
        }else if(s.row==13){
            std::string url=s.col==1?s.licenseUrl:s.sourceUrl;
            if(url.empty()&&s.selectedResult>=0&&s.selectedResult<static_cast<int>(s.results.size()))url=s.col==1?s.results[s.selectedResult].license:s.results[s.selectedResult].url;
            if(url.empty())s.status=s.col==1?"No license link was supplied; check the original source page.":"No source page is available for this item.";
            else if(url.rfind("https://",0)!=0)s.status="This source link is not an HTTPS page.";
            else send(s,"info",{{"url",url}});
        }
        return true;
    }
    if(e.action!=ButtonAction::PRESSED||!is_dpad(e.button))return false;
    const bool positive=e.button==Button::DPAD_RIGHT||e.button==Button::DPAD_UP;
    if(e.mods.r&&(e.button==Button::DPAD_LEFT||e.button==Button::DPAD_RIGHT)){
        const int64_t delta=(positive?1:-1)*(e.mods.a?1000:5000);
        if(live_history(s))history_seek(s,delta);
        else if(s.live)s.status="Live rewind is unavailable on this device.";
        else if(idle(s)&&loaded(s))send(s,"seek",{{"position_ms",std::clamp<int64_t>(s.positionMs+delta,0,s.durationMs)}});
        if(e.mods.a)s.aUsed=true;return true;
    }
    if(s.listFocused){
        if(e.mods.a)s.aUsed=true;
        if(e.button==Button::DPAD_LEFT){s.listFocused=false;return true;}
        if(e.button==Button::DPAD_UP||e.button==Button::DPAD_DOWN){
            auto& cursor=s.row==3?s.selectedResult:s.selectedTrack;const auto& list=s.row==3?s.results:s.tracks;
            cursor=selected(cursor+(e.button==Button::DPAD_UP?-1:1),list.size());
        }
        return true;
    }
    if(!e.mods.a){
        if(e.button==Button::DPAD_UP||e.button==Button::DPAD_DOWN){const auto rows=visibleRows(s);s.row=rows[(displayIndex(rows,s.row)+int(rows.size())+(e.button==Button::DPAD_DOWN?1:-1))%int(rows.size())];}
        else if((s.row==3||s.row==4)&&e.button==Button::DPAD_RIGHT){s.listFocused=true;s.showingTracks=s.row==4;}
        else if(s.row==0){for(int step=1;step<SOURCE_SAMPLER_PROVIDERS;++step){const int other=(s.provider+SOURCE_SAMPLER_PROVIDERS+(positive?1:-1)*step)%SOURCE_SAMPLER_PROVIDERS;if(s.providerAvailable[other]){s.provider=other;break;}}}
        else if(s.row==1)s.genre=(std::clamp(s.genre,0,genreCount-1)+genreCount+(positive?1:-1))%genreCount;
        else if(s.row==2)s.col=(s.col+3+(positive?1:-1))%3;
        else if(s.row==5||s.row==13)s.col=positive?1:0;
        else if(s.row==6&&live_history(s))history_seek(s,positive?5000:-5000);
        else if(s.row==14&&s.previewGainAvailable){const int gain=std::clamp(s.previewGainPercent+(positive?10:-10),25,200);if(send(s,"preview_gain",{{"percent",gain}}))s.previewGainPercent=gain;}
        else if ((s.row == 15 || (s.row == 9 && s.live)) && s.liveAvailable && idle(s)) {
            const auto& lengths = sources::live_take_lengths;
            const auto found = std::find(lengths.begin(), lengths.end(), s.liveCaptureSeconds);
            const int current = found == lengths.end() ? 0 : static_cast<int>(found - lengths.begin());
            const int next = std::clamp(current + (positive ? 1 : -1), 0, static_cast<int>(lengths.size()) - 1);
            if (next != current && send(s, "live_length", {{"seconds", lengths[next]}}))
                s.liveCaptureSeconds = lengths[next];
        }
        else if (s.row == SOURCE_QUICK_CLIP_ROW) {
            if(live_history(s)){
                const int last=history_last_length_index(s);
                s.historyTakeLengthIndex=std::clamp(std::min(s.historyTakeLengthIndex,last)+(positive?1:-1),0,last);
            }else if(!s.live){
                const int last=static_cast<int>(clipLengths.size())-1;
                s.quickClipLengthIndex=std::clamp(quick_clip_index(s)+(positive?1:-1),0,last);
            }
        }
        return true;
    }
    s.aUsed=true;
    const int direction=positive?1:-1;
    const bool coarse=e.button==Button::DPAD_UP||e.button==Button::DPAD_DOWN;
    if(s.row==0){for(int step=1;step<SOURCE_SAMPLER_PROVIDERS;++step){const int other=(s.provider+SOURCE_SAMPLER_PROVIDERS+direction*step)%SOURCE_SAMPLER_PROVIDERS;if(s.providerAvailable[other]){s.provider=other;break;}}}
    else if(s.row==1)s.genre=(std::clamp(s.genre,0,genreCount-1)+genreCount+direction)%genreCount;
    else if(s.row==3)s.selectedResult=selected(s.selectedResult+direction,s.results.size());
    else if(s.row==4)s.selectedTrack=selected(s.selectedTrack+direction,s.tracks.size());
    else if(s.row==5||s.row==13)s.col=positive?1:0;
    else if(s.row==11)s.targetInstrument=std::clamp(s.targetInstrument+direction*(coarse?16:1),-1,127);
    else if(s.row==6){
        if(live_history(s))history_seek(s,direction*(coarse?10000:1000));
        else if(!s.live&&idle(s)&&loaded(s))send(s,"seek",{{"position_ms",std::clamp<int64_t>(s.positionMs+direction*(coarse?10000:1000),0,s.durationMs)}});
    }
    else if(s.row==7||s.row==8){
        if(s.live&&!region_valid(s)){s.status="Mark IN and OUT within the radio history first.";return true;}
        if(!idle(s)||!loaded(s))return true;
        invalidate_clip(s);int64_t in=s.inMs,out=s.outMs;
        const int64_t start=s.live?s.liveWindowStartMs:0;
        const int64_t end=s.live?s.liveWindowEndMs:s.durationMs;
        if(s.row==7)in=std::clamp<int64_t>(in+direction*(coarse?1000:10),start,std::max(start,out-(s.live?1:0)));
        else out=std::clamp<int64_t>(out+direction*(coarse?1000:10),in+(s.live?1:0),end);
        send(s,"set_region",{{"in_ms",in},{"out_ms",out}});
    }
    return true;
}

void source_sampler_draw(Canvas& c,const SourceSamplerState& s,const Theme& t,const songcore::Project& project) {
    if (s.collectionOpen && has_collection(s, s.collectionProvider)) {
        draw_collection_picker(c,s,t);
        return;
    }
    const auto panel=t.rowEvery4th,accent=t.rowCursor,ink=t.textValue,muted=t.textParam;
    const bool showTracks=s.showingTracks&&!s.tracks.empty();
    const auto& list=showTracks?s.tracks:s.results;
    const int chosen=showTracks?s.selectedTrack:s.selectedResult;
    const auto* info=(s.row==3||s.row==4)&&chosen>=0&&chosen<static_cast<int>(list.size())?&list[chosen]:nullptr;
    const auto& instruments=info?info->instruments:s.instruments;
    const auto& tags=info?info->tags:s.tags;
    const auto& description=info?info->description:s.description;
    source_panel(c,18,104,604,48,t,panel,with_alpha(muted,.2f));
    c.draw_label("SOURCES / DIG & CAPTURE",30,112,accent,12);
    const std::string metadataLine=!instruments.empty()?"Instruments: "+instruments:!tags.empty()?"Tags: "+tags:"Instrument details not supplied";
    c.draw_label(Canvas::clip_label(metadataLine,214,12),294,112,muted,12);
    const auto stateColor=s.recording?t.meterHigh:s.playing?accent:muted;
    c.panel_rect(518,109,92,23,darken(panel,.72f),with_alpha(stateColor,.6f),4);
    if(s.playing){for(int i=0;i<8;++i)c.fill_rect(526+i,116+i/2,1,9-i,stateColor);}
    else{c.fill_rect(527,116,2,10,stateColor);c.fill_rect(532,116,2,10,stateColor);}
    const auto playerState=!s.backend?"OFFLINE":s.recording?"RECORD":s.rendering?"RENDER":s.busy?"LOADING":
        s.playing?(s.live?(s.liveRewound?"REWIND":"LIVE"):"PLAYING"):s.durationMs>0?"PAUSED":"READY";
    c.draw_label(playerState,541,113,stateColor,12);
    c.draw_label(Canvas::clip_label(s.title.empty()?source_sampler_provider_name(s.provider):s.title,570,18),30,129,ink,18);
    if(s.showHelp){
        source_panel(c,18,162,604,278,t,panel,with_alpha(muted,.2f),7);
        const char* help[]={"Browse sources: A opens. L / R changes the provider inside.",
            "Results / Tracks: Right enters the list; Up / Down browses vertically.",
            "A opens an album or previews the selected title or live station.",
            "Left or B returns to controls. L switches source order / title A-Z.",
            "Preview: Play / Stop. Preview level adjusts listening gain only.",
            "Quick clip: Left / Right sets length. A marks from the playhead.",
            "Files: R + arrows seeks 5s. A marks IN / OUT. A + arrows trims.",
            "Render + Edit renders IN/OUT and opens the finished WAV in the editor.",
            live_history(s) ? "Rewind: Left / Right jumps 5s. A returns LIVE. Save last makes a WAV."
                            : "Record live: Left / Right picks 10 to 60 seconds. A starts / finishes.",
            "Discover: Search / Random / Favorites. Radio rights remain unchanged."};
        for(int i=0;i<10;++i)c.draw_label(help[i],30,174+i*25,i==8?accent:ink,12);
        return;
    }
    source_panel(c,18,162,210,278,t,panel,with_alpha(muted,.18f));
    c.draw_label("BROWSE / CAPTURE",29,172,accent,12);
    const auto visible=visibleRows(s);const int rowCount=int(visible.size());
    const int cursor = s.row == SOURCE_COLLECTION_ROW && !has_collection(s, s.provider) ? 2 : s.row;
    const int first=std::clamp(displayIndex(visible,cursor)-5,0,std::max(0,rowCount-8));
    for(int index=first;index<std::min(first+8,rowCount);++index){
        const int i=visible[index];const int y=196+(index-first)*29;const bool current=i==cursor;
        if(current&&!s.listFocused)c.panel_rect(25,y-3,196,28,darken(accent,.16f),accent,3);
        const char* name = i==9&&s.live ? "Record live" : i==6&&live_history(s) ? "Rewind"
            : i==SOURCE_QUICK_CLIP_ROW&&live_history(s) ? "Save last" : rowNames[i];
        c.draw_label(name,31,y,current?ink:muted,12);
        std::string value=control_value(s,i);
        if(i==11&&s.targetInstrument>=0&&s.targetInstrument<static_cast<int>(project.instruments.size()))value+=" / "+project.instruments[s.targetInstrument].name;
        value=Canvas::clip_label(value,182,12);
        c.draw_label(value,31,y+12,current?accent:ink,12);
    }
    c.fill_rect(222,195,1,230,with_alpha(muted,.12f));
    c.fill_rect(222,195+first*99/std::max(1,rowCount-8),1,131,with_alpha(accent,.7f));
    source_panel(c,238,162,384,166,t,darken(panel,.76f),s.listFocused?accent:with_alpha(muted,.18f));
    c.draw_label(s.listFocused?(showTracks?"> TITLES / A: PREVIEW":"> RESULTS / A: OPEN"):(showTracks?"TITLES":"DISCOVERED SOURCES"),250,172,accent,12);
    c.draw_label(s.sortTitle?"A-Z / L":"Source / L",453,172,muted,12);
    c.draw_label(std::to_string(list.size())+" items",550,172,muted,12);
    if(list.empty()){
        c.draw_label(s.busy?"Finding your next sound...":"Start at Browse sources or try Search / Random.",250,211,muted,12);
        c.draw_label("In results, A opens or plays the selected item.",250,234,ink,12);
    }
    const int listFirst=std::clamp(chosen-2,0,std::max(0,static_cast<int>(list.size())-5));
    for(int i=listFirst;i<std::min(listFirst+5,static_cast<int>(list.size()));++i){
        const int y=194+(i-listFirst)*23;const bool current=i==chosen;
        if(current)c.panel_rect(246,y-2,368,23,panel,with_alpha(accent,s.listFocused?.8f:.25f),3);
        if(i==(showTracks?s.playingTrack:s.openedResult))c.fill_rect(609,y+5,2,12,accent);
        c.draw_label(Canvas::clip_label(list[i].title.empty()?list[i].id:list[i].title,350,12),253,y,current?ink:muted,12);
        const auto detail=list[i].creator.empty()?(list[i].kind.empty()?"Creator not supplied":list[i].kind):list[i].creator;
        c.draw_label(Canvas::clip_label(detail,350,12),253,y+12,muted,12);
    }
    c.draw_label("PREVIEW "+(s.previewGainAvailable?std::to_string(s.previewGainPercent)+"%":"LEVEL: DEVICE")+"  /  "+(s.playing?"A: PAUSE  |  Stop cancels":"A: LISTEN"),250,316,s.playing?accent:muted,12);
    source_panel(c,238,338,384,102,t,panel,with_alpha(muted,.18f));
    draw_capture_timeline(c, s, t);
    std::string catalog=s.catalogProvider;
    for(int p=0;p<SOURCE_SAMPLER_PROVIDERS;++p)if(catalog==providerIds[p]){catalog=providers[p];break;}
    if(!s.shelfTitle.empty())catalog+=" / "+s.shelfTitle;
    else if(const auto name=collection_name(s.catalogCollection);!name.empty())catalog+=" / "+name;
    if(!s.catalogGenre.empty())catalog+=" / "+s.catalogGenre;
    const std::string about=(catalog.empty()?"":catalog+" | ")+(description.empty()?"Source / license links retain the original attribution.":description);
    c.draw_label(Canvas::clip_label(about,604,12),18,443,muted,12);
    const std::string browseHint=s.listFocused?"Up / Down: titles   A: open / listen   Left / B: controls   Preview > Stop cancels":
        (s.row==0||s.row==1)?"Left / Right: select    A: next    Down: collections / search":
        cursor==SOURCE_COLLECTION_ROW?"A: browse archives and radio   L / R switches providers inside":
        s.row==2?"Left / Right: Search / Random / Favorites     A: discover     L: title order":
        live_history(s)&&s.row==6?"Left / Right: jump 5 s     A: return LIVE":
        live_history(s)&&s.row==SOURCE_QUICK_CLIP_ROW?"Left / Right: length     A: save before the playhead and open editor":
        s.live && s.row==9?(s.recording?"A: finish now and open the take":s.liveRewound?"Use Save last while rewound":"Left / Right: take length (10-60 s)     A: record"):
        s.row==15?"Left / Right: 10, 20, 30, 40, 45 or 60 seconds":
        (s.row==3||s.row==4)?"Right: enter list    Up / Down: controls    L: source order / A-Z":s.status;
    c.draw_label(Canvas::clip_label(s.error.empty()&&!s.busy?browseHint:s.status,604,12),18,461,s.error.empty()?t.textTitle:t.meterHigh,12);
}
} // namespace pt::ui
