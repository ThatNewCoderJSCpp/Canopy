#include <canopy/shortcuts.hpp>

#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace canopy {

namespace {

grove::Chord key(const char* text) {
    return grove::parse_chord(text).value_or(grove::Chord{});
}

Shortcut general(const char* id, const char* label, const char* help, std::vector<grove::Chord> defaults) {
    Shortcut s;
    s.id = id;
    s.label = label;
    s.help = help;
    s.group = "General";
    s.defaults = std::move(defaults);
    return s;
}

Shortcut fixed(const char* label, const char* help, const char* group, const char* shown) {
    Shortcut s;
    s.id = std::string("fixed:") + label;
    s.label = label;
    s.help = help;
    s.group = group;
    s.fixed = true;
    s.fixed_text = shown;
    return s;
}

std::string unquote(std::string text) {
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.pop_back();
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.erase(text.begin());
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"') text = text.substr(1, text.size() - 2);
    return text;
}

std::size_t chord_parts(const std::string& text) {
    std::size_t parts = 0;
    bool any = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == ',' && !(i > 0 && text[i - 1] == '+')) {
            if (any) ++parts;
            any = false;
        } else if (text[i] != ' ') {
            any = true;
        }
    }
    return parts + (any ? 1 : 0);
}

} // namespace

const std::vector<std::string>& shortcut_groups() {
    static const std::vector<std::string> groups = { "General", "Editor", "Mouse" };
    return groups;
}

const std::vector<Shortcut>& shortcuts() {
    static const std::vector<Shortcut> list = [] {
        std::vector<Shortcut> out = {
            general("save", "Save", "Save the file you're editing", { key("Ctrl+S") }),
            general("build", "Build", "Build the file you're editing (or rebuild a LaTeX preview)", { key("Ctrl+B") }),
            general("run", "Run", "Build and run the file you're editing", { key("F5") }),
            general("close-tab", "Close tab", "Close the tab you're on", { key("Ctrl+W") }),
            general("show-problems", "Show Problems", "Open the Problems tab in the panel", { key("Ctrl+Shift+M") }),
            general("show-warnings", "Show Warnings", "Open the Warnings tab in the panel", {}),
            general("show-output", "Show Output", "Open the Output tab in the panel", { key("Ctrl+Shift+U") }),
            general("open-settings", "Open Settings", "Open the Settings tab", { key("Ctrl+,") }),
            general("open-developer", "Open Developer Info", "Open the Developer tab: window size, frame times, CPU, memory, GPU and system details (clicking the version in the status bar opens it too)", { key("Ctrl+Shift+I") }),
            general("definition", "Go to definition", "Jump to where the name at the caret is defined", { key("F12") }),
            general("go-back", "Go back", "Back to where you were before jumping", { key("Alt+Left") }),
            general("go-forward", "Go forward", "Forward again after going back", { key("Alt+Right") }),
            general("show-terminal", "Show Terminal", "Open the Terminal tab in the panel", { key("Ctrl+`") }),
            general("new-terminal", "New Terminal", "Start another terminal in the current terminal's folder", { key("Ctrl+Shift+`") }),
            general("save-all", "Save All", "Save every file with unsaved changes", { key("Ctrl+Alt+S") }),
            general("show-git", "Show Git", "Open the Git tab in the panel: changes, commit, push, pull, branches and tasks", { key("Ctrl+Shift+G") }),
            general("show-history", "Show File History", "Open the History tab for the file you're editing: your saves and its Git commits", { key("Ctrl+Shift+H") }),
            general("git-commit", "Git: Commit", "Commit with the message typed in the Git tab (stages everything if nothing is staged)", {}),
            general("git-commit-push", "Git: Commit and Push", "Commit with the message in the Git tab, then push", {}),
            general("git-push", "Git: Push", "Push the current branch", {}),
            general("git-pull", "Git: Pull", "Pull the current branch", {}),
            general("run-task", "Run Task", "Pick a task from .canopy/tasks.bark and run it", { key("Ctrl+Shift+R") }),
            general("find", "Find", "Find in the file you're editing: every match is highlighted, with arrows to step through them", { key("Ctrl+F") }),
            general("replace", "Replace", "Find and replace in the file you're editing", { key("Ctrl+H") }),
            general("find-next", "Find next", "Go to the next match", { key("F3") }),
            general("find-previous", "Find previous", "Go to the previous match", { key("Shift+F3") }),
            general("select-all-matches", "Select all matches", "Put a caret on every match, so typing changes them all at once", { key("Alt+Enter") }),
        };
        for (const grove::KeyAction& a : grove::code_editor_actions()) {
            out.push_back(Shortcut{ a.id, a.label, a.help, a.mouse ? "Mouse" : "Editor", a.defaults, true, a.mouse, false, std::string() });
        }
        out.push_back(fixed("Show suggestions", "Open the suggestion list at the caret, even when it didn't open by itself", "Editor", "Ctrl+Space"));
        out.push_back(fixed("Accept a suggestion", "Put the highlighted suggestion in (Enter too, if that's turned on in Settings)", "Editor", "Tab"));
        out.push_back(fixed("Close suggestions", "Hide the suggestion list until you start another word", "Editor", "Escape"));
        out.push_back(fixed("Select a word", "Pick the whole word under the mouse", "Mouse", "Double-click"));
        out.push_back(fixed("Select text", "Drag across the text to select it", "Mouse", "Drag"));
        out.push_back(fixed("Extend the selection", "Select from the caret to where you click", "Mouse", "Shift+Click"));
        out.push_back(fixed("Scroll sideways", "Scroll the code, an image or a page sideways", "Mouse", "Shift+Wheel"));
        out.push_back(fixed("Pick a hover link", "In a hover popup, follow a link or flip between overloads", "Mouse", "Click"));
        return out;
    }();
    return list;
}

const Shortcut* find_shortcut(const std::string& id) {
    for (const Shortcut& s : shortcuts()) {
        if (s.id == id) return &s;
    }
    return nullptr;
}

std::vector<ShortcutBinding> shortcut_bindings(const Settings& settings) {
    std::vector<ShortcutBinding> out;
    for (const Shortcut& s : shortcuts()) {
        if (s.fixed) continue;
        ShortcutBinding b;
        b.shortcut = &s;
        b.chords = s.defaults;
        b.text = grove::chords_text(s.defaults);
        if (const Setting* set = settings.find("keys", s.id)) {
            b.text = unquote(set->value);
            b.file = set->file;
            b.line = set->line;
            b.custom = set->source != SettingSource::Default;
            std::string lowered = b.text;
            std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lowered == "none" || lowered.empty()) {
                b.chords.clear();
            } else {
                b.chords = grove::parse_chords(b.text);
                b.invalid = b.chords.size() != chord_parts(b.text);
                for (const grove::Chord& c : b.chords) b.invalid = b.invalid || c.mouse() != s.mouse;
            }
        }
        out.push_back(std::move(b));
    }
    return out;
}

std::string shortcut_where(const ShortcutBinding& b) {
    if (!b.custom || b.file.empty()) return "Canopy's default, in Settings > Shortcuts > " + (b.shortcut ? b.shortcut->group : std::string("General"));
    return fs::path(b.file).filename().string() + " line " + std::to_string(b.line) + " (" + b.file + ")";
}

std::vector<ShortcutConflict> shortcut_conflicts(const std::vector<ShortcutBinding>& bindings) {
    std::vector<ShortcutConflict> out;
    for (std::size_t i = 0; i < bindings.size(); ++i) {
        for (std::size_t j = i + 1; j < bindings.size(); ++j) {
            const ShortcutBinding& a = bindings[i];
            const ShortcutBinding& b = bindings[j];
            for (const grove::Chord& c : a.chords) {
                if (std::find(b.chords.begin(), b.chords.end(), c) == b.chords.end()) continue;
                ShortcutConflict k;
                k.chord = c;
                k.first = a.shortcut;
                k.second = b.shortcut;
                k.first_where = shortcut_where(a);
                k.second_where = shortcut_where(b);
                const ShortcutBinding& changed = b.custom ? b : a;
                k.file = changed.custom ? changed.file : std::string();
                k.line = changed.custom ? changed.line : 0;
                k.message = grove::chord_text(c) + " is used by both \"" + a.shortcut->label + "\" (" + a.shortcut->group + ") and \"" + b.shortcut->label + "\" (" + b.shortcut->group + "). \"" +
                            a.shortcut->label + "\" is set in " + k.first_where + "; \"" + b.shortcut->label + "\" is set in " + k.second_where + ".";
                out.push_back(std::move(k));
            }
        }
    }
    return out;
}

std::vector<std::string> shortcut_errors(const std::vector<ShortcutBinding>& bindings) {
    std::vector<std::string> out;
    for (const ShortcutBinding& b : bindings) {
        if (!b.invalid) continue;
        out.push_back(shortcut_where(b) + ": \"" + b.text + "\" for \"" + b.shortcut->label + "\" isn't a shortcut Canopy understands. Write it like " +
                      (b.shortcut->mouse ? std::string("Ctrl+Click or Alt+Shift+Click") : std::string("Ctrl+Shift+B, Alt+Left or F5")) + ", with commas between several, or none.");
    }
    return out;
}

const ShortcutBinding* binding_for(const std::vector<ShortcutBinding>& bindings, const std::string& id) {
    for (const ShortcutBinding& b : bindings) {
        if (b.shortcut && b.shortcut->id == id) return &b;
    }
    return nullptr;
}

std::optional<std::string> command_for(const std::vector<ShortcutBinding>& bindings, const grove::Chord& pressed) {
    if (pressed.empty()) return std::nullopt;
    for (const ShortcutBinding& b : bindings) {
        if (!b.shortcut || b.shortcut->editor) continue;
        if (std::find(b.chords.begin(), b.chords.end(), pressed) != b.chords.end()) return b.shortcut->id;
    }
    return std::nullopt;
}

std::string editor_keymap(const std::vector<ShortcutBinding>& bindings) {
    grove::Keymap map;
    for (const ShortcutBinding& b : bindings) {
        if (!b.shortcut || !b.shortcut->editor || !b.custom || b.invalid || b.chords == b.shortcut->defaults) continue;
        map[b.shortcut->id] = b.chords;
    }
    return grove::keymap_text(map);
}

} // namespace canopy
