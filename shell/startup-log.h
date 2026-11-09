#pragma once

#include <cstdio>
#include <cstdlib>

#if defined(__linux__)
#include <sys/stat.h>
#include <unistd.h>
#endif

// Only the handheld launcher opts in. A hard reset can otherwise lose even
// unbuffered messages in the kernel's dirty-page cache. Never sync a pipe/device
// and never put this in the audio callback or the regular rendering loop.
inline void persist_startup_log() {
    std::fflush(stderr);
#if defined(__linux__)
    const char* enabled = std::getenv("POCKETTRACKER_SYNC_STARTUP");
    if (!enabled || enabled[0] != '1' || enabled[1] != '\0') return;
    struct stat st{};
    const int fd = fileno(stderr);
    if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode)) {
        if (fsync(fd) != 0) {
            std::fputs("startup: WARNING: log could not be synchronized\n", stderr);
            std::fflush(stderr);
        }
    }
#endif
}
