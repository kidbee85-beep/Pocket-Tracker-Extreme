#include "sdl/sdl-video.h"
#include "startup-log.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ui/canvas.h"

// The window icon is embedded for desktop Linux (a bare ELF); Windows takes it from the .exe's
// resources and Android has its launcher icon.
#if !defined(_WIN32) && !defined(__ANDROID__)
#include "skin/image.h"
#include "sdl/window_icon.h"
#endif

using pt::ui::Canvas;
using pt::ui::DESIGN_H;
using pt::ui::DESIGN_W;

namespace {
void log_video_startup(const char* phase) {
    std::fprintf(stderr, "startup: video %s\n", phase);
    persist_startup_log();
}

void write_startup_ready_marker() {
    const char* path = std::getenv("PTE_STARTUP_READY_FILE");
    if (!path || !*path) return;

    // The launcher owns this unique per-launch name. Exclusive creation also
    // refuses an existing file or symlink instead of replacing user content.
    std::FILE* marker = std::fopen(path, "wx");
    if (!marker) {
        const int error = errno;
        std::fprintf(stderr, "startup: first-frame marker could not be created: %s\n",
                     std::strerror(error));
        persist_startup_log();
        return;
    }
    constexpr char submitted[] = "frame submitted\n";
    bool written = std::fwrite(submitted, 1, sizeof(submitted) - 1, marker) == sizeof(submitted) - 1;
    if (std::fclose(marker) != 0) written = false;
    if (!written) {
        // This file was just exclusively created by this call, not a preexisting
        // file. Do not leave an incomplete readiness marker for the watchdog.
        std::remove(path);
        log_video_startup("first-frame marker write failed");
    }
}
}

bool SdlVideo::open(const char* title, int windowW, int windowH, bool fullscreen, bool resizable) {
    // Only an explicit opt-in changes the established desktop/Android render path.
    // The compatibility renderer draws into its own stable surface instead of requesting an EGL
    // renderer. The SDL platform can still use its own accelerated transfer for the window surface.
    close();
    const char* videoMode = std::getenv("POCKETTRACKER_VIDEO");
    surfaceMode_ = videoMode && std::strcmp(videoMode, "surface") == 0;
    Uint32 flags = SDL_WINDOW_SHOWN;
    if (fullscreen) flags |= surfaceMode_ ? SDL_WINDOW_FULLSCREEN : SDL_WINDOW_FULLSCREEN_DESKTOP;

    if (surfaceMode_ && fullscreen) {
        SDL_DisplayMode desktop{};
        if (SDL_GetDesktopDisplayMode(0, &desktop) == 0 && desktop.w > 0 && desktop.h > 0) {
            // Match the complete framebuffer, including the bars around the 640x480 tracker.
            windowW = desktop.w;
            windowH = desktop.h;
        }
    }

    if (surfaceMode_ && (windowW <= 0 || windowH <= 0 || windowW > 8192 || windowH > 8192 ||
                         static_cast<uint64_t>(windowW) * windowH > 16u * 1024u * 1024u)) {
        std::fprintf(stderr, "surface video window size is invalid: %dx%d\n", windowW, windowH);
        return false;
    }

    // A windowed host opens at the largest integer scale that fits 90 % of the display (room for a
    // title bar). ⚠️ Derived, because desktop Linux and PortMaster are the same build: a hardcoded
    // 2× would overflow every handheld.
    if (resizable && !surfaceMode_) {
        flags |= SDL_WINDOW_RESIZABLE;

        SDL_DisplayMode desktop{};
        if (SDL_GetDesktopDisplayMode(0, &desktop) == 0 && desktop.w > 0 && desktop.h > 0) {
            const int scale = std::max(1, std::min(desktop.w * 9 / 10 / windowW,
                                                   desktop.h * 9 / 10 / windowH));
            // ASCII only: the console's encoding is not ours.
            std::printf("video:   desktop=%dx%d  opening at %dx (%dx%d), resizable\n", desktop.w,
                        desktop.h, scale, windowW * scale, windowH * scale);
            windowW *= scale;
            windowH *= scale;
        }
    }

    std::fprintf(stderr, "startup: video SDL_CreateWindow begin; size=%dx%d flags=0x%08X\n",
                 windowW, windowH, static_cast<unsigned>(flags));
    persist_startup_log();
    window_ = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, windowW,
                               windowH, flags);
    if (!window_) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }
    log_video_startup("SDL_CreateWindow ready");

    if (surfaceMode_) {
        log_video_startup("window surface acquisition begin");
        SDL_Surface* windowSurface = SDL_GetWindowSurface(window_);
        if (!windowSurface || windowSurface->w <= 0 || windowSurface->h <= 0 ||
            windowSurface->w > 8192 || windowSurface->h > 8192 ||
            static_cast<uint64_t>(windowSurface->w) * windowSurface->h > 16u * 1024u * 1024u) {
            std::fprintf(stderr, "SDL_GetWindowSurface failed or returned invalid dimensions: %s\n",
                         SDL_GetError());
            close();
            return false;
        }
        std::fprintf(stderr, "startup: video window surface ready; size=%dx%d pitch=%d format=%s\n",
                     windowSurface->w, windowSurface->h, windowSurface->pitch,
                     SDL_GetPixelFormatName(windowSurface->format->format));
        persist_startup_log();
        softwareSurface_ = SDL_CreateRGBSurfaceWithFormat(0, windowSurface->w, windowSurface->h,
                                                          32, SDL_PIXELFORMAT_ARGB8888);
        if (!softwareSurface_) {
            std::fprintf(stderr, "surface video backing allocation failed: %s\n", SDL_GetError());
            close();
            return false;
        }
        SDL_SetSurfaceBlendMode(softwareSurface_, SDL_BLENDMODE_NONE);
        log_video_startup("SDL_CreateSoftwareRenderer begin");
        renderer_ = SDL_CreateSoftwareRenderer(softwareSurface_);
        if (!renderer_) {
            std::fprintf(stderr, "SDL_CreateSoftwareRenderer failed: %s\n", SDL_GetError());
            close();
            return false;
        }
        log_video_startup("SDL_CreateSoftwareRenderer ready");
    }

#if !defined(_WIN32) && !defined(__ANDROID__)
    // SDL_SetWindowIcon copies the surface. image.h's 0xAARRGGBB is ARGB8888 on these little-endian
    // targets, so the surface wraps the buffer as is. A failure keeps SDL's default icon.
    if (ptshell::Image icon = ptshell::decode_png(kWindowIconPng, kWindowIconPngLen); icon.ok()) {
        SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormatFrom(
            icon.pixels.data(), icon.width, icon.height, 32, icon.width * 4, SDL_PIXELFORMAT_ARGB8888);
        if (surf) {
            SDL_SetWindowIcon(window_, surf);
            SDL_FreeSurface(surf);
            // Always printed: a failed icon looks exactly like one never attempted.
            std::printf("video:   window icon set (%dx%d embedded)\n", icon.width, icon.height);
        } else {
            std::printf("video:   window icon: SDL_CreateRGBSurfaceWithFormatFrom failed: %s\n",
                        SDL_GetError());
        }
    } else {
        std::printf("video:   window icon: embedded PNG did not decode - keeping SDL's default\n");
    }
#endif

    // ⚠️ SDL_RENDERER_ACCELERATED means "require": with no GPU driver SDL_CreateRenderer fails
    // outright. So try accelerated + vsync, then accelerated, then software — enough for one blit.
    struct Attempt {
        Uint32      flags;
        const char* what;
    };
    const Attempt attempts[] = {
        {SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC, "accelerated + vsync"},
        {SDL_RENDERER_ACCELERATED, "accelerated, no vsync"},
        {SDL_RENDERER_SOFTWARE, "software"},
    };
    for (const Attempt& a : attempts) {
        if (surfaceMode_) break;
        std::fprintf(stderr, "startup: video SDL_CreateRenderer begin; attempt=%s flags=0x%08X\n",
                     a.what, static_cast<unsigned>(a.flags));
        persist_startup_log();
        renderer_ = SDL_CreateRenderer(window_, -1, a.flags);
        if (renderer_) {
            std::fprintf(stderr, "startup: video SDL_CreateRenderer ready; attempt=%s\n", a.what);
            persist_startup_log();
            std::printf("video:   %s\n", a.what);
            break;
        }
        std::fprintf(stderr, "startup: video SDL_CreateRenderer failed; attempt=%s error=%s\n",
                     a.what, SDL_GetError());
        persist_startup_log();
    }
    if (!renderer_) {
        std::fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        close();
        return false;
    }

    // Ask the renderer: attempt 1 can succeed on a driver that quietly ignores vsync.
    SDL_RendererInfo info{};
    vsync_ = (SDL_GetRendererInfo(renderer_, &info) == 0) && !surfaceMode_ &&
             ((info.flags & SDL_RENDERER_PRESENTVSYNC) != 0);
    std::fprintf(stderr, "startup: video selected renderer=%s\n", info.name ? info.name : "unknown");
    persist_startup_log();

    log_video_startup("streaming texture creation begin");
    if (!create_texture()) { close(); return false; }
    log_video_startup("streaming texture ready");
    describe();
    return true;
}

// What the display actually is, one line, always on: driver, panel, output, frame rect (smaller than
// the output = the letterbox). The shell asks for a 640×480 window; on any other panel that is a guess.
void SdlVideo::describe() const {
    int outW = 0, outH = 0;
    SDL_GetRendererOutputSize(renderer_, &outW, &outH);

    SDL_DisplayMode mode{};
    const bool haveMode = SDL_GetCurrentDisplayMode(SDL_GetWindowDisplayIndex(window_), &mode) == 0;

    const SDL_Rect d = dest_rect();
    std::printf("video:   driver=%s  panel=%dx%d@%dHz  output=%dx%d  frame=%dx%d at %d,%d  %s  %s\n",
                SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "?",
                haveMode ? mode.w : 0, haveMode ? mode.h : 0, haveMode ? mode.refresh_rate : 0, outW,
                outH, d.w, d.h, d.x, d.y, scaling_ == ScalingMode::FIT ? "FIT" : "INTEGER",
                vsync_ ? "vsync" : "SELF-PACED");

    // Bars are expected on a panel that is no whole multiple of 640x480; a frame larger than the
    // output means the edges of the UI are gone.
    if (d.w > outW || d.h > outH) {
        std::printf("video:   WARNING: the frame is LARGER than the display - edges are cut off\n");
    } else if (d.w < outW || d.h < outH) {
        std::printf("video:   letterbox: %dpx horizontal, %dpx vertical bars\n", (outW - d.w) / 2,
                    (outH - d.h) / 2);
    }
}

bool SdlVideo::create_texture() {
    if (texture_) SDL_DestroyTexture(texture_);

    // ⚠️ SDL2 reads the filter hint when the texture is created, so a mode change recreates it.
    // Nearest keeps INTEGER pixel-perfect; linear keeps FIT's fractional stretch even.
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, scaling_ == ScalingMode::FIT ? "1" : "0");

    // ARGB8888 is Canvas's uint32 layout on little-endian targets: the upload is a straight memcpy.
    texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                 DESIGN_W, DESIGN_H);
    if (!texture_) {
        std::fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        return false;
    }
    return true;
}

void SdlVideo::set_scaling(ScalingMode m) {
    if (m == scaling_) return;
    const ScalingMode was = scaling_;
    scaling_              = m;
    create_texture();

    // Once per change. The rect is printed beside the mode, so "the setting does nothing" and "the
    // window is wrong" can be told apart in a log.
    const SDL_Rect d = dest_rect();
    std::printf("video:   scaling %s -> %s   frame=%dx%d at %d,%d\n",
                was == ScalingMode::FIT ? "FIT" : "INTEGER",
                scaling_ == ScalingMode::FIT ? "FIT" : "INTEGER", d.w, d.h, d.x, d.y);
    std::fflush(stdout);
}

void SdlVideo::close() {
    if (texture_) SDL_DestroyTexture(texture_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (softwareSurface_) SDL_FreeSurface(softwareSurface_);
    if (window_) SDL_DestroyWindow(window_);
    texture_  = nullptr;
    renderer_ = nullptr;
    softwareSurface_ = nullptr;
    window_   = nullptr;
    surfaceMode_ = false;
    vsync_ = false;
    haveLast_ = false;
    lastOutW_ = lastOutH_ = 0;
    firstUploadAttempted_ = firstUploadFailureLogged_ = firstPresentReturned_ = false;
    presentationFailureLogged_ = false;
}

SDL_Rect SdlVideo::dest_rect() const {
    int outW = 0, outH = 0;
    SDL_GetRendererOutputSize(renderer_, &outW, &outH);

    if (scaling_ == ScalingMode::FIT) {
        // FIT keeps the aspect: one factor, the binding axis's. ⚠️ Integer arithmetic — a float
        // scale can land at 959.99997 and leave a 1px bar on an exact multiple.
        int w, h;
        if (outW * DESIGN_H >= outH * DESIGN_W) {  // window wider than 4:3 → height binds
            h = outH;
            w = outH * DESIGN_W / DESIGN_H;
        } else {                                   // taller than 4:3 → width binds
            w = outW;
            h = outW * DESIGN_H / DESIGN_W;
        }
        return SDL_Rect{(outW - w) / 2, frame_top(outH, h), w, h};
    }

    // INTEGER: the largest whole multiple that fits, at least 1 (a desktop window can be dragged smaller).
    const int scale = std::max(1, std::min(outW / DESIGN_W, outH / DESIGN_H));
    const int w     = DESIGN_W * scale;
    const int h     = DESIGN_H * scale;
    return SDL_Rect{(outW - w) / 2, frame_top(outH, h), w, h};
}

// The frame's top edge, for both scaling modes. Anchored = centred in the top half (clear of the
// status bar, near where the portrait skin puts it), clamped at 0.
int SdlVideo::frame_top(int outH, int h) const {
    if (!topAnchor_) return (outH - h) / 2;
    return std::max(0, (outH / 2 - h) / 2);
}

void SdlVideo::invalidate_backbuffer(bool texture_lost) {
    // A device reset (GL context loss) takes the texture with it; a re-expose does not.
    if (texture_lost) create_texture();

    haveLast_ = false;
}

bool SdlVideo::present(const Canvas& canvas, uint32_t letterboxArgb,
                       const std::function<void(SDL_Renderer*)>& overlay, uint64_t overlaySig,
                       uint32_t modalScrimArgb) {
    // ⚠️ Re-describe when the output changes — on Android the surface reaches its size a few frames
    // after the window opens. Above the pixel gate: an INTEGER rect can absorb a small change and the
    // skip would hide it. The portrait skin logs its own geometry from app.cpp.
    int outW = 0, outH = 0;
    SDL_GetRendererOutputSize(renderer_, &outW, &outH);
    if (outW != lastOutW_ || outH != lastOutH_) {
        if (lastOutW_ != 0) describe();   // open() has just printed it
        lastOutW_ = outW;
        lastOutH_ = outH;
    }

    // The modal scrim, if any, dims the whole output around the frame.
    return present_impl(canvas, letterboxArgb, dest_rect(), {}, overlay, overlaySig, modalScrimArgb,
                        SDL_Rect{0, 0, outW, outH});
}

bool SdlVideo::present_skinned(const Canvas& canvas, uint32_t clearArgb, const SDL_Rect& frameDest,
                               const std::function<void(SDL_Renderer*)>& underlay,
                               const std::function<void(SDL_Renderer*)>& overlay, uint64_t overlaySig,
                               uint32_t modalScrimArgb, const SDL_Rect& scrimBounds) {
    // The scrim is bounded to the bezel's inner glass, so it never dims the casing or the buttons.
    return present_impl(canvas, clearArgb, frameDest, underlay, overlay, overlaySig, modalScrimArgb,
                        scrimBounds);
}

bool SdlVideo::presentation_failed(const char* operation) {
    if (!presentationFailureLogged_) {
        std::fprintf(stderr, "video presentation failed (%s): %s\n", operation, SDL_GetError());
        persist_startup_log();
        presentationFailureLogged_ = true;
    }
    haveLast_ = false;
    return false;
}

bool SdlVideo::present_impl(const Canvas& canvas, uint32_t clearArgb, const SDL_Rect& dest,
                            const std::function<void(SDL_Renderer*)>& underlay,
                            const std::function<void(SDL_Renderer*)>& overlay, uint64_t overlaySig,
                            uint32_t modalScrimArgb, const SDL_Rect& scrimBounds) {
    // The native app presents on one UI thread. These markers run only once per
    // process and distinguish a blocked upload from a blocked display flip.
    // ── Don't present a frame that is already on screen ──────────────────────────────────────────
    //
    // app.cpp decides when not to draw; this, when a drawn frame is not worth sending — a net no
    // forgotten state field can fool. ⚠️ Everything outside the canvas that changes the screen is
    // compared too.
    const size_t n = static_cast<size_t>(DESIGN_W) * DESIGN_H;
    if (haveLast_ && clearArgb == lastLetterbox_ && overlaySig == lastOverlaySig_ &&
        modalScrimArgb == lastModalScrim_ &&
        dest.x == lastDest_.x && dest.y == lastDest_.y && dest.w == lastDest_.w &&
        dest.h == lastDest_.h &&
        std::memcmp(lastFrame_.data(), canvas.pixels(), n * sizeof(uint32_t)) == 0) {
        return false;
    }

    // The whole frame every time: 1.2 MB costs microseconds, and Canvas tracks no dirty rects.
    void* dst   = nullptr;
    int   pitch = 0;
    if (!firstUploadAttempted_) {
        log_video_startup("first frame texture upload begin");
        firstUploadAttempted_ = true;
    }
    if (SDL_LockTexture(texture_, nullptr, &dst, &pitch) != 0) {
        if (!firstPresentReturned_ && !firstUploadFailureLogged_) {
            std::fprintf(stderr, "startup: video first texture upload failed: %s\n", SDL_GetError());
            persist_startup_log();
            firstUploadFailureLogged_ = true;
        }
        return false;
    }

    const auto* src = reinterpret_cast<const uint8_t*>(canvas.pixels());
    const int   row = canvas.pitch_bytes();
    if (pitch == row) {
        SDL_memcpy(dst, src, static_cast<size_t>(row) * DESIGN_H);
    } else {
        // A driver may hand back a padded pitch.
        auto* out = reinterpret_cast<uint8_t*>(dst);
        for (int y = 0; y < DESIGN_H; ++y) {
            SDL_memcpy(out + static_cast<size_t>(y) * pitch, src + static_cast<size_t>(y) * row,
                       static_cast<size_t>(row));
        }
    }
    SDL_UnlockTexture(texture_);

    // The bars in the live theme's background (or the casing), so the window reads as one surface.
    SDL_SetRenderDrawColor(renderer_, static_cast<Uint8>((clearArgb >> 16) & 0xFF),
                           static_cast<Uint8>((clearArgb >> 8) & 0xFF),
                           static_cast<Uint8>(clearArgb & 0xFF), 255);
    if (SDL_RenderClear(renderer_) != 0) return presentation_failed("clear");

    // After the clear, before the frame: the portrait skin's chrome. A no-op on the centred path.
    if (underlay) underlay(renderer_);

    // The modal scrim fills the four regions around `dest` inside `scrimBounds` — never the frame,
    // whose own modal already dims it. Landscape passes the whole output, the portrait skin its glass.
    if (modalScrimArgb != 0) {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, static_cast<Uint8>((modalScrimArgb >> 16) & 0xFF),
                               static_cast<Uint8>((modalScrimArgb >> 8) & 0xFF),
                               static_cast<Uint8>(modalScrimArgb & 0xFF),
                               static_cast<Uint8>((modalScrimArgb >> 24) & 0xFF));
        // Frame edges clamped into the bounds, so no bar gets a negative size.
        const int bx = scrimBounds.x, by = scrimBounds.y;
        const int br = scrimBounds.x + scrimBounds.w, bb = scrimBounds.y + scrimBounds.h;
        const int fx = std::max(bx, std::min(br, dest.x));
        const int fy = std::max(by, std::min(bb, dest.y));
        const int fr = std::max(bx, std::min(br, dest.x + dest.w));
        const int fb = std::max(by, std::min(bb, dest.y + dest.h));
        const SDL_Rect bars[4] = {
            {bx, by, br - bx, fy - by},   // above the frame
            {bx, fb, br - bx, bb - fb},   // below the frame
            {bx, fy, fx - bx, fb - fy},   // left of the frame
            {fr, fy, br - fr, fb - fy},   // right of the frame
        };
        SDL_RenderFillRects(renderer_, bars, 4);
    }

    if (SDL_RenderCopy(renderer_, texture_, nullptr, &dest) != 0)
        return presentation_failed("frame copy");

    // After the frame, before the flip: the touch panels or the portrait button cluster.
    if (overlay) overlay(renderer_);

    if (!firstPresentReturned_) log_video_startup("first SDL_RenderPresent begin");
    SDL_RenderPresent(renderer_);
    if (surfaceMode_) {
        if (!firstPresentReturned_) log_video_startup("first window surface update begin");
        // SDL owns this target and its pitch/pixel format. Never retain or free it: a resize or mode
        // switch can invalidate it. Blitting also converts our ARGB pixels to the framebuffer format.
        SDL_Surface* windowSurface = SDL_GetWindowSurface(window_);
        if (!windowSurface || SDL_BlitScaled(softwareSurface_, nullptr, windowSurface, nullptr) != 0 ||
            SDL_UpdateWindowSurface(window_) != 0) {
            return presentation_failed("window surface update");
        }
        if (!firstPresentReturned_) log_video_startup("first window surface update returned");
    }
    presentationFailureLogged_ = false;
    if (!firstPresentReturned_) {
        // SDL2 returns no presentation status: this confirms that the call
        // returned, not that the physical panel has been visually verified.
        log_video_startup("first SDL_RenderPresent returned");
        write_startup_ready_marker();
        firstPresentReturned_ = true;
    }

    // What is now on screen — taken after the present, so a failed present cannot make it lie.
    if (lastFrame_.size() != n) lastFrame_.resize(n);
    std::memcpy(lastFrame_.data(), canvas.pixels(), n * sizeof(uint32_t));
    lastLetterbox_  = clearArgb;
    lastDest_       = dest;
    lastOverlaySig_ = overlaySig;
    lastModalScrim_ = modalScrimArgb;
    haveLast_       = true;

    // ⚠️ Nothing here paces: the app loop owns both rates.
    return true;
}
