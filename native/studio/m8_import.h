#pragma once
#include "songcore/model.h"
#include <functional>
#include <string>
#include <vector>
namespace studio {
struct M8Import {
 bool ok=false;
 songcore::Project project;
 std::string report,error;
 std::vector<std::string> summary;
};
// Conservative v6 supported-data reader. Unsupported sounds/commands are omitted
// with a report. The original bytes are retained; the source is never written.
M8Import import_m8(const std::string&,const std::function<std::string(const std::string&)>& samplePath);
}
