// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "studio/rack.h"
#include "songcore/model.h"
#include "songcore/project_io.h"
#include <array>
#include <string>

namespace pt::ui {
inline constexpr const char* STUDIO_FX_FILE_EXT="ptfx";
inline constexpr const char* STUDIO_MASTER_FILE_EXT="ptmaster";
inline bool studio_preset_key(bool effect,int key){return key==studio::ENABLE||(effect?key<=studio::FEEDBACK:key>=studio::DRIVE);}
inline void apply_studio_preset(bool effect,const std::array<int,studio::COUNT>& values,songcore::Project& project){
 for(int i=0;i<studio::COUNT;++i)if(studio_preset_key(effect,i))project.studioMaster[i]=values[i];
}
// The existing custom-slot version1 format is retained. File imports require an explicit kind;
// custom slots can still read older files that omitted it. Parse atomically into a temporary.
inline bool parse_studio_preset(bool effect,const std::string& text,std::array<int,studio::COUNT>& out,bool requireKind=true){
 const auto j=songcore::json::parse(text,nullptr,false);
 if(!j.is_object()||!j.contains("version")||!j["version"].is_number_integer()||j["version"]!=1||!j.contains("master"))return false;
 if(requireKind&&!j.contains("kind"))return false;
 if(j.contains("kind")&&j["kind"]!=(effect?"effects":"master"))return false;
 const auto& values=j["master"];if(!values.is_array()||values.size()!=studio::COUNT)return false;
 std::array<int,studio::COUNT> checked{};
 for(int i=0;i<studio::COUNT;++i){
  if(!values[i].is_number_integer())return false;
  // Reject enormous unsigned JSON numbers before any signed conversion.
  if(values[i]<0||values[i]>255)return false;checked[i]=values[i].get<int>();
 }
 if(checked[studio::ENABLE]>1||checked[studio::FX]>19)return false;
 out=checked;return true;
}
inline std::string serialize_studio_preset(bool effect,const std::array<int,studio::COUNT>& values,const std::string& name={}){
 songcore::json j={{"version",1},{"kind",effect?"effects":"master"},{"master",values}};
 if(!name.empty())j["name"]=name;
 return j.dump(2);
}
} // namespace pt::ui
