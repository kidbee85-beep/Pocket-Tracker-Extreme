#pragma once
#include <cstdio>
#include <string>
#include <system_error>

namespace pt::ui::live_resample_files {
// Both temporary creation and final publication refuse an existing entry, including a symlink.
std::FILE* create_exclusive(const std::string& path);
enum class PublishMode { AUTO, ATOMIC_RENAME, EXCLUSIVE_COPY };
// Successful publication leaves a complete destination; the caller owns/removes its temporary.
// EXCLUSIVE_COPY also exercises the safe fallback used when atomic filesystem operations fail.
// A failed fallback may leave its own incomplete new destination. It never replaces an old file.
bool publish_no_replace(const std::string& temp,const std::string& destination,
                        std::error_code& error,PublishMode mode=PublishMode::AUTO);
}
