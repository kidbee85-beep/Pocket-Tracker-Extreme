#include "ui/modules/file_browser.h"

#include "ui/helpers.h"
#include "ui/std_filesystem.h"

#include <algorithm>
#include <cstdio>
#include <ctime>

namespace pt::ui {

namespace {

/** "12B" / "48KB" / "3MB" — integer division, no decimals. */
std::string format_file_size(int64_t bytes) {
    char buf[32];
    if (bytes < 1024) {
        std::snprintf(buf, sizeof(buf), "%lldB", static_cast<long long>(bytes));
    } else if (bytes < 1024 * 1024) {
        std::snprintf(buf, sizeof(buf), "%lldKB", static_cast<long long>(bytes / 1024));
    } else {
        std::snprintf(buf, sizeof(buf), "%lldMB", static_cast<long long>(bytes / (1024 * 1024)));
    }
    return buf;
}

/** "dd-MM-yy" over the file's mtime, in the machine's local zone. */
std::string format_file_date(int64_t millis) {
    const std::time_t secs = static_cast<std::time_t>(millis / 1000);
    std::tm           tm{};
#if defined(_WIN32)
    if (localtime_s(&tm, &secs) != 0) return "--------";
#else
    if (localtime_r(&secs, &tm) == nullptr) return "--------";
#endif
    // `% 100` quiets -Wformat-truncation; a valid `tm` is unchanged.
    const int day   = tm.tm_mday % 100;
    const int month = (tm.tm_mon + 1) % 100;
    const int year  = (tm.tm_year + 1900) % 100;

    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d-%02d-%02d", day, month, year);
    return buf;
}

bool ext_matches(const std::string& ext, const std::vector<std::string>& allowed) {
    if (allowed.empty()) return true;   // no filter = every file
    const std::string lower = to_lower(ext);
    return std::find(allowed.begin(), allowed.end(), lower) != allowed.end();
}

bool is_digit(char c) { return c >= '0' && c <= '9'; }

Argb library_tint(Argb base, Argb accent, int weight) {
    auto channel = [&](int shift) {
        const int a = (base >> shift) & 255, b = (accent >> shift) & 255;
        return static_cast<Argb>((a * (255 - weight) + b * weight + 127) / 255);
    };
    return 0xFF000000u | channel(16) << 16 | channel(8) << 8 | channel(0);
}

std::string path_label(Canvas& c, const std::string& path, int width) {
    int columns = std::min(Canvas::glyph_count(path), 100);
    std::string clipped = Canvas::clip_text_head(path, columns);
    while (columns > 1 && c.label_width(clipped, 12) > width)
        clipped = Canvas::clip_text_head(path, --columns);
    return clipped;
}

// Small, code-drawn marks keep folder/source recognition independent of the font.
void browser_icon(Canvas& c, int x, int y, const BrowserItem& item, Argb ink) {
    if (item.kind == BrowserItem::Kind::FOLDER) {
        c.fill_rect(x + 1, y, 6, 3, ink);
        c.fill_rect(x, y + 3, 14, 9, with_alpha(ink, .16f));
        c.stroke_rect(x, y + 3, 14, 9, ink);
    } else if (item.kind == BrowserItem::Kind::PARENT) {
        c.fill_rect(x + 6, y + 2, 2, 10, ink);
        c.fill_rect(x + 4, y + 3, 6, 2, ink);
        c.fill_rect(x + 2, y + 5, 10, 2, ink);
    } else if (item.kind == BrowserItem::Kind::ACTION) {
        c.fill_rect(x + 6, y + 1, 2, 11, ink);
        c.fill_rect(x + 1, y + 5, 12, 2, ink);
    } else if (ext_matches(item.extension, sample_extensions())) {
        const int heights[] = {3, 7, 12, 8, 4};
        for (int i = 0; i < 5; ++i)
            c.fill_rect(x + i * 3, y + (12 - heights[i]) / 2, 2, heights[i], ink);
    } else {
        c.stroke_rect(x + 2, y, 10, 13, ink);
        c.fill_rect(x + 5, y + 4, 5, 1, ink);
        c.fill_rect(x + 5, y + 7, 5, 1, ink);
        c.fill_rect(x + 5, y + 10, 3, 1, ink);
    }
}

std::string browser_title(const std::vector<std::string>& extensions) {
    if (!extensions.empty() && std::all_of(extensions.begin(), extensions.end(),
        [](const std::string& ext) { return ext_matches(ext, sample_extensions()); })) return "Samples";
    if (std::find(extensions.begin(), extensions.end(), "pti") != extensions.end()) return "Instruments";
    if (std::find(extensions.begin(), extensions.end(), "ptp") != extensions.end() ||
        std::find(extensions.begin(), extensions.end(), "m8s") != extensions.end()) return "Projects";
    if (extensions == soundfont_extensions()) return "Soundfonts";
    return "Library";
}

}  // namespace

// ─── The name order ──────────────────────────────────────────────────────────────────────────────

bool natural_name_less(const std::string& a, const std::string& b) {
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (is_digit(a[i]) && is_digit(b[j])) {
            // Skip leading zeros: then the longer run is the larger number.
            size_t si = i, sj = j;
            while (si < a.size() && a[si] == '0') ++si;
            while (sj < b.size() && b[sj] == '0') ++sj;
            size_t ei = si, ej = sj;
            while (ei < a.size() && is_digit(a[ei])) ++ei;
            while (ej < b.size() && is_digit(b[ej])) ++ej;

            if (ei - si != ej - sj) return (ei - si) < (ej - sj);
            for (size_t k = 0; k < ei - si; ++k) {
                if (a[si + k] != b[sj + k]) return a[si + k] < b[sj + k];
            }
            i = ei;   // the same number — step over both
            j = ej;
            continue;
        }
        // Unsigned, so non-ASCII (UTF-8) bytes sort above ASCII.
        const unsigned char ca = static_cast<unsigned char>(a[i]);
        const unsigned char cb = static_cast<unsigned char>(b[j]);
        if (ca != cb) return ca < cb;
        ++i;
        ++j;
    }

    // The shorter name is smaller; if every token matched, the raw compare keeps the order total.
    if (i < a.size() || j < b.size()) return i >= a.size();
    return a < b;
}

// ─── State ───────────────────────────────────────────────────────────────────────────────────────

bool FileBrowserState::is_selected(int index) const {
    if (!selectionMode || selectionAnchor < 0) return false;
    const int lo = std::max(std::min(selectionAnchor, cursor), first_selectable());
    const int hi = std::max(selectionAnchor, cursor);
    return index >= lo && index <= hi;
}

std::vector<const BrowserItem*> FileBrowserState::delete_targets() const {
    std::vector<const BrowserItem*> out;
    if (selectionMode) {
        for (int i = 0; i < static_cast<int>(items.size()); ++i)
            if (is_selected(i) && !items[static_cast<size_t>(i)].is_pseudo())
                out.push_back(&items[static_cast<size_t>(i)]);
    } else if (const BrowserItem* item = current(); item && !item->is_pseudo()) {
        out.push_back(item);
    }
    return out;
}

std::string FileBrowserState::clipboard_info() const {
    if (fileClipboard.empty()) return "";
    const size_t n = fileClipboard.size();
    return std::string(fileClipboardIsCut ? "CUT " : "CPY ") + std::to_string(n) +
           (n == 1 ? " FILE" : " FILES");
}

// ─── The listing ─────────────────────────────────────────────────────────────────────────────────

std::vector<BrowserItem> build_item_list(FileSystem& fs, const std::string& directory,
                                         const std::vector<std::string>& extensions) {
    std::vector<BrowserItem> items;

    // ".." only when there is somewhere to go.
    const std::string parent = fs.parent_path(directory);
    if (!parent.empty()) {
        BrowserItem up;
        up.kind        = BrowserItem::Kind::PARENT;
        up.path        = parent;
        up.displayName = "..";
        items.push_back(std::move(up));
    }

    std::vector<FileInfo> entries = fs.list_files(directory);

    std::vector<BrowserItem> actions;
    std::vector<BrowserItem> folders;
    std::vector<BrowserItem> files;
    for (const FileInfo& e : entries) {
        // An ACTION row is neither hidden nor filtered, and is drawn verbatim: its name is a sentence.
        if (e.isAction) {
            BrowserItem act;
            act.kind        = BrowserItem::Kind::ACTION;
            act.path        = e.path;
            act.displayName = e.name;
            act.sortName    = to_lower(e.name);
            actions.push_back(std::move(act));
            continue;
        }

        if (!e.name.empty() && e.name[0] == '.') continue;   // hidden

        BrowserItem it;
        it.path         = e.path;
        it.sortName     = to_lower(e.name);
        it.size         = e.size;
        it.lastModified = e.lastModified;
        it.isRoot       = e.isRoot;

        if (e.isDirectory) {
            it.kind        = BrowserItem::Kind::FOLDER;
            it.displayName = "[" + e.name + "]";
            folders.push_back(std::move(it));
        } else {
            if (!ext_matches(e.extension, extensions)) continue;
            it.kind        = BrowserItem::Kind::FILE;
            it.extension   = e.extension;
            it.displayName = e.name_without_extension();
            it.sizeText    = format_file_size(e.size);
            it.dateText    = format_file_date(e.lastModified);
            files.push_back(std::move(it));
        }
    }

    // By name: the base every other sort is stable against. ⚠️ `natural_name_less`, or DATE and SIZE
    // would break ties unlike NAME.
    auto by_name = [](const BrowserItem& a, const BrowserItem& b) {
        return natural_name_less(a.sortName, b.sortName);
    };
    std::stable_sort(folders.begin(), folders.end(), by_name);
    std::stable_sort(files.begin(), files.end(), by_name);

    // The actions sit under "..", unsorted: they are ways out, not things in here.
    items.insert(items.end(), actions.begin(), actions.end());
    items.insert(items.end(), folders.begin(), folders.end());
    items.insert(items.end(), files.begin(), files.end());
    return items;
}

void sort_items(std::vector<BrowserItem>& items, FileSortMode mode) {
    // ".." and the actions stay pinned; folders and files are sorted as two groups.
    std::vector<BrowserItem> pinned, folders, files;
    for (BrowserItem& it : items) {
        switch (it.kind) {
            case BrowserItem::Kind::PARENT:
            case BrowserItem::Kind::ACTION: pinned.push_back(std::move(it)); break;
            case BrowserItem::Kind::FOLDER: folders.push_back(std::move(it)); break;
            case BrowserItem::Kind::FILE:   files.push_back(std::move(it)); break;
        }
    }

    // ⚠️ stable_sort: same-second files keep the name order.
    auto apply = [mode](std::vector<BrowserItem>& v) {
        switch (mode) {
            case FileSortMode::NAME_ASC:
                std::stable_sort(v.begin(), v.end(), [](const BrowserItem& a, const BrowserItem& b) {
                    return natural_name_less(a.sortName, b.sortName);
                });
                break;
            case FileSortMode::NAME_DESC:
                std::stable_sort(v.begin(), v.end(), [](const BrowserItem& a, const BrowserItem& b) {
                    return natural_name_less(b.sortName, a.sortName);
                });
                break;
            case FileSortMode::DATE_ASC:
                std::stable_sort(v.begin(), v.end(), [](const BrowserItem& a, const BrowserItem& b) {
                    return a.lastModified < b.lastModified;
                });
                break;
            case FileSortMode::DATE_DESC:
                std::stable_sort(v.begin(), v.end(), [](const BrowserItem& a, const BrowserItem& b) {
                    return b.lastModified < a.lastModified;
                });
                break;
            case FileSortMode::SIZE_ASC:
                std::stable_sort(v.begin(), v.end(),
                                 [](const BrowserItem& a, const BrowserItem& b) { return a.size < b.size; });
                break;
            case FileSortMode::SIZE_DESC:
                std::stable_sort(v.begin(), v.end(),
                                 [](const BrowserItem& a, const BrowserItem& b) { return b.size < a.size; });
                break;
        }
    };
    apply(folders);
    apply(files);

    items.clear();
    items.insert(items.end(), std::make_move_iterator(pinned.begin()), std::make_move_iterator(pinned.end()));
    items.insert(items.end(), std::make_move_iterator(folders.begin()), std::make_move_iterator(folders.end()));
    items.insert(items.end(), std::make_move_iterator(files.begin()), std::make_move_iterator(files.end()));
}

void rebuild_items(FileBrowserState& s, FileSystem& fs) {
    // ⚠️ Rebuild, then sort — never re-sort what is on screen, or ties would follow the previous mode.
    s.items = build_item_list(fs, s.currentDirectory, s.fileExtensions);
    sort_items(s.items, s.sortMode);
    const auto at=s.items.begin()+(!s.items.empty()&&s.items.front().is_parent()?1:0);
    s.items.insert(at,s.sourceBookmarks.begin(),s.sourceBookmarks.end());
}

void navigate_to_folder(FileBrowserState& s, FileSystem& fs, const std::string& folder) {
    s.currentDirectory = folder;
    fs.forget_listing(folder);  // A reopened SAF folder must include changes made outside the app.
    rebuild_items(s, fs);
    s.cursor           = 0;
    s.scroll           = 0;
    s.statusMessage.clear();
    s.statusSuccess   = true;

    // ⚠️ The selection's anchor indexes the directory left behind; B would copy unpicked files.
    s.selectionMode   = false;
    s.selectionAnchor = -1;

    // A directory we cannot read lists empty.
}

void navigate_to_parent(FileBrowserState& s, FileSystem& fs) {
    const std::string parent = fs.parent_path(s.currentDirectory);
    if (parent.empty()) return;   // already at a root
    navigate_to_folder(s, fs, parent);
}

void restore_browser_position(FileBrowserState& s, const std::string& selectedPath,
                              int cursor, int scroll) {
    const int last = std::max(0, static_cast<int>(s.items.size()) - 1);
    s.cursor = std::clamp(cursor, 0, last);
    if (!selectedPath.empty()) {
        const auto found = std::find_if(s.items.begin(), s.items.end(), [&](const BrowserItem& item) {
            return item.path == selectedPath;
        });
        if (found != s.items.end()) s.cursor = static_cast<int>(found - s.items.begin());
    }
    s.scroll = std::clamp(scroll, 0, std::max(0, last - BROWSER_VISIBLE_ROWS + 1));
    if (s.cursor < s.scroll) s.scroll = s.cursor;
    if (s.cursor >= s.scroll + BROWSER_VISIBLE_ROWS)
        s.scroll = s.cursor - BROWSER_VISIBLE_ROWS + 1;
}

// ─── Drawing ─────────────────────────────────────────────────────────────────────────────────────

void FileBrowserModule::draw(Canvas& c, int x, int y, const FileBrowserState& s,
                             const Theme& t) const {
    c.set_surface_style(t.surfaceStyle);
    c.surface_rect(x, y, WIDTH, HEIGHT, t.background);
    const int total = static_cast<int>(s.items.size());
    const BrowserItem* current = s.current();
    const bool confirming = s.mode != BrowserMode::NORMAL;
    const Argb accent = s.mode == BrowserMode::DELETE ? 0xFFF5809B : t.rowCursor;

    // A large title, actual folder context and position are distinct from the working list.
    c.panel_rect(x + 6, y + 5, WIDTH - 12, 51, t.rowEvery4th,
                 with_alpha(t.textParam, .22f), 6);
    c.fill_rect(x + 10, y + 15, 3, 29, accent);
    const std::string title = s.sourceBookmarks.empty()?browser_title(s.fileExtensions):"Instrument Sources";
    c.draw_label(title, x + 20, y + 10, t.textValue, 18);
    const std::string count = total > 0 ? std::to_string(s.cursor + 1) + "/" + std::to_string(total) : "0 files";
    const int countW = Canvas::text_width(count, 1, 2);
    const int countX = x + WIDTH - 18 - countW;
    const int contextX = x + 35 + c.label_width(title, 18);
    std::string folder = path_name(s.currentDirectory);
    if (folder.empty()) folder = s.currentDirectory;
    c.draw_label(c.clip_label(folder, countX - 22 - contextX, 12), contextX, y + 14,
                 t.textParam, 12);

    c.panel_rect(countX - 7, y + 11, countW + 14, 20,
                 library_tint(t.rowEvery4th, accent, 18), with_alpha(accent, .22f), 4);
    c.draw_text(count, countX, y + 16, t.textTitle, 1, 2);
    c.draw_text("FOLDER", x + 20, y + 37, t.textEmpty, 1, 2);
    c.draw_label(path_label(c, s.currentDirectory, WIDTH - 110), x + 91, y + 35,
                 t.textParam, 12);

    // Nineteen unchanged navigation rows. The left branch marks entries as children of this folder.
    constexpr int listY = 60;
    constexpr int listH = BROWSER_VISIBLE_ROWS * ROW_HEIGHT - 1;
    c.panel_rect(x + 7, y + listY - 2, WIDTH - 20, listH + 3,
                 library_tint(t.background, t.rowEvery4th, 40), with_alpha(t.textParam, .12f), 5);
    for (int i = 0; i < BROWSER_VISIBLE_ROWS; ++i) {
        const int index = s.scroll + i;
        if (index < 0 || index >= total) break;
        const int rowY = y + listY + i * ROW_HEIGHT;
        const BrowserItem& item = s.items[static_cast<size_t>(index)];
        const bool isCursor = index == s.cursor;
        const bool isSel = s.is_selected(index);
        const Argb rowAccent = isCursor ? accent : t.rowSelection;
        if (isCursor || isSel) {
            const Argb base = library_tint(t.rowEvery4th, rowAccent, isCursor ? 36 : 23);
            c.panel_rect(x + 9, rowY, WIDTH - 25, ROW_HEIGHT - 1, base,
                         with_alpha(rowAccent, isCursor ? .75f : .35f), 3);
            c.fill_rect(x + 10, rowY + 4, 3, ROW_HEIGHT - 9, rowAccent);
        } else if (index % 2 != 0) {
            c.fill_rect(x + 9, rowY, WIDTH - 25, ROW_HEIGHT - 1, with_alpha(t.rowEvery4th, .38f));
        }

        Argb ink = t.textValue;
        if (item.kind != BrowserItem::Kind::FILE) ink = t.textTitle;
        else if (ext_matches(item.extension, video_extensions())) ink = COLOR_VIDEO;
        const Argb mark = isCursor || isSel ? rowAccent : with_alpha(ink, .68f);
        if (item.kind != BrowserItem::Kind::PARENT) {
            c.fill_rect(x + 22, rowY, 1, (index + 1 == total ? 11 : ROW_HEIGHT),
                         with_alpha(t.textParam, .16f));
            c.fill_rect(x + 22, rowY + 10, 7, 1, with_alpha(t.textParam, .22f));
        }
        browser_icon(c, x + 32, rowY + 4, item, mark);

        std::string name = item.displayName;
        if (item.kind == BrowserItem::Kind::PARENT) name = "Parent folder";
        if (item.kind == BrowserItem::Kind::FOLDER && name.size() >= 2 &&
            name.front() == '[' && name.back() == ']') name = name.substr(1, name.size() - 2);
        const bool file = item.kind == BrowserItem::Kind::FILE;
        c.draw_label(c.clip_label(name, file ? 337 : 550, 12), x + 56, rowY + 4, ink, 12);

        if (file) {
            std::string ext = item.extension;
            for (char& letter : ext) if (letter >= 'a' && letter <= 'z') letter -= 'a' - 'A';
            ext = Canvas::clip_text(ext, 4);
            c.panel_rect(x + 401, rowY + 3, 48, 15,
                         library_tint(t.rowEvery4th, mark, 18), with_alpha(mark, .16f), 3);
            c.draw_text(ext, x + 425 - Canvas::text_width(ext, 1, 2) / 2,
                         rowY + 6, isCursor ? t.textValue : t.textParam, 1, 2);
            const std::string size = Canvas::clip_text(item.sizeText, 7);
            c.draw_text(size, x + 531 - Canvas::text_width(size, 1, 2), rowY + 6,
                         t.textParam, 1, 2);
            c.draw_text(item.dateText, x + 537, rowY + 6, t.textEmpty, 1, 2);
        }
    }

    if (total == 0) {
        c.panel_rect(x + 83, y + 197, WIDTH - 166, 98, t.rowEvery4th,
                     with_alpha(t.textParam, .18f), 8);
        BrowserItem emptyFolder; emptyFolder.kind = BrowserItem::Kind::FOLDER;
        browser_icon(c, x + 310, y + 212, emptyFolder, accent);
        const std::string empty = "This folder is empty";
        c.draw_label(empty, x + (WIDTH - c.label_width(empty, 18)) / 2, y + 236, t.textValue, 18);
        const std::string tip = "Choose another folder or change the file filter.";
        c.draw_label(tip, x + (WIDTH - c.label_width(tip, 12)) / 2, y + 266, t.textParam, 12);
    } else if (total > BROWSER_VISIBLE_ROWS) {
        const int thumbH = std::max(8, listH * BROWSER_VISIBLE_ROWS / total);
        const int offset = (listH - thumbH) * std::clamp(s.scroll, 0, total - BROWSER_VISIBLE_ROWS) /
                           (total - BROWSER_VISIBLE_ROWS);
        c.fill_rect(x + WIDTH - 6, y + listY, 2, listH, with_alpha(t.textParam, .10f));
        c.fill_rect(x + WIDTH - 6, y + listY + offset, 2, thumbH, with_alpha(t.textTitle, .65f));
    }

    // The footer reports available actions. A pending confirmation owns the entire line.
    std::string hint;
    Argb hintColor = t.textParam;
    if (s.mode == BrowserMode::DELETE) {
        const auto doomed = s.delete_targets();
        const std::string what = doomed.size() == 1 ? doomed[0]->displayName : std::to_string(doomed.size()) + " files";
        const int room = WIDTH - 40 - c.label_width("Delete ?   A Yes   B No", 12);
        hint = "Delete " + c.clip_label(what, room, 12) + "?   A Yes   B No";
        hintColor = accent;
    } else if (s.mode == BrowserMode::SET_HOME || s.mode == BrowserMode::FORGET_ROOT) {
        const std::string action = s.mode == BrowserMode::SET_HOME ? "Use as home: " : "Forget access: ";
        const int room = WIDTH - 40 - c.label_width(action + "?   A Yes   B No", 12);
        hint = action + c.clip_label(current ? current->displayName : "", room, 12) + "?   A Yes   B No";
        hintColor = t.textTitle;
    } else if (!s.statusMessage.empty()) {
        hint = s.statusMessage;
        hintColor = s.statusSuccess ? t.textTitle : 0xFFF5809B;
    } else if (s.selectionMode) {
        hint = "B Copy   L+A Cut   L+B Select all   L+R Cancel selection";
        hintColor = t.rowSelection;
    } else if (!s.fileClipboard.empty()) {
        hint = "L+A Paste   " + s.clipboard_info() + "   B Back";
        hintColor = t.textTitle;
    } else if (current && current->isRoot) {
        hint = "A Open   B Back   SEL+A Set home   SEL+B Forget access";
    } else if (total == 0) {
        hint = "B Back   R+Left Parent folder   SEL+R New folder";
    } else {
        hint = "A Open   B Back   R+Left Up   R+Up/Down Sort   SEL+A Rename   SEL+B Delete   SEL+R New";
    }
    c.fill_rect(x + 8, y + HEIGHT - ROW_HEIGHT, WIDTH - 16, 1,
                 with_alpha(confirming ? accent : t.textParam, confirming ? .45f : .18f));
    c.draw_label(c.clip_label(hint, WIDTH - 28, 12), x + 14, y + HEIGHT - 16, hintColor, 12);
}

}  // namespace pt::ui
