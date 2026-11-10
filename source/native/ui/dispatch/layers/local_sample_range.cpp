// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/dispatch/dispatch_common.h"
#include "ui/local_sample_range.h"
#include "ui/source_sampler.h"
#include "ui/std_filesystem.h"
#include "common/load_progress.h"
#include "common/platform_memory.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <vector>

namespace pt::ui {
namespace {
int64_t import_frame_budget(const pt::LocalWavSnapshot& s){
    const int64_t mib=1024*1024,total=pt::total_memory_bytes(),available=pt::available_memory_bytes();
    int64_t bytes=(total>0&&total<=256*mib)?8*mib:64*mib;
    // Leave system/UI headroom and room for loaded PCM, Undo and one edit result.
    if(available>0)bytes=std::min(bytes,std::max<int64_t>(64*1024,(available-8*mib)/3));
    return std::max<int64_t>(1,bytes/(4*std::max(1,s.channels)));
}
std::vector<int64_t> lengths(const LocalSampleRangeState& v){
    std::vector<int64_t> result;
    const auto max=std::min(v.maxImportFrames,v.source.totalFrames-v.inFrame);
    for(int seconds:{1,2,5,10,15,20,30,40,60,90,120,180}){
        const int64_t frames=int64_t(seconds)*v.source.rate;
        if(frames<=max)result.push_back(frames);
    }
    if(max>0&&(result.empty()||result.back()!=max))result.push_back(max);
    return result;
}
std::string new_clip_path(FileSystem& fs,const std::string& directory,const std::string& title){
    if(directory.empty())return {};
    std::string base=safe_file_name(path_stem(title)).substr(0,36);
    if(base.empty())base="Source";
    const auto stamp=std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    for(int suffix=0;suffix<10000;++suffix){
        const auto path=directory+"/"+base+"_Crop_"+std::to_string(stamp)+"_"+std::to_string(suffix)+".wav";
        if(!fs.file_exists(path))return path;
    }
    return {};
}
}
bool InputDispatcher::open_large_sample(const std::string& path,const std::string& title,bool autoPlay,bool onlyIfNeeded){
    if(!host_.has_local_wav_source())return false;
    auto& source=host_.local_wav_source();
    if(!source.open(path)){
        set_status(s_.fileBrowser,source.snapshot().error.empty()?"WAV PREVIEW UNAVAILABLE":source.snapshot().error,false);
        return true;
    }
    if(onlyIfNeeded){
        const auto metadata=source.snapshot();
        const int64_t needed=metadata.totalFrames*metadata.channels*4,available=pt::available_memory_bytes();
        const int64_t editBudget=std::max<int64_t>(0,(available-8ll*1024*1024)/3);
        if(needed<16ll*1024*1024&&(available<=0||needed<=editBudget)){
            source.close();return false; // Ordinary short samples keep the original fast load/transport.
        }
    }
    host_.stop();host_.stop_preview(true);host_.clear_previews();
    if(s_.sourceSampler.backend)source_sampler_stop(s_.sourceSampler);
    // Clearing the scratch slots also closes local preview. Reopen after releasing that RAM.
    if(!source.open(path)){
        set_status(s_.fileBrowser,source.snapshot().error.empty()?"WAV PREVIEW UNAVAILABLE":source.snapshot().error,false);
        return true;
    }
    LocalSampleRangeState next;next.isOpen=true;next.path=path;next.title=title;next.source=source.snapshot();
    next.targetInstrument=s_.browserPurpose==AppState::BrowserPurpose::LOAD_SAMPLE_EDITOR?
        s_.sampleEditor.instrumentId:s_.currentInstrument;
    next.targetInstrument=std::clamp(next.targetInstrument,0,int(host_.project().instruments.size())-1);
    next.previousAutoName=instrument_auto_name(host_.project(),next.targetInstrument);
    next.maxImportFrames=import_frame_budget(next.source);
    next.outFrame=std::min({next.source.totalFrames,next.maxImportFrames,int64_t(next.source.rate)*10});
    next.message="Preview the full source from storage; mark a smaller section for the editor.";
    s_.localSampleRange=std::move(next);s_.helpFull=false;s_.helpOpen=false;
    if(autoPlay)source.play();
    poll_local_sample_range();
    return true;
}
void InputDispatcher::poll_local_sample_range(){
    auto& v=s_.localSampleRange;if(!v.isOpen||!host_.has_local_wav_source())return;
    v.source=host_.local_wav_source().snapshot();
    if(!v.source.error.empty())v.message=v.source.error;
    if(!v.source.open&&!s_.loading.running){
        v.isOpen=false;set_status(s_.fileBrowser,v.message.empty()?"SOURCE CLOSED":v.message,false);
    }
}
void InputDispatcher::local_sample_range_edit(int direction,int milliseconds,bool fine){
    auto& v=s_.localSampleRange;auto& source=host_.local_wav_source();
    const int64_t step=std::max<int64_t>(1,int64_t(v.source.rate)*milliseconds/1000);
    if(v.row==0)source.seek(std::clamp<int64_t>(v.source.positionFrame+direction*step,0,v.source.totalFrames));
    else if(v.row==1)v.inFrame=std::clamp<int64_t>(v.inFrame+direction*step,0,std::max<int64_t>(0,v.outFrame-1));
    else if(v.row==2)v.outFrame=std::clamp<int64_t>(v.outFrame+direction*step,v.inFrame+1,v.source.totalFrames);
    else if(v.row==3){
        const auto choices=lengths(v);if(choices.empty())return;
        const auto length=v.outFrame-v.inFrame;
        auto at=std::lower_bound(choices.begin(),choices.end(),length);
        int index=at==choices.end()?int(choices.size())-1:int(at-choices.begin());
        if(at!=choices.end()&&*at==length)index+=direction;
        else if(direction<0)--index;
        index=std::clamp(index,0,int(choices.size())-1);
        v.outFrame=v.inFrame+choices[index];
    }
    (void)fine;
    v.message.clear();poll_local_sample_range();
}
void InputDispatcher::import_local_sample_range(bool wholeFile){
    auto& v=s_.localSampleRange;auto& source=host_.local_wav_source();
    if(!v.source.open)return;
    const auto first=wholeFile?int64_t(0):v.inFrame,end=wholeFile?v.source.totalFrames:v.outFrame;
    v.maxImportFrames=std::min(v.maxImportFrames,import_frame_budget(v.source));
    if(end<=first){v.message="Set OUT after IN.";return;}
    if(!wholeFile&&end-first>v.maxImportFrames){
        v.message="Choose a shorter excerpt: editor limit "+local_sample_time(v.maxImportFrames,v.source.rate)+". Full preview is still available.";return;
    }
    if(wholeFile){
        const int64_t needed=v.source.totalFrames*v.source.channels*4,available=pt::available_memory_bytes();
        const int64_t budget=pt::load_budget_bytes();
        if((budget>0&&needed>budget)||(available>0&&needed>std::max<int64_t>(0,(available-8ll*1024*1024)/3))){
            v.message="Full file needs more editing RAM. Use Crop to editor; the source stays on the card.";return;
        }
    }
    const int target=v.targetInstrument;const auto original=v.path,title=v.title,previousName=v.previousAutoName;
    const auto oldPosition=v.source.positionFrame;
    source.pause();
    const LoadScope loading(*this,now_ms_,wholeFile?"Loading complete WAV":"Preparing selected WAV excerpt");
    std::string imported=original;
    if(!wholeFile){
        const auto library=fs_.make_app_folder("Samples/Chops");
        const bool virtualLibrary=library.find("://")!=std::string::npos;
        std::filesystem::path directory;
        if(!library.empty()&&!virtualLibrary)directory=library;
        else directory=std::filesystem::path(fs_.settings_path()).parent_path()/".long-wav";
        std::error_code error;std::filesystem::create_directories(directory,error);
        if(error){v.message="Cannot create a writable folder for the new excerpt.";return;}
        imported=new_clip_path(fs_,directory.string(),title);
        if(imported.empty()||!source.export_region(imported,first,end)){
            v.message=pt::load_cancelled()?"Excerpt cancelled; original kept":source.snapshot().error;
            if(v.message.empty())v.message="Could not save this excerpt. Original kept.";
            return;
        }
        if(virtualLibrary){
            const auto final=new_clip_path(fs_,library,title);
            if(!final.empty()&&fs_.copy_file(imported,final)){
                fs_.delete_path(imported);imported=final;
            }
            // A failed external copy still leaves the complete excerpt in app-owned storage.
        }
    }
    if(!host_.load_sample(target,imported)){
        source.open(original);source.seek(oldPosition);v.source=source.snapshot();
        v.message=host_.last_load_cancelled()?"Load cancelled; original instrument kept":
            "Excerpt saved; instrument kept. Free some RAM, then load the excerpt.";
        return;
    }
    host_.set_instrument_type(target,songcore::InstrumentType::SAMPLER);
    auto& ins=host_.edit_project().instruments[size_t(target)];
    if(songcore::instrument_has_default_name(ins)||(!previousName.empty()&&ins.name==previousName))
        ins.name=adopted_name(path_stem(title));
    v.isOpen=false;source.close();
    close_file_browser();s_.currentInstrument=target;mark_modified();open_sample_editor();
    set_status(s_,wholeFile?"WAV LOADED":"EXCERPT SAVED / READY TO SLICE",true);
}
GestureResult InputDispatcher::local_sample_range_layer(Gesture g){
    auto& v=s_.localSampleRange;auto& source=host_.local_wav_source();
    if(g==Gesture::B){source.close();v.isOpen=false;return GestureResult::TAKEN;}
    if(g==Gesture::SELECT){v.help=!v.help;return GestureResult::TAKEN;}
    if(v.help)return GestureResult::TAKEN;
    if(g==Gesture::START||g==Gesture::A_START){
        if(v.source.playing)source.pause();else source.play();
    }else if(g==Gesture::DPAD_UP)v.row=std::max(0,v.row-1);
    else if(g==Gesture::DPAD_DOWN)v.row=std::min(6,v.row+1);
    else if(g==Gesture::DPAD_LEFT||g==Gesture::DPAD_RIGHT)local_sample_range_edit(g==Gesture::DPAD_RIGHT?1:-1,1000);
    else if(g==Gesture::A_LEFT||g==Gesture::A_RIGHT)local_sample_range_edit(g==Gesture::A_RIGHT?1:-1,10,true);
    else if(g==Gesture::A_UP||g==Gesture::A_DOWN)local_sample_range_edit(g==Gesture::A_UP?1:-1,10000);
    else if(g==Gesture::R_LEFT||g==Gesture::R_RIGHT)
        source.seek(v.source.positionFrame+(g==Gesture::R_RIGHT?1:-1)*int64_t(v.source.rate)*5);
    else if(g==Gesture::A){
        const auto position=std::clamp<int64_t>(v.source.positionFrame,0,v.source.totalFrames);
        if(v.row==1){
            const auto length=std::max<int64_t>(1,v.outFrame-v.inFrame);
            v.inFrame=std::min(position,v.source.totalFrames-1);
            if(v.outFrame<=v.inFrame)v.outFrame=std::min(v.source.totalFrames,v.inFrame+length);
        }else if(v.row==2)v.outFrame=std::max(v.inFrame+1,position);
        else if(v.row==3)local_sample_range_edit(1,1000);
        else if(v.row==5)import_local_sample_range(false);
        else if(v.row==6)import_local_sample_range(true);
        else if(v.source.playing)source.pause();else source.play();
    }
    poll_local_sample_range();
    return GestureResult::TAKEN; // STOP_PREVIEW and unrelated chords never leak behind the modal.
}
}
