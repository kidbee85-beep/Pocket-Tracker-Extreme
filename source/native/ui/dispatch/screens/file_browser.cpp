// The file browser — its buttons, opening, the cursor, loading, the file clipboard.

#include "ui/dispatch/dispatch_common.h"
#include "ui/studio.h"
#include "studio/m8_import.h"
#include "songcore/project_io.h"

#include "ui/fx_preset_io.h"
#include "ui/studio_preset_io.h"
#include "ui/sample_granular_presets.h"
#include "ui/groove_io.h"
#include "ui/scale_io.h"
#include "ui/std_filesystem.h"   // path_name / path_stem / path_extension / to_lower
#include "ui/theme_io.h"
#include "common/load_progress.h"       // load_cancelled
#include "common/byte_source.h"         // pt_path_is_uri: native and granted storage stay distinct
#include "common/platform_memory.h"

#include <algorithm>
#include <string>
#include <vector>

namespace pt::ui {

namespace {

std::string file_count(size_t n) { return std::to_string(n) + (n == 1 ? " FILE" : " FILES"); }
bool probe_wav_size(const BrowserItem& item){
    // PCM8 can expand fourfold; inspect metadata before a potentially unsafe full load.
    // Tiny drum files keep the direct path unless the device is already short of RAM.
    if(item.size<=0||item.size>=4ll*1024*1024)return true;
    const auto available=pt::available_memory_bytes();
    return available>0&&item.size>std::max<int64_t>(64*1024,(available-8ll*1024*1024)/3)/4;
}
bool audio_source_browser(AppState::BrowserPurpose p){
    return p==AppState::BrowserPurpose::LOAD_SOURCE||p==AppState::BrowserPurpose::LOAD_SAMPLE_EDITOR||p==AppState::BrowserPurpose::LOAD_GRANULAR_SAMPLE;
}

void end_selection(FileBrowserState& b) {
    b.selectionMode   = false;
    b.selectionAnchor = -1;
}

}  // namespace

// ─── The buttons ─────────────────────────────────────────────────────────────────────────────────

GestureResult InputDispatcher::file_browser_screen(Gesture g) {
    FileBrowserState& fb = s_.fileBrowser;
    if (fb.mode == BrowserMode::DELETE) {
        switch (g) {
            case Gesture::A: browser_confirm(); break;
            case Gesture::B: browser_back(); break;
            case Gesture::STOP_PREVIEW: host_.stop_preview(); break;
            default: break;
        }
        return GestureResult::TAKEN;
    }
    switch (g) {
        // UP/DOWN move a row; LEFT/RIGHT page by a screenful.
        case Gesture::DPAD_UP:    browser_move_cursor(-1, /*page=*/false); break;
        case Gesture::DPAD_DOWN:  browser_move_cursor(+1, /*page=*/false); break;
        case Gesture::DPAD_LEFT:  browser_move_cursor(-BROWSER_VISIBLE_ROWS, /*page=*/true); break;
        case Gesture::DPAD_RIGHT: browser_move_cursor(+BROWSER_VISIBLE_ROWS, /*page=*/true); break;

        case Gesture::A:     browser_confirm(); break;
        case Gesture::B:     browser_back(); break;
        case Gesture::START: browser_audition(); break;

        // An audition rings until stopped; scrolling must not stack them.
        case Gesture::STOP_PREVIEW: host_.stop_preview(); break;

        // Help is the shared path's, which knows the panel has no room here.
        case Gesture::SELECT: return GestureResult::PASS;

        // ⚠️ Not navigation: the browser is not a grid cell. Down a directory is A on a folder.
        case Gesture::R_UP:   browser_cycle_sort(+1); break;
        case Gesture::R_DOWN: browser_cycle_sort(-1); break;
        case Gesture::R_LEFT: navigate_to_parent(fb, fs_); break;

        case Gesture::L_B: browser_select(); break;
        case Gesture::L_A: browser_cut_or_paste(); break;
        case Gesture::L_R: end_selection(fb); break;

        case Gesture::SELECT_A: browser_rename(); break;
        case Gesture::SELECT_B: browser_arm_delete(); break;
        case Gesture::SELECT_R: browser_new_folder(); break;

        default: break;   // every other gesture means nothing here
    }
    return GestureResult::TAKEN;
}

void InputDispatcher::browser_back() {
    FileBrowserState& fb = s_.fileBrowser;

    // B is the NO: it disarms any mode rather than leaving, so an accidental SELECT+A/B is harmless.
    if (fb.mode != BrowserMode::NORMAL) {
        fb.mode = BrowserMode::NORMAL;
        fb.pendingDelete = {};
        return;
    }

    // Inside a file selection, B COPIES it — as over a grid.
    if (fb.selectionMode) {
        std::vector<std::string> files = browser_selected_paths();
        if (!files.empty()) {
            const size_t n = files.size();
            fb.fileClipboard      = std::move(files);
            fb.fileClipboardIsCut = false;
            set_status(fb, "CPY " + file_count(n), true);
        }
        end_selection(fb);
        return;
    }

    close_file_browser(true);
}

void InputDispatcher::browser_audition() {
    // Decoded into slot 255 on the preview lane; every audible extension, from every context.
    const BrowserItem* item = s_.fileBrowser.current();
    if (!item || item->kind != BrowserItem::Kind::FILE) return;

    const std::string ext = to_lower(item->extension);
    const bool audible = std::find(sample_extensions().begin(), sample_extensions().end(), ext) !=
                         sample_extensions().end();
    if (!audible) return;   // a .pti or an .sf2 has no waveform
    if(ext=="wav"&&probe_wav_size(*item)&&open_large_sample(item->path,item->displayName,true,true))return;

    // No song cell behind a file: neutral gain.
    host_.set_preview_track(-1);
    // ⚠️ A full decode (a long mp3 is a real load), so the loading strip and B.
    const LoadScope previewScope(*this, now_ms_, item->displayName);
    if (!host_.preview_file(item->path)) {
        if (host_.last_load_cancelled()) return;   // stopped on purpose
        if(ext=="wav"&&host_.last_load_ran_out_of_memory()&&open_large_sample(item->path,item->displayName,true))return;
        set_status(s_.fileBrowser, "PREVIEW FAILED", false);
    }
}

// ─── Opening and closing the browser ─────────────────────────────────────────────────────────────

std::string InputDispatcher::browser_dir(BrowserDir cat) {
    // The config.json override per category, else the built-in default.
    const std::optional<std::string>* ov = nullptr;
    std::string                       def;
    switch (cat) {
        case BrowserDir::SAMPLES:     ov = &s_.folderConfig.samples;     def = fs_.samples_directory();     break;
        case BrowserDir::SOUNDFONTS:  ov = &s_.folderConfig.soundfonts;  def = fs_.soundfonts_directory();  break;
        case BrowserDir::INSTRUMENTS: ov = &s_.folderConfig.instruments; def = fs_.instruments_directory(); break;
        case BrowserDir::PROJECTS:    ov = &s_.folderConfig.projects;    def = fs_.projects_directory();    break;
        case BrowserDir::THEMES:      ov = &s_.folderConfig.themes;      def = fs_.themes_directory();      break;
    }
    // ⚠️ The whole rule is resolve_browse_dir; inline none of it, or config and sample paths disagree.
    return ov ? resolve_browse_dir(fs_, *ov, def) : def;
}

void InputDispatcher::open_file_browser(AppState::BrowserPurpose purpose, const std::string& directory,
                                        const std::vector<std::string>& extensions, bool sampleDomain) {
    s_.previousScreen = s_.currentScreen;
    s_.browserPurpose = purpose;

    s_.fileBrowser.fileExtensions = extensions;
    s_.fileBrowser.mode           = BrowserMode::NORMAL;
    s_.fileBrowser.pendingDelete  = {};
    s_.fileBrowser.sampleDomain   = sampleDomain || purpose == AppState::BrowserPurpose::LOAD_SAMPLE_EDITOR;
    s_.fileBrowser.sourceBookmarks.clear();
    if(purpose==AppState::BrowserPurpose::LOAD_SOURCE){
        s_.fileBrowser.fileExtensions=instrument_source_extensions();
        auto add=[&](const char* action,const char* title){BrowserItem item;item.kind=BrowserItem::Kind::ACTION;item.path=std::string(SOURCE_BOOKMARK_PREFIX)+action;item.displayName=title;s_.fileBrowser.sourceBookmarks.push_back(std::move(item));};
        add("synth","Synthesizers / Factory Presets");
        add("sample-granular","Sample Granular / PCM Studio");
        add("presets","Instrument Presets (.pti)");
        add("samples","Samples / Audio");
        add("soundfonts","Soundfonts (.sf2 / .sf3)");
        if(host_.project().instruments[s_.currentInstrument].instrumentType==songcore::InstrumentType::EXTERNAL)
            add("midi","External MIDI settings");
    }

    if (s_.fileBrowser.sampleDomain) open_sample_browser_folder(directory);
    else navigate_to_folder(s_.fileBrowser, fs_, directory);

    s_.currentScreen = ScreenType::FILE_BROWSER;
}

void InputDispatcher::remember_sample_browser() {
    const auto& b = s_.fileBrowser;
    if (!b.sampleDomain || b.currentDirectory.empty()) return;
    auto& v = s_.settings;
    v.lastSampleFolder = b.currentDirectory;
    v.lastSampleSelection = b.current() ? b.current()->path : std::string();
    v.lastSampleCursor = b.cursor;
    v.lastSampleScroll = b.scroll;
    v.lastSampleSort = static_cast<int>(b.sortMode);
}

void InputDispatcher::open_sample_browser_folder(const std::string& fallback) {
    auto& b = s_.fileBrowser;
    auto& v = s_.settings;
    b.sampleDomain = true;
    std::string start = fallback;
    bool restore = false;
    if (v.rememberFolder && !v.lastSampleFolder.empty()) {
        // SAF's directory check reads its parent's cached listing. Invalidate before validating:
        // a deleted folder or revoked grant must not remain apparently usable across reopen.
        const auto parent = fs_.parent_path(v.lastSampleFolder);
        if (!parent.empty()) fs_.forget_listing(parent);
        fs_.forget_listing(v.lastSampleFolder);
        const bool sameKind = pt_path_is_uri(v.lastSampleFolder.c_str()) == pt_path_is_uri(fallback.c_str());
        if (sameKind && fs_.is_directory(v.lastSampleFolder)) {
            start = v.lastSampleFolder;
            restore = true;
            b.sortMode = static_cast<FileSortMode>(std::clamp(v.lastSampleSort, 0, FILE_SORT_MODE_COUNT - 1));
        } else {
            v.lastSampleFolder.clear();
            v.lastSampleSelection.clear();
            v.lastSampleCursor = v.lastSampleScroll = 0;
        }
    }
    if (!fs_.is_directory(start)) start = fs_.samples_directory();
    navigate_to_folder(b, fs_, start);
    if (restore) restore_browser_position(b, v.lastSampleSelection, v.lastSampleCursor, v.lastSampleScroll);
}

void InputDispatcher::close_file_browser(bool viaBack) {
    remember_sample_browser();
    // The audition is over; a preview left resident is PCM nothing will play again.
    host_.clear_previews();
    end_selection(s_.fileBrowser);
    s_.currentScreen = s_.previousScreen;
    if (s_.browserPurpose == AppState::BrowserPurpose::LOAD_STUDIO_FX ||
        s_.browserPurpose == AppState::BrowserPurpose::LOAD_STUDIO_MASTER) s_.studioOpen = true;
    if(s_.sampleGranular.browserReturn&&(s_.browserPurpose==AppState::BrowserPurpose::LOAD_GRANULAR_SAMPLE||s_.browserPurpose==AppState::BrowserPurpose::LOAD_GRANULAR_PRESET)){
        if(s_.browserPurpose==AppState::BrowserPurpose::LOAD_GRANULAR_PRESET)s_.sampleGranular.presetDirectory=s_.fileBrowser.currentDirectory;
        if(s_.sampleGranular.browserFromEditor)s_.previousScreen=s_.sampleGranular.browserEditorReturn;
        s_.sampleGranular.browserFromEditor=false;
        s_.sampleGranular.browserReturn=false;s_.sampleGranular.browserInstrument=-1;
        s_.sampleGranular.returnReleaseMask|=viaBack?2u:s_.aHeld?1u:0u;
        s_.studioOpen=true;s_.studioTab=0;s_.sampleGranular.open=true;s_.sampleGranular.nextPollMs=0;
    }
}

void InputDispatcher::refresh_browser() {
    FileBrowserState& b = s_.fileBrowser;

    // ⚠️ SafFileSystem caches listings and drops them only on this app's writes; forget it here, or files
    // added from outside stay invisible.
    fs_.forget_listing(b.currentDirectory);

    // Re-list keeping the cursor (a refresh is not a navigation), clamped to the shorter list.
    rebuild_items(b, fs_);

    const int last = static_cast<int>(b.items.size()) - 1;
    b.cursor = std::clamp(b.cursor, 0, std::max(last, 0));

    if (b.cursor < b.scroll) b.scroll = b.cursor;
    if (b.cursor >= b.scroll + BROWSER_VISIBLE_ROWS) b.scroll = b.cursor - BROWSER_VISIBLE_ROWS + 1;
    b.scroll = std::clamp(b.scroll, 0, std::max(0, last - BROWSER_VISIBLE_ROWS + 1));
}

void InputDispatcher::refresh_browser_on_foreground() {
    // ⚠️ Only on the browser: its state outlives the screen.
    if (s_.currentScreen != ScreenType::FILE_BROWSER) return;

    // ⚠️ Not under a modal: a refresh can move the cursor, leaving "DELETE X?" with A deleting Y.
    if (s_.fileBrowser.mode != BrowserMode::NORMAL) return;

    refresh_browser();
}

// ─── The browser's cursor ────────────────────────────────────────────────────────────────────────

void InputDispatcher::browser_move_cursor(int delta, bool page) {
    FileBrowserState& b     = s_.fileBrowser;
    const int         total = static_cast<int>(b.items.size());
    if (total == 0) return;

    // ⚠️ UP/DOWN wrap; a page clamps (wrapping would fling you across a 400-file folder).
    if (page) {
        b.cursor = std::clamp(b.cursor + delta, 0, total - 1);
    } else {
        // ⚠️ Modulo TWICE: a page step can exceed the list length.
        b.cursor = ((b.cursor + delta) % total + total) % total;
    }

    // Keep the 19-row window around the cursor.
    if (b.cursor < b.scroll) {
        b.scroll = b.cursor;
    } else if (b.cursor >= b.scroll + BROWSER_VISIBLE_ROWS) {
        b.scroll = b.cursor - BROWSER_VISIBLE_ROWS + 1;
    }
}

// ─── A: open a folder, or LOAD the file ──────────────────────────────────────────────────────────

void InputDispatcher::browser_confirm() {
    FileBrowserState& b = s_.fileBrowser;

    // Delete only the paths captured for the question. If an outside browser
    // transition changed the listing or selection, the user must choose again.
    if (b.mode == BrowserMode::DELETE) {
        auto request = std::move(b.pendingDelete);
        b.pendingDelete = {};
        b.mode = BrowserMode::NORMAL;
        const auto selected = b.delete_targets();
        if (request.paths.empty() || request.directory != b.currentDirectory ||
            !std::equal(request.paths.begin(), request.paths.end(), selected.begin(), selected.end(),
                        [](const std::string& path, const BrowserItem* item) { return path == item->path; })) {
            set_status(b, "Delete selection changed. Select again.", false);
            return;
        }

        int done = 0, failed = 0;
        for (const std::string& path : request.paths) {
            if (fs_.delete_path(path)) ++done; else ++failed;
        }
        end_selection(b);
        if (done > 0) refresh_browser();

        if (request.paths.size() == 1)
            set_status(b, done ? "DELETED: " + request.name : "DELETE FAILED", failed == 0);
        else
            set_status(b, "DELETED " + file_count(static_cast<size_t>(done)) +
                              (failed ? ", FAILED " + std::to_string(failed) : ""), failed == 0);
        return;
    }

    // SET_HOME / FORGET_ROOT: A is the YES, armed by SELECT+A / SELECT+B on a granted tree.
    if (b.mode == BrowserMode::SET_HOME || b.mode == BrowserMode::FORGET_ROOT) {
        const bool         home = (b.mode == BrowserMode::SET_HOME);
        const BrowserItem* item = b.current();
        b.mode = BrowserMode::NORMAL;
        if (!item || !item->isRoot) return;

        const std::string name = item->displayName;
        if (!(home ? fs_.set_home_directory(item->path) : fs_.revoke_access(item->path))) {
            set_status(b, home ? "COULD NOT SET HOME FOLDER" : "COULD NOT FORGET IT", false);
            return;
        }
        // ⚠️ The derived roots were read at boot and must follow the home (a forgotten tree may have been
        // it), or media resolves against the old tree.
        const std::string root = fs_.parent_path(fs_.samples_directory());
        set_media_base_dir(root);
        host_.set_app_root(root);

        refresh_browser();
        set_status(b, (home ? "HOME FOLDER: " : "FORGOT: ") + name, true);
        return;
    }

    const BrowserItem* item = b.current();
    if (!item) return;

    if (item->is_parent()) { navigate_to_parent(b, fs_); return; }

    // An ACTION is the filesystem's to perform. ⚠️ No refresh: activate does not wait (Android's picker);
    // refresh_browser_on_foreground catches up.
    if (item->kind == BrowserItem::Kind::ACTION) {
        if(s_.browserPurpose==AppState::BrowserPurpose::LOAD_SOURCE&&item->path.rfind(SOURCE_BOOKMARK_PREFIX,0)==0){
            const auto action=item->path.substr(std::char_traits<char>::length(SOURCE_BOOKMARK_PREFIX));
            if(action=="synth"){
                close_file_browser();
                open_studio_synth(s_,host_.project().instruments[s_.currentInstrument]);
            }else if(action=="sample-granular"){
                close_file_browser();
                open_studio_synth(s_,host_.project().instruments[s_.currentInstrument]);
                sample_granular_select(s_,host_,true);
                if(s_.aHeld)s_.sampleGranular.returnReleaseMask|=1u;
            }else if(action=="presets"){
                remember_sample_browser();b.sampleDomain=false;
                navigate_to_folder(b,fs_,browser_dir(BrowserDir::INSTRUMENTS));
            }else if(action=="samples"){
                remember_sample_browser();open_sample_browser_folder(browser_dir(BrowserDir::SAMPLES));
            }else if(action=="soundfonts"){
                remember_sample_browser();b.sampleDomain=false;
                navigate_to_folder(b,fs_,browser_dir(BrowserDir::SOUNDFONTS));
            }
            else if(action=="midi"){close_file_browser();s_.currentScreen=ScreenType::INSTRUMENT;s_.instrumentCursorRow=2;s_.instrumentCursorColumn=1;}
            return;
        }
        const std::string label = item->displayName;
        if (!fs_.activate(item->path)) {
            set_status(b, label + " FAILED", false);
        }
        return;
    }

    if (item->kind == BrowserItem::Kind::FOLDER) { navigate_to_folder(b, fs_, item->path); return; }

    // ── It is a FILE — the reason the browser was opened ─────────────────────────────────────────
    const int         id   = s_.currentInstrument;
    const std::string ext  = to_lower(item->extension);
    const std::string path = item->path;
    const std::string stem = item->displayName;
    if(ext=="wav"&&probe_wav_size(*item)&&audio_source_browser(s_.browserPurpose)&&
       open_large_sample(path,stem,false,true))return;

    // The SAMPLE EDITOR's LOAD targets the editor's slot, which need not be INSTRUMENT's last one.
    const int sourceId = (s_.browserPurpose == AppState::BrowserPurpose::LOAD_SAMPLE_EDITOR)
                             ? s_.sampleEditor.instrumentId
                             : id;
    // The name the slot is about to stop deserving, read before the load.
    const std::string previousAutoName = instrument_auto_name(host_.project(), sourceId);

    // ⚠️ Every arm: slowness is the file's and the device's. A fast load draws nothing.
    const LoadScope loadScope(*this, now_ms_, stem);

    bool ok = false;
    switch (s_.browserPurpose) {
        case AppState::BrowserPurpose::LOAD_PRESET:
            ok = ext=="pti"&&host_.load_instrument_preset(id, path);
            break;

        case AppState::BrowserPurpose::LOAD_SOURCE:
            // The extension decides, not the slot's type: an .sf2/.sf3 turns a sampler slot into a
            // SoundFont.
            if(ext=="pti"){
                ok=host_.load_instrument_preset(id,path);
            }else if (is_soundfont_extension(ext)) {
                ok = host_.load_soundfont(id, path);
                if(ok)host_.set_instrument_type(id,songcore::InstrumentType::SOUNDFONT);
            } else if(std::find(sample_extensions().begin(),sample_extensions().end(),ext)!=sample_extensions().end()) {
                ok = host_.load_sample(id, path);
                if(ok)host_.set_instrument_type(id,songcore::InstrumentType::SAMPLER);
            }
            break;

        case AppState::BrowserPurpose::LOAD_SAMPLE_EDITOR:
            // The editor's own LOAD: the same load, returning to the EDITOR.
            ok = host_.load_sample(s_.sampleEditor.instrumentId, path);
            break;

        case AppState::BrowserPurpose::LOAD_GRANULAR_SAMPLE:
            if(id!=s_.sampleGranular.browserInstrument){set_status(b,"TARGET CHANGED / REOPEN LOAD",false);return;}
            ok=sample_granular_load_source(s_,host_,path);
            break;

        case AppState::BrowserPurpose::LOAD_GRANULAR_PRESET: {
            if(id!=s_.sampleGranular.browserInstrument){set_status(b,"TARGET CHANGED / REOPEN LOAD",false);return;}
            bool sampleReplaced=false;
            if(ext!=kSampleGranularPresetExtension||!sample_granular_load_preset(s_,host_,fs_,path,&sampleReplaced)){
                set_status(b,s_.studioMessage.empty()?"INVALID GRAIN PRESET / SOUND KEPT":s_.studioMessage,false);return;
            }
            close_file_browser();
            if(sampleReplaced&&s_.currentScreen==ScreenType::SAMPLE_EDITOR&&s_.sampleEditor.instrumentId==id)init_sample_editor_state();
            mark_modified();return;
        }

        case AppState::BrowserPurpose::LOAD_PROJECT:
            if(ext=="m8s"){
                std::string blob;if(!fs_.read_file(path,blob)){set_status(b,"M8 READ FAILED",false);return;}
                const auto directory=fs_.parent_path(path);
                const auto imported=studio::import_m8(blob,[&](std::string rel){
                    std::replace(rel.begin(),rel.end(),'\\','/');
                    while(!rel.empty()&&rel.front()=='/')rel.erase(0,1);
                    if(rel.empty()||rel.find(':')!=std::string::npos)return std::string();
                    // Only file-system paths under already granted roots; no traversal or network URI.
                    for(size_t at=0;at<=rel.size();){const auto end=rel.find('/',at);const auto part=rel.substr(at,end==std::string::npos?end:end-at);if(part==".."||part==".")return std::string();if(end==std::string::npos)break;at=end+1;}
                    // An M8 song often lives in Songs/ while /Samples is beside that folder.
                    auto ancestor=directory;
                    for(int depth=0;depth<8&&!ancestor.empty();++depth){
                        const auto candidate=ancestor+"/"+rel;if(fs_.file_exists(candidate))return candidate;
                        const auto parent=fs_.parent_path(ancestor);if(parent==ancestor)break;ancestor=parent;
                    }
                    if(rel.rfind("Samples/",0)==0)rel.erase(0,8);
                    const auto sample=fs_.samples_directory()+"/"+rel;
                    return fs_.file_exists(sample)?sample:std::string();
                });
                if(!imported.ok){set_status(b,imported.error,false);return;}
                s_.m8PendingJson=songcore::serialize_project(imported.project);s_.m8PendingPath=path;s_.m8PendingReport=imported.report;
                s_.confirm.open(ConfirmDialogState::Kind::M8_IMPORT);s_.confirm.details=imported.summary;return;
            }
            // ⚠️ The whole document, returning early: load_project_file is parse, push, media and params.
            if (!host_.load_project_file(path, fs_.samples_directory())) {
                set_status(b, "LOAD FAILED", false);
                return;
            }
            // ⚠️ A cancelled project load cannot stay half-done (later instruments would play silence):
            // NEW PROJECT's blank document, and the message says so.
            if (pt::load_cancelled()) {
                host_.new_project();
                host_.push_params();
                // No source path, and the autosave cleared.
                load_project_done("");
                set_status(s_, "LOAD CANCELLED", true);
                return;
            }
            load_project_done(path);
            return;

        case AppState::BrowserPurpose::LOAD_THEME:
            // ⚠️ Pixels only, returning early. The extension is re-checked (the D-pad leaves the folder),
            // and a failed parse must not blank the palette.
            if (ext != "ptt" || !load_theme_file(fs_, path, s_.theme)) {
                set_status(b, "LOAD FAILED", false);
                return;
            }
            // Back to the editor that raised the browser (`theme_row_action` closed it on the way out).
            close_file_browser();
            open_theme_editor();
            set_status(s_, "THEME LOADED", true);
            return;

        case AppState::BrowserPurpose::LOAD_SCALE: {
            // ⚠️ Into a copy that keeps the slot's id, committed only once it parses. The extension is
            // re-checked: the D-pad walks out of the folder.
            songcore::Scale loaded = host_.project().scales[static_cast<size_t>(s_.currentScale)];
            if (ext != SCALE_FILE_EXT || !load_scale_file(fs_, path, loaded)) {
                set_status(b, "LOAD FAILED", false);
                return;
            }
            host_.edit_project().scales[static_cast<size_t>(s_.currentScale)] = loaded;
            mark_modified();
            close_file_browser();
            set_status(s_, "SCALE LOADED", true);
            return;
        }

        case AppState::BrowserPurpose::LOAD_GROOVE: {
            // The scale load's two guards, for the same reasons (the slot's `id` is what `GRV` names).
            songcore::Groove loaded = host_.project().grooves[static_cast<size_t>(s_.currentGroove)];
            if (ext != GROOVE_FILE_EXT || !load_groove_file(fs_, path, loaded)) {
                set_status(b, "LOAD FAILED", false);
                return;
            }
            host_.edit_project().grooves[static_cast<size_t>(s_.currentGroove)] = loaded;
            mark_modified();
            close_file_browser();
            set_status(s_, "GROOVE LOADED", true);
            return;
        }

        case AppState::BrowserPurpose::LOAD_REVERB: {
            // Read into a COPY of the cells, committed only once the file parses.
            ReverbCells cells = reverb_cells_of(host_.project());
            if (ext != REVERB_FILE_EXT || !load_reverb_file(fs_, path, cells)) {
                set_status(b, "LOAD FAILED", false);
                return;
            }
            apply_reverb_cells(host_.edit_project(), cells);
            // ⚠️ Closed first: mark_modified pushes only from EFFECTS, so the preset would show and not sound.
            close_file_browser();
            mark_modified();
            set_status(s_, "REVERB LOADED", true);
            return;
        }

        case AppState::BrowserPurpose::LOAD_DELAY: {
            // As the reverb's, including the order of the last two calls.
            DelayCells cells = delay_cells_of(host_.project());
            if (ext != DELAY_FILE_EXT || !load_delay_file(fs_, path, cells)) {
                set_status(b, "LOAD FAILED", false);
                return;
            }
            apply_delay_cells(host_.edit_project(), cells);
            close_file_browser();
            mark_modified();
            set_status(s_, "DELAY LOADED", true);
            return;
        }
        case AppState::BrowserPurpose::LOAD_STUDIO_FX:
        case AppState::BrowserPurpose::LOAD_STUDIO_MASTER: {
            const bool effect = s_.browserPurpose == AppState::BrowserPurpose::LOAD_STUDIO_FX;
            std::string data;
            std::array<int,studio::COUNT> checked{};
            if (ext != (effect ? STUDIO_FX_FILE_EXT : STUDIO_MASTER_FILE_EXT) ||
                !fs_.read_file(path,data) || !parse_studio_preset(effect,data,checked)) {
                set_status(b,"INVALID PRESET - MIX KEPT",false); return;
            }
            apply_studio_preset(effect,checked,host_.edit_project());
            // Factory files and the native selector are two views of the same authored bank.
            // Matching only the affected group avoids changing the opposite live rack.
            for (int presetId=0;presetId<(effect?studio::effect_preset_count():studio::master_preset_count());++presetId) {
                const auto candidate=effect?studio::effect_preset(presetId):studio::master_preset(presetId);
                bool same=true;
                for (int i=0;i<studio::COUNT;++i) if (studio_preset_key(effect,i)&&candidate[i]!=checked[i]) same=false;
                if (same) {if(effect)s_.studioEffectPreset=presetId;else s_.studioMasterPreset=presetId;break;}
            }
            close_file_browser();
            host_.push_globals();
            mark_modified();
            s_.studioMessage = effect ? "FX LIBRARY PRESET LOADED" : "MASTER LIBRARY PRESET LOADED";
            set_status(s_,s_.studioMessage,true);
            return;
        }
    }

    if (!ok) {
        // ⚠️ A cancel is not a failure: no red line.
        if (host_.last_load_cancelled()) {
            set_status(b, "CANCELLED", true);
            return;
        }
        if(ext=="wav"&&host_.last_load_ran_out_of_memory()&&audio_source_browser(s_.browserPurpose)&&
           open_large_sample(path,stem,false))return;
        if(s_.browserPurpose==AppState::BrowserPurpose::LOAD_GRANULAR_SAMPLE){set_status(b,s_.studioMessage.empty()?"SOURCE LOAD FAILED / OLD SOUND KEPT":s_.studioMessage,false);return;}
        // ⚠️ A file too big for the device is not corrupt; say so.
        set_status(b, host_.last_load_ran_out_of_memory() ? "FILE TOO BIG" : "LOAD FAILED", false);
        return;
    }

    // The slot adopts the file's name unless the user TYPED its current one (not the default, not the
    // name it took from the replaced source). Both source loads apply it.
    if ((s_.browserPurpose == AppState::BrowserPurpose::LOAD_SOURCE ||
         s_.browserPurpose == AppState::BrowserPurpose::LOAD_SAMPLE_EDITOR) &&
        ext!="pti"&&sourceId >= 0 && static_cast<size_t>(sourceId) < host_.project().instruments.size()) {
        Instrument& ins = host_.edit_project().instruments[static_cast<size_t>(sourceId)];
        if (songcore::instrument_has_default_name(ins) ||
            (!previousAutoName.empty() && ins.name == previousAutoName)) {
            ins.name = adopted_name(stem);
        }
    }

    mark_modified();

    if (s_.browserPurpose == AppState::BrowserPurpose::LOAD_SAMPLE_EDITOR) {
        // Back into the editor on the new audio (previousScreen is the editor's return target), rebuilt.
        close_file_browser();
        s_.currentScreen = ScreenType::SAMPLE_EDITOR;
        init_sample_editor_state();
        return;
    }

    const bool granularSource=s_.browserPurpose==AppState::BrowserPurpose::LOAD_GRANULAR_SAMPLE;
    close_file_browser();
    if(granularSource&&s_.currentScreen==ScreenType::SAMPLE_EDITOR&&s_.sampleEditor.instrumentId==id)init_sample_editor_state();
}

// ─── The multi-select and the file clipboard ─────────────────────────────────────────────────────

void InputDispatcher::browser_select() {
    // ⚠️ A plain anchor..cursor range (a second tap within 500 ms selects all) — not the grids' widener.
    FileBrowserState& b = s_.fileBrowser;
    if (b.mode != BrowserMode::NORMAL) return;

    if (!b.selectionMode) {
        b.selectionMode   = true;
        b.selectionAnchor = b.cursor;
        b.lastSelectTapMs = now_ms_;
    } else if (now_ms_ - b.lastSelectTapMs <= 500) {
        // Tap again inside the window: select everything, skipping the ".." row.
        const int first = b.first_selectable();
        const int last  = std::max(static_cast<int>(b.items.size()) - 1, first);
        b.selectionAnchor = first;
        b.cursor          = last;
        b.scroll          = std::max(0, last - BROWSER_VISIBLE_ROWS + 1);
        b.lastSelectTapMs = 0;   // …so a third tap re-anchors rather than re-selecting all
    } else {
        b.selectionAnchor = b.cursor;   // the window lapsed — start a fresh range here
        b.lastSelectTapMs = now_ms_;
    }
}

void InputDispatcher::browser_cut_or_paste() {
    FileBrowserState& b = s_.fileBrowser;
    if (b.mode != BrowserMode::NORMAL) return;

    if (b.selectionMode) {
        std::vector<std::string> files = browser_selected_paths();
        if (files.empty()) return;
        const size_t n = files.size();

        b.fileClipboard      = std::move(files);
        b.fileClipboardIsCut = true;
        end_selection(b);
        set_status(b, "CUT " + file_count(n), true);
    } else if (!b.fileClipboard.empty()) {
        browser_paste();
    }
}

std::vector<std::string> InputDispatcher::browser_selected_paths() const {
    const FileBrowserState& b = s_.fileBrowser;
    std::vector<std::string> out;
    if (!b.selectionMode || b.selectionAnchor < 0) return out;

    const int lo = std::max(std::min(b.selectionAnchor, b.cursor), b.first_selectable());
    const int hi = std::max(b.selectionAnchor, b.cursor);
    for (int i = lo; i <= hi; ++i) {
        const BrowserItem* item = b.item_at(i);
        if (item && !item->is_pseudo()) out.push_back(item->path);
    }
    return out;
}

void InputDispatcher::browser_paste() {
    FileBrowserState& b = s_.fileBrowser;
    if (b.fileClipboard.empty()) return;

    const std::string& dest = b.currentDirectory;
    int done = 0, failed = 0;

    for (const std::string& src : b.fileClipboard) {
        if (!fs_.file_exists(src)) { ++failed; continue; }

        const std::string name = path_name(src);
        std::string       target = dest + "/" + name;
        if (target == src) { ++done; continue; }   // pasted back into the source folder

        // De-duplicate: "kick.wav" → "kick_2.wav" → "kick_3.wav". Never overwrite.
        if (fs_.file_exists(target)) {
            const std::string ext  = path_extension(name);
            const std::string base = path_stem(name);
            for (int n = 2;; ++n) {
                target = dest + "/" + base + "_" + std::to_string(n) + (ext.empty() ? "" : "." + ext);
                if (!fs_.file_exists(target)) break;
            }
        }

        const bool ok = b.fileClipboardIsCut ? fs_.move_file(src, target) : fs_.copy_file(src, target);
        if (ok) ++done; else ++failed;
    }

    const bool  cut  = b.fileClipboardIsCut;
    const char* verb = cut ? "MOVED" : "COPIED";

    // A CUT clipboard is spent once pasted; a COPY can be pasted into several folders.
    if (cut) b.fileClipboard.clear();

    refresh_browser();
    set_status(b,
               std::string(verb) + " " +
                   (failed == 0 ? file_count(static_cast<size_t>(done))
                                : std::to_string(done) + ", FAILED " + std::to_string(failed)),
               failed == 0);
}

// ─── SELECT + A / B / R — the browser's file-management chords ───────────────────────────────────

void InputDispatcher::browser_rename() {
    if (s_.fileBrowser.mode != BrowserMode::NORMAL) return;

    const BrowserItem* item = s_.fileBrowser.current();

    // On a granted tree SELECT+A offers to make it the home folder. Armed: A confirms, B cancels.
    if (item && item->isRoot) {
        s_.fileBrowser.mode = BrowserMode::SET_HOME;
        set_status(s_.fileBrowser, "", true);
        return;
    }

    if (!item || item->is_pseudo()) return;   // ".." and ADD FOLDER… are not files

    const bool  dir   = (item->kind == BrowserItem::Kind::FOLDER);
    const std::string ext = to_lower(item->extension);
    const char* label = dir              ? "FOLDER NAME:"
                        : (ext == "wav") ? "SAMPLE NAME:"
                        : (ext == "ptp") ? "PROJECT NAME:"
                                         : "FILE NAME:";

    // A FOLDER's displayName is "[name]"; the brackets must not reach the rename box.
    const std::string base = dir ? path_name(item->path) : item->displayName;
    open_qwerty(QwertyContext::FILE_RENAME, base, label, item->path);
}

void InputDispatcher::browser_arm_delete() {
    auto& browser = s_.fileBrowser;
    if (browser.mode != BrowserMode::NORMAL) return;

    const BrowserItem* item = browser.current();

    // ⚠️ On a granted tree this is FORGET: deleting "pt://<id>" would remove the whole directory. Handing
    // back the permission touches no file.
    if (!browser.selectionMode && item && item->isRoot) {
        browser.mode = BrowserMode::FORGET_ROOT;
        set_status(browser, "", true);
        return;
    }

    const auto targets = browser.delete_targets();
    if (targets.empty()) return;

    auto& request = browser.pendingDelete;
    request = {};
    request.directory = browser.currentDirectory;
    request.name = targets.front()->displayName;
    request.paths.reserve(targets.size());
    for (const auto* target : targets) {
        request.paths.push_back(target->path);
        request.includesFolder |= target->kind == BrowserItem::Kind::FOLDER;
    }
    host_.stop_preview(/*cut=*/true);
    browser.mode = BrowserMode::DELETE;
    set_status(browser, "", true);
}

void InputDispatcher::browser_new_folder() {
    if (s_.fileBrowser.mode != BrowserMode::NORMAL) return;
    open_qwerty(QwertyContext::FOLDER_CREATE, "NEW FOLDER", "FOLDER NAME:",
                s_.fileBrowser.currentDirectory);
}

// ─── Sorting ─────────────────────────────────────────────────────────────────────────────────────

void InputDispatcher::browser_cycle_sort(int delta) {
    FileBrowserState& b = s_.fileBrowser;

    // Steps the modes by index, so the enum's order is behaviour (ui/filesystem.h).
    const int next = (static_cast<int>(b.sortMode) + delta + FILE_SORT_MODE_COUNT) % FILE_SORT_MODE_COUNT;
    b.sortMode = static_cast<FileSortMode>(next);

    // ⚠️ Rebuild rather than re-sort in place, or the tie-break depends on the previous sort mode.
    rebuild_items(b, fs_);

    // The cursor stays put, so the row under it now holds a different file — the list is re-ordered.
    set_status(b, file_sort_label(b.sortMode), true);
}

}  // namespace pt::ui
