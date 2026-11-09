#include "ui/source_sampler.h"
#include "ui/canvas.h"
#include "ui/helpers.h"
#include "songcore/host.h"
#include "songcore/project_io.h"
#include "audio-engine.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace pt::ui {
namespace {
using songcore::json;
constexpr const char* genres[] = {"Jazz", "Soul", "Library", "Funk", "All genres"};
constexpr const char* genreIds[] = {"jazz", "soul", "library", "funk", ""};
constexpr const char* providers[] = {"Internet Archive", "Wikimedia Commons", "YouTube / experimental"};
constexpr const char* providerIds[] = {"archive", "commons", "youtube"};
constexpr const char* rows[] = {"Provider", "Genre", "Search", "Results", "Tracks", "Transport", "Seek",
                               "IN marker", "OUT marker", "Record region", "Render selection", "Target slot", "Import WAV", "Source / license"};
constexpr int64_t MAX_SOURCE_MS = 7LL * 24 * 60 * 60 * 1000;

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
    if(it->is_array())for(const auto& value:*it)if(value.is_string()){
        if(!result.empty())result+=" / ";result+=value.get<std::string>();if(result.size()>300)break;
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
                       metadata(item,"description"),metadata(item,"tags"),metadata(item,"instruments")});
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
    s.clipReady=false;s.importRequested=false;
}
bool send(SourceSamplerState& s, const char* action, const json& args = json::object()) {
    if (!s.backend) { s.status = "Source service is not connected on this device."; return false; }
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
    if (!s.busy && !s.rendering) return true;
    s.status = "Source is busy. Use Transport > Stop to cancel first."; return false;
}
bool loaded(SourceSamplerState& s) {
    if (s.durationMs > 0 && !s.title.empty()) return true;
    s.status = "Choose a result, then load a track first."; return false;
}
bool region_valid(const SourceSamplerState& s) {
    return s.durationMs > 0 && s.inMs >= 0 && s.outMs > s.inMs && s.outMs <= s.durationMs && s.outMs-s.inMs <= SOURCE_CLIP_MAX_MS;
}
bool clip_valid(const SourceSamplerState& s) {
    return !s.clipPath.empty()&&s.clipFrames>0&&s.clipRate>0&&(s.clipChannels==1||s.clipChannels==2)&&
           s.clipFrames<=static_cast<int64_t>(s.clipRate)*SOURCE_CLIP_MAX_MS/1000;
}
std::string time(int64_t value) {
    value=std::max<int64_t>(0,value);char text[32];
    std::snprintf(text,sizeof(text),"%lld:%02lld.%03lld",static_cast<long long>(value/60000),
                  static_cast<long long>(value/1000%60),static_cast<long long>(value%1000));return text;
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
        case 2:return s.col==1?"RANDOM / "+std::string(source_sampler_genre_name(s.genre)):s.query.empty()?"A: enter query":s.query;
        case 3:return std::to_string(s.results.empty()?0:s.selectedResult+1)+" / "+std::to_string(s.results.size());
        case 4:return std::to_string(s.tracks.empty()?0:s.selectedTrack+1)+" / "+std::to_string(s.tracks.size());
        case 5:return s.col==1?"STOP / CANCEL":s.playing?"PAUSE":"PLAY";
        case 6:return time(s.positionMs);
        case 7:return time(s.inMs);
        case 8:return time(s.outMs);
        case 9:return s.recording?"STOP + RENDER":"RECORD IN / OUT";
        case 10:return s.rendering?"RENDERING...":"PCM16 WAV / max3min";
        case 11:return slot(s.targetInstrument);
        case 12:return s.clipReady?"IMPORT READY CLIP":"No ready clip";
        default:return s.col==1?"LICENSE LINK":"SOURCE PAGE";
    }
}
json search_args(const SourceSamplerState& s) {
    return {{"provider",providerIds[std::clamp(s.provider,0,SOURCE_SAMPLER_PROVIDERS-1)]},{"genre",genreIds[std::clamp(s.genre,0,4)]},{"query",s.query}};
}
} // namespace

const char* source_sampler_genre_name(int genre) { return genres[std::clamp(genre,0,4)]; }
const char* source_sampler_provider_name(int provider) { return providers[std::clamp(provider,0,SOURCE_SAMPLER_PROVIDERS-1)]; }

bool source_sampler_poll(SourceSamplerState& s, uint64_t now) {
    if(s.hasPolled&&now>=s.lastPollMs&&now-s.lastPollMs<100)return false;
    const bool forced=!s.hasPolled;
    s.lastPollMs=now;s.hasPolled=true;
    if(!s.backend)return false;
    try{
        const std::string raw=s.backend->state_json();
        if(raw==s.lastSnapshot&&!forced)return false;
        if(raw.size()>2*1024*1024){s.status="Source response is too large.";return true;}
        const auto state=json::parse(raw,nullptr,false);
        if(state.is_discarded()||!state.is_object()){s.status="Source returned an unreadable status.";return true;}
        auto results=items(state,"results"),tracks=items(state,"tracks");
        const int opened=index(state,"selected_index"),playing=index(state,"track_index");
        s.selectedResult=preserve_selection(s.results,s.selectedResult,results,opened);
        s.selectedTrack=preserve_selection(s.tracks,s.selectedTrack,tracks,playing);
        s.results=std::move(results);s.tracks=std::move(tracks);s.openedResult=opened;s.playingTrack=playing;
        s.query=string(state,"query",s.query);s.title=string(state,"title");s.creator=string(state,"creator");
        s.description=metadata(state,"description");s.tags=metadata(state,"tags");s.instruments=metadata(state,"instruments");
        s.sortTitle=string(state,"sort_order",s.sortTitle?"title":"source")=="title";
        s.sourceUrl=string(state,"source_url");s.licenseUrl=string(state,"license_url");
        s.backendStatus=string(state,"status","idle");s.error=string(state,"error");
        s.busy=flag(state,"busy");s.playing=flag(state,"playing");s.recording=flag(state,"recording");s.rendering=flag(state,"rendering");
        if(s.pendingTrackList&&!s.busy){
            s.pendingTrackList=false;
            if(!s.tracks.empty()){s.row=4;s.col=0;s.listFocused=true;}
        }
        s.durationMs=number(state,"duration_ms");s.positionMs=std::min(number(state,"position_ms"),s.durationMs);
        s.inMs=std::min(number(state,"in_ms"),s.durationMs);s.outMs=std::min(number(state,"out_ms"),s.durationMs);
        s.clipPath=string(state,"clip_path");s.clipFrames=number(state,"clip_frames",48000LL*180*8);
        s.clipRate=static_cast<int>(number(state,"clip_rate",384000));s.clipChannels=static_cast<int>(number(state,"clip_channels",32));
        s.clipReady=flag(state,"clip_ready")&&clip_valid(s)&&s.clipPath!=s.invalidatedClipPath;
        if(!s.clipPath.empty()&&s.clipPath!=s.invalidatedClipPath)s.invalidatedClipPath.clear();
        if(s.importRequested&&(!s.clipReady||s.importPath!=s.clipPath)){s.importRequested=false;s.importPath.clear();}
        if(auto it=state.find("progress");it!=state.end()&&it->is_number()){
            const double v=it->get<double>();s.progress=std::isfinite(v)?std::clamp(v,0.,1.):0.;
        }else s.progress=0;
        if(auto it=state.find("providers");it!=state.end()&&it->is_array()){
            s.providerAvailable={{false,false,false}};
            for(const auto& provider:*it)if(provider.is_object())for(int i=0;i<SOURCE_SAMPLER_PROVIDERS;++i)
                if(string(provider,"id")==providerIds[i])s.providerAvailable[i]=flag(provider,"available");
            s.provider=std::clamp(s.provider,0,SOURCE_SAMPLER_PROVIDERS-1);
            if(!s.providerAvailable[s.provider])for(int i=0;i<SOURCE_SAMPLER_PROVIDERS;++i)if(s.providerAvailable[i]){s.provider=i;break;}
        }
        const auto message=string(state,"message");
        s.status=!s.error.empty()?s.error:!message.empty()?message:s.backendStatus=="idle"?"Choose genre and Search. Results retain their source and license links.":s.backendStatus;
        s.lastSnapshot=raw;return true;
    }catch(...){s.status="Source service could not be read. Try again.";return true;}
}

void source_sampler_stop(SourceSamplerState& s) {
    if(send(s,"stop")){s.playing=false;s.recording=false;s.status="Source stopped.";}
    s.aUsed=false;s.pendingTrackList=false;
}

bool source_sampler_submit_query(SourceSamplerState& s, const std::string& query) {
    s.query = query.substr(0, 160);
    s.listFocused = false;
    s.pendingTrackList = false;
    if (!idle(s)) return false;
    if (send(s, "search", search_args(s))) {
        s.busy = true;
        invalidate_clip(s);
        return true;
    }
    return false;
}

bool source_sampler_button(const ButtonEvent& e, SourceSamplerState& s, songcore::SongcoreHost& host, AudioEngine& engine) {
    s.row=std::clamp(s.row,0,SOURCE_SAMPLER_ROWS-1);s.col=std::clamp(s.col,0,1);s.provider=std::clamp(s.provider,0,SOURCE_SAMPLER_PROVIDERS-1);
    if(s.row!=3&&s.row!=4)s.listFocused=false;
    if(e.button==Button::START){if(e.action==ButtonAction::PRESSED)source_sampler_stop(s);return false;}
    if(e.button==Button::B){
        if(s.listFocused&&!s.showHelp){if(e.action==ButtonAction::RELEASED)s.listFocused=false;return true;}
        if(e.action==ButtonAction::RELEASED){if(s.showHelp){s.showHelp=false;return true;}source_sampler_stop(s);}return false;
    }
    if(e.button==Button::SELECT&&e.action==ButtonAction::RELEASED){s.showHelp=!s.showHelp;return true;}
    if(s.showHelp)return true;
    if(e.button==Button::L_SHIFT){
        if(e.action==ButtonAction::RELEASED&&idle(s))send(s,"sort",{{"order",s.sortTitle?"source":"title"}});
        return true;
    }
    if(e.button==Button::A){
        if(e.action==ButtonAction::PRESSED){s.aUsed=false;return true;}
        if(s.aUsed){s.aUsed=false;return true;}
        if(!s.backend&&((s.row>=2&&s.row<=10)||s.row==13)){s.status="Source service is not connected on this device.";return true;}
        if(s.row==2){if(s.providerAvailable[s.provider]){
            s.listFocused=false;s.pendingTrackList=false;
            if(s.col==1){if(idle(s)&&send(s,"discover",search_args(s))){s.busy=true;invalidate_clip(s);}}
            else send(s,"query_dialog",search_args(s));
        }else s.status="This provider is unavailable on this device.";}
        else if(s.row==3){
            if(!idle(s))return true;
            if(s.selectedResult<0||s.selectedResult>=static_cast<int>(s.results.size()))s.status="Search first, then choose a result.";
            else{stop_tracker(host,engine);if(send(s,"open",{{"index",s.selectedResult}})){s.busy=true;s.pendingTrackList=true;invalidate_clip(s);}}
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
        }else if(s.row==7||s.row==8){if(idle(s)&&loaded(s))send(s,s.row==7?"mark_in":"mark_out");}
        else if(s.row==9){
            if(s.recording){if(send(s,"record",{{"enabled",false}})){s.recording=false;s.busy=true;s.rendering=true;invalidate_clip(s);}}
            else if(idle(s)&&loaded(s)){stop_tracker(host,engine);if(send(s,"record",{{"enabled",true}})){s.recording=true;invalidate_clip(s);}}
        }
        else if(s.row==10){
            if(!idle(s))return true;
            if(!region_valid(s))s.status="Set IN before OUT; choose a region of at most3minutes.";
            else if(send(s,"render")){s.busy=true;s.rendering=true;invalidate_clip(s);}
        }else if(s.row==12){
            if(!idle(s))return true;
            if(!s.clipReady||!clip_valid(s))s.status="Render a valid PCM clip before importing.";
            else{stop_tracker(host,engine);s.importPath=s.clipPath;s.importRequested=true;s.status="Ready to import this clip into the sample editor.";}
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
        if(idle(s)&&loaded(s))send(s,"seek",{{"position_ms",std::clamp<int64_t>(s.positionMs+(positive?1:-1)*(e.mods.a?1000:5000),0,s.durationMs)}});
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
        if(e.button==Button::DPAD_UP||e.button==Button::DPAD_DOWN)s.row=(s.row+SOURCE_SAMPLER_ROWS+(e.button==Button::DPAD_DOWN?1:-1))%SOURCE_SAMPLER_ROWS;
        else if((s.row==3||s.row==4)&&e.button==Button::DPAD_RIGHT)s.listFocused=true;
        else if(s.row==2||s.row==5||s.row==13)s.col=positive?1:0;
        return true;
    }
    s.aUsed=true;
    const int direction=positive?1:-1;
    const bool coarse=e.button==Button::DPAD_UP||e.button==Button::DPAD_DOWN;
    if(s.row==0){for(int step=1;step<SOURCE_SAMPLER_PROVIDERS;++step){const int other=(s.provider+SOURCE_SAMPLER_PROVIDERS+direction*step)%SOURCE_SAMPLER_PROVIDERS;if(s.providerAvailable[other]){s.provider=other;break;}}}
    else if(s.row==1)s.genre=(std::clamp(s.genre,0,4)+5+direction)%5;
    else if(s.row==3)s.selectedResult=selected(s.selectedResult+direction,s.results.size());
    else if(s.row==4)s.selectedTrack=selected(s.selectedTrack+direction,s.tracks.size());
    else if(s.row==5||s.row==13)s.col=positive?1:0;
    else if(s.row==11)s.targetInstrument=std::clamp(s.targetInstrument+direction*(coarse?16:1),-1,127);
    else if(s.row==6){if(idle(s)&&loaded(s))send(s,"seek",{{"position_ms",std::clamp<int64_t>(s.positionMs+direction*(coarse?10000:1000),0,s.durationMs)}});}
    else if(s.row==7||s.row==8){
        if(!idle(s)||!loaded(s))return true;
        int64_t in=s.inMs,out=s.outMs;
        if(s.row==7)in=std::clamp<int64_t>(in+direction*(coarse?1000:10),0,std::max<int64_t>(0,out));
        else out=std::clamp<int64_t>(out+direction*(coarse?1000:10),in,s.durationMs);
        send(s,"set_region",{{"in_ms",in},{"out_ms",out}});
    }
    return true;
}

void source_sampler_draw(Canvas& c,const SourceSamplerState& s,const Theme& t,const songcore::Project& project) {
    const auto panel=t.rowEvery4th,accent=t.rowCursor,ink=t.textValue,muted=t.textParam;
    const bool showTracks=!s.tracks.empty()&&(s.row>=4&&s.row!=3);
    const auto& list=showTracks?s.tracks:s.results;
    const int chosen=showTracks?s.selectedTrack:s.selectedResult;
    const auto* info=(s.row==3||s.row==4)&&chosen>=0&&chosen<static_cast<int>(list.size())?&list[chosen]:nullptr;
    const auto& instruments=info?info->instruments:s.instruments;
    const auto& tags=info?info->tags:s.tags;
    const auto& description=info?info->description:s.description;
    source_panel(c,18,104,604,48,t,panel,with_alpha(muted,.2f));
    c.draw_label("SOURCE SAMPLER",30,112,accent,12);
    const std::string metadataLine=!instruments.empty()?"Instruments: "+instruments:!tags.empty()?"Tags: "+tags:"Instrument details not supplied";
    c.draw_label(Canvas::clip_label(metadataLine,306,12),196,112,muted,12);
    const auto stateColor=s.recording?t.meterHigh:s.playing?accent:muted;
    c.panel_rect(518,109,92,23,darken(panel,.72f),with_alpha(stateColor,.6f),4);
    if(s.playing){for(int i=0;i<8;++i)c.fill_rect(526+i,116+i/2,1,9-i,stateColor);}
    else{c.fill_rect(527,116,2,10,stateColor);c.fill_rect(532,116,2,10,stateColor);}
    const auto playerState=s.recording?"RECORD":s.rendering?"RENDER":s.busy?"LOADING":s.playing?"PLAYING":s.durationMs>0?"PAUSED":"READY";
    c.draw_label(playerState,541,113,stateColor,12);
    c.draw_label(Canvas::clip_label(s.title.empty()?source_sampler_provider_name(s.provider):s.title,570,18),30,129,ink,18);
    if(s.showHelp){
        source_panel(c,18,162,604,278,t,panel,with_alpha(muted,.2f),7);
        const char* help[]={"Search: Left / Right chooses Search or Random finds for this genre.",
            "Results / Tracks: Right enters the list; Up / Down browses vertically.",
            "A opens a result. A on a track plays or pauses that track.",
            "Left or B returns to controls. L switches source order / title A-Z.",
            "Hold R + Left / Right to seek5seconds; add A for1second steps.",
            "A on IN / OUT marks the position; A + Left / Right adjusts10ms.",
            "A + Up / Down adjusts markers1second. Existing Seek stays available.",
            "Record starts a region; Record again renders PCM16 WAV, max3minutes.",
            "Import uses the target slot and opens the normal sample editor.",
            "Descriptions and tags come from the source; absent details stay unknown."};
        for(int i=0;i<10;++i)c.draw_label(help[i],30,174+i*25,i==8?accent:ink,12);
        return;
    }
    source_panel(c,18,162,210,278,t,panel,with_alpha(muted,.18f));
    c.draw_label("BROWSE / CAPTURE",29,172,accent,12);
    const int first=std::clamp(s.row-5,0,SOURCE_SAMPLER_ROWS-8);
    for(int i=first;i<std::min(first+8,SOURCE_SAMPLER_ROWS);++i){
        const int y=196+(i-first)*29;const bool current=i==s.row;
        if(current&&!s.listFocused)c.panel_rect(25,y-3,196,28,darken(accent,.16f),accent,3);
        c.draw_label(rows[i],31,y,current?ink:muted,12);
        std::string value=control_value(s,i);
        if(i==11&&s.targetInstrument>=0&&s.targetInstrument<static_cast<int>(project.instruments.size()))value+=" / "+project.instruments[s.targetInstrument].name;
        value=Canvas::clip_label(value,182,12);
        c.draw_label(value,31,y+12,current?accent:ink,12);
    }
    c.fill_rect(222,195,1,230,with_alpha(muted,.12f));
    c.fill_rect(222,195+first*99/(SOURCE_SAMPLER_ROWS-8),1,131,with_alpha(accent,.7f));
    source_panel(c,238,162,384,166,t,darken(panel,.76f),s.listFocused?accent:with_alpha(muted,.18f));
    c.draw_label(showTracks?"TRACKS":"SEARCH RESULTS",250,172,accent,12);
    c.draw_label(s.sortTitle?"A-Z / L":"Source / L",453,172,muted,12);
    c.draw_label(std::to_string(list.size())+" items",550,172,muted,12);
    if(list.empty())c.draw_label(s.busy?"Loading source...":"Search, open a result, then choose a track.",250,211,muted,12);
    const int listFirst=std::clamp(chosen-2,0,std::max(0,static_cast<int>(list.size())-5));
    for(int i=listFirst;i<std::min(listFirst+5,static_cast<int>(list.size()));++i){
        const int y=194+(i-listFirst)*25;const bool current=i==chosen;
        if(current)c.panel_rect(246,y-2,368,25,panel,with_alpha(accent,s.listFocused?.8f:.25f),3);
        if(i==(showTracks?s.playingTrack:s.openedResult))c.fill_rect(609,y+5,2,12,accent);
        c.draw_label(Canvas::clip_label(list[i].title.empty()?list[i].id:list[i].title,350,12),253,y,current?ink:muted,12);
        const auto detail=list[i].creator.empty()?(list[i].kind.empty()?"Creator not supplied":list[i].kind):list[i].creator;
        c.draw_label(Canvas::clip_label(detail,350,12),253,y+12,muted,12);
    }
    source_panel(c,238,338,384,102,t,panel,with_alpha(muted,.18f));
    c.draw_label(s.recording?"RECORDING REGION":s.playing?"PLAYING SOURCE":"POSITION / REGION",250,348,accent,12);
    c.draw_label(time(s.positionMs)+" / "+time(s.durationMs),409,348,ink,12);
    constexpr int left=250,width=360,top=379;
    c.fill_rect(left,top,width,4,with_alpha(muted,.2f));
    const auto point=[&](int64_t ms){return left+static_cast<int>(s.durationMs>0?std::clamp<int64_t>(ms,0,s.durationMs)*width/s.durationMs:0);};
    if(s.durationMs>0){
        const int in=point(s.inMs),out=point(s.outMs),position=point(s.positionMs);
        if(out>in)c.fill_rect(in,top-3,out-in,10,with_alpha(accent,.2f));
        c.fill_rect(in,top-7,2,18,accent);c.fill_rect(out-1,top-7,2,18,t.textTitle);
        c.fill_rect(position,top-5,1,14,ink);
    }
    c.draw_label("IN "+time(s.inMs),250,400,accent,12);c.draw_label("OUT "+time(s.outMs),432,400,t.textTitle,12);
    std::string detail=s.clipReady?"READY WAV / "+std::to_string(s.clipRate)+"Hz / "+std::to_string(s.clipChannels)+"ch":
        s.rendering?"Rendering "+std::to_string(static_cast<int>((std::isfinite(s.progress)?std::clamp(s.progress,0.,1.):0.)*100))+"%":
        "REGION "+time(std::max<int64_t>(0,s.outMs-s.inMs))+" / max3:00";
    c.draw_label(detail,250,421,s.clipReady?accent:muted,12);
    const std::string about=description.empty()?"No description supplied. Source and license links remain under Source / license.":"About: "+description;
    c.draw_label(Canvas::clip_label(about,604,12),18,443,muted,12);
    const std::string browseHint=s.listFocused?"Up / Down: browse    A: open / play / pause    Left / B: controls    R + arrows: seek":
        s.row==2?"Left / Right: Search / Random     A: choose     L: source order / A-Z":
        (s.row==3||s.row==4)?"Right: enter list    Up / Down: controls    L: source order / A-Z":s.status;
    c.draw_label(Canvas::clip_label(s.error.empty()&&!s.busy?browseHint:s.status,604,12),18,461,s.error.empty()?t.textTitle:t.meterHigh,12);
}
} // namespace pt::ui
