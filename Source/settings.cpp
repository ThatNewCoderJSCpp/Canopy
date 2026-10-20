#include <canopy/settings.hpp>

#include <grove/bark/parser.hpp>
#include <grove/input/code_buffer.hpp>
#include <grove/project.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace canopy {

namespace {

std::string value_text(const grove::bark::ValueList& values) {
    if (values.size() == 1 && values.front().is_string()) return values.front().text;
    std::string out;
    for (const grove::bark::Value& v : values) {
        if (!out.empty()) out += ' ';
        std::string piece = v.is_string() ? v.text : grove::bark::to_string(v);
        if (v.is_color() && piece.size() == 9 && piece.compare(7, 2, "ff") == 0) piece.resize(7);
        out += piece;
    }
    return out;
}

bool space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

enum : char { Code = 0, Comment = 1, Quoted = 2 };

std::vector<char> classify(std::string_view text) {
    std::vector<char> kind(text.size(), Code);
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            while (i < text.size() && text[i] != '\n') kind[i++] = Comment;
            continue;
        }
        if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '*') {
            const std::size_t close = text.find("*/", i + 2);
            const std::size_t end = close == std::string_view::npos ? text.size() : close + 2;
            while (i < end) kind[i++] = Comment;
            continue;
        }
        if (text[i] == '"' || text[i] == '\'') {
            const char quote = text[i];
            kind[i++] = Quoted;
            while (i < text.size() && text[i] != quote && text[i] != '\n') {
                if (text[i] == '\\' && i + 1 < text.size()) kind[i++] = Quoted;
                kind[i++] = Quoted;
            }
            if (i < text.size()) kind[i++] = Quoted;
            continue;
        }
        ++i;
    }
    return kind;
}

std::string code_only(std::string_view text, const std::vector<char>& kind, std::size_t begin, std::size_t end) {
    std::string out;
    bool gap = false;
    for (std::size_t i = begin; i < end; ++i) {
        if (kind[i] == Comment) {
            gap = true;
            continue;
        }
        if (space(text[i])) {
            gap = true;
            continue;
        }
        if (gap && !out.empty()) out += ' ';
        gap = false;
        out += text[i];
    }
    return out;
}

struct Block {
    std::string selector;
    std::size_t open = 0;
    std::size_t close = 0;
};

std::vector<Block> blocks_of(std::string_view text, const std::vector<char>& kind) {
    std::vector<Block> out;
    std::size_t start = 0;
    int depth = 0;
    Block current;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (kind[i] != Code) continue;
        if (text[i] == '{') {
            if (depth == 0) {
                current = Block{};
                current.selector = code_only(text, kind, start, i);
                current.open = i;
            }
            ++depth;
        } else if (text[i] == '}' && depth > 0) {
            if (--depth == 0) {
                current.close = i;
                out.push_back(current);
                start = i + 1;
            }
        } else if (text[i] == ';' && depth == 0) {
            start = i + 1;
        }
    }
    return out;
}

struct Declaration {
    std::string property;
    std::size_t begin = 0;
    std::size_t end = 0;
};

std::vector<Declaration> declarations_of(std::string_view text, const std::vector<char>& kind, const Block& block) {
    std::vector<Declaration> out;
    std::size_t start = block.open + 1;
    for (std::size_t i = block.open + 1; i <= block.close; ++i) {
        const bool last = i == block.close;
        if (!last && (kind[i] != Code || text[i] != ';')) continue;
        std::size_t begin = start;
        while (begin < i && (kind[begin] != Code || space(text[begin]))) ++begin;
        if (begin < i) {
            std::size_t colon = begin;
            while (colon < i && !(kind[colon] == Code && text[colon] == ':')) ++colon;
            if (colon < i) {
                Declaration d;
                d.property = code_only(text, kind, begin, colon);
                d.begin = begin;
                std::size_t end = last ? i : i + 1;
                if (last) {
                    while (end > begin && space(text[end - 1])) --end;
                }
                d.end = end;
                out.push_back(d);
            }
        }
        start = i + 1;
    }
    return out;
}

std::string file_header(const std::string& path) {
    const std::string name = fs::path(path).filename().string();
    if (!default_config_file(name).empty()) {
        return "// Your " + fs::path(name).stem().string() + " settings. Only what you change is kept here; everything else\n"
               "// comes from Canopy's built-in " + name + ". The Settings tab writes to this file.\n\n";
    }
    return std::string();
}

std::vector<std::string> theme_order() {
    return { "canopy-dark", "dark-modern", "neon", "amber", "ember", "soft", "ocean", "forest", "mono", "light" };
}

bool starts_with(const std::string& text, std::string_view prefix) {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

std::size_t edit_distance(const std::string& a, const std::string& b) {
    std::vector<std::size_t> row(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) row[j] = j;
    for (std::size_t i = 1; i <= a.size(); ++i) {
        std::size_t diagonal = row[0];
        row[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const std::size_t above = row[j];
            const std::size_t cost = a[i - 1] == b[j - 1] ? 0 : 1;
            row[j] = std::min({ row[j] + 1, row[j - 1] + 1, diagonal + cost });
            diagonal = above;
        }
    }
    return row[b.size()];
}

std::string suggestion(const std::string& name, const std::vector<std::string>& candidates) {
    const std::string best = closest_name(name, candidates);
    return best.empty() ? std::string() : " (did you mean '" + best + "'?)";
}

} // namespace

const char* to_string(SettingSource source) noexcept {
    switch (source) {
    case SettingSource::Default: return "default";
    case SettingSource::User: return "user";
    case SettingSource::Workspace: return "workspace";
    }
    return "?";
}

std::string closest_name(const std::string& name, const std::vector<std::string>& candidates) {
    std::string best;
    std::size_t best_distance = std::max<std::size_t>(2, name.size() / 3) + 1;
    for (const std::string& c : candidates) {
        const std::size_t d = edit_distance(name, c);
        if (d < best_distance) {
            best_distance = d;
            best = c;
        }
    }
    return best;
}

Settings Settings::defaults() {
    Settings settings;
    grove::Diagnostics diagnostics;
    for (const ConfigFile& f : default_config_files()) {
        if (f.name.find('/') != std::string_view::npos) continue;
        settings.load_text(f.text, "<built-in " + std::string(f.name) + ">", SettingSource::Default, diagnostics);
    }
    settings.allow("highlight", highlight_kinds());
    for (const std::string& language : highlight_languages()) settings.allow("highlight." + language, highlight_kinds());
    return settings;
}

std::uint64_t Settings::next_revision() noexcept {
    static std::atomic<std::uint64_t> counter{ 0 };
    return ++counter;
}

void Settings::allow(const std::string& section, const std::vector<std::string>& keys) {
    m_revision = next_revision();
    std::vector<std::string>& known = m_schema[section];
    for (const std::string& k : keys) {
        if (std::find(known.begin(), known.end(), k) == known.end()) known.push_back(k);
    }
}

bool Settings::load_text(std::string_view text, const std::string& file, SettingSource source, grove::Diagnostics& diagnostics) {
    m_revision = next_revision();
    grove::Diagnostics parsed;
    const grove::bark::Stylesheet sheet = grove::bark::parse_stylesheet(text, file, parsed);
    for (const grove::Diagnostic& d : parsed.items()) {
        if (d.severity == grove::Severity::Warning && (starts_with(d.message, "unknown property") || starts_with(d.message, "there is no <"))) continue;
        diagnostics.add(d.severity, d.file, d.span, d.message);
    }

    std::vector<std::string> known_sections;
    for (const auto& entry : m_schema) known_sections.push_back(entry.first);

    for (const grove::bark::Rule& rule : sheet.rules()) {
        for (const grove::bark::Selector& selector : rule.selectors) {
            const std::string section = selector.to_string();
            const bool checking = source != SettingSource::Default;
            const auto schema = m_schema.find(section);
            if (checking && schema == m_schema.end()) {
                diagnostics.warning(file, selector.span, "unknown settings section '" + section + "'" + suggestion(section, known_sections));
                continue;
            }
            for (const grove::bark::Declaration& d : rule.declarations) {
                if (checking && std::find(schema->second.begin(), schema->second.end(), d.property) == schema->second.end()) {
                    diagnostics.warning(file, d.span, "unknown setting '" + d.property + "' in " + section + suggestion(d.property, schema->second));
                    continue;
                }
                if (!checking) {
                    std::vector<std::string>& keys = m_schema[section];
                    if (std::find(keys.begin(), keys.end(), d.property) == keys.end()) keys.push_back(d.property);
                }
                Setting s;
                s.section = section;
                s.key = d.property;
                s.value = value_text(d.values);
                s.quoted = d.values.size() == 1 && d.values.front().is_string();
                s.source = source;
                s.file = file;
                s.line = d.span.begin.line;
                m_values[section][d.property] = std::move(s);
            }
        }
    }
    return diagnostics.ok();
}

bool Settings::load_file(const std::string& path, SettingSource source, grove::Diagnostics& diagnostics) {
    const std::optional<std::string> text = grove::read_file(path);
    if (!text) {
        diagnostics.error(path, grove::Span{}, "can't read settings file");
        return false;
    }
    return load_text(*text, path, source, diagnostics);
}

const Setting* Settings::find(const std::string& section, const std::string& key) const {
    const auto s = m_values.find(section);
    if (s == m_values.end()) return nullptr;
    const auto k = s->second.find(key);
    return k == s->second.end() ? nullptr : &k->second;
}

std::optional<std::string> Settings::get(const std::string& section, const std::string& key) const {
    const Setting* s = find(section, key);
    if (!s) return std::nullopt;
    return s->value;
}

std::string Settings::get_or(const std::string& section, const std::string& key, const std::string& fallback) const {
    const Setting* s = find(section, key);
    return s ? s->value : fallback;
}

std::optional<double> Settings::number(const std::string& section, const std::string& key) const {
    const Setting* s = find(section, key);
    if (!s) return std::nullopt;
    std::istringstream in(s->value);
    double v = 0.0;
    if (!(in >> v)) return std::nullopt;
    return v;
}

bool Settings::flag(const std::string& section, const std::string& key, bool fallback) const {
    const Setting* s = find(section, key);
    if (!s) return fallback;
    if (s->value == "true" || s->value == "yes" || s->value == "on") return true;
    if (s->value == "false" || s->value == "no" || s->value == "off") return false;
    return fallback;
}

bool Settings::is_known(const std::string& section, const std::string& key) const {
    const auto s = m_schema.find(section);
    return s != m_schema.end() && std::find(s->second.begin(), s->second.end(), key) != s->second.end();
}

std::vector<std::string> Settings::sections() const {
    std::vector<std::string> out;
    for (const auto& entry : m_values) out.push_back(entry.first);
    return out;
}

std::vector<std::string> Settings::known_sections() const {
    std::vector<std::string> out;
    for (const auto& entry : m_schema) out.push_back(entry.first);
    return out;
}

std::vector<std::string> Settings::keys(const std::string& section) const {
    const auto s = m_schema.find(section);
    return s == m_schema.end() ? std::vector<std::string>{} : s->second;
}

std::vector<Setting> Settings::all() const {
    std::vector<Setting> out;
    for (const auto& section : m_values) {
        for (const auto& key : section.second) out.push_back(key.second);
    }
    return out;
}

std::string user_config_dir() {
    const std::string over = env("CANOPY_CONFIG_DIR");
    if (!over.empty()) return over;
#if defined(_WIN32)
    std::string base = env("APPDATA");
    if (base.empty()) base = env("USERPROFILE");
    return (fs::path(base) / "Canopy").string();
#elif defined(__APPLE__)
    return (fs::path(env("HOME")) / "Library" / "Application Support" / "Canopy").string();
#else
    const std::string xdg = env("XDG_CONFIG_HOME");
    if (!xdg.empty()) return (fs::path(xdg) / "canopy").string();
    return (fs::path(env("HOME")) / ".config" / "canopy").string();
#endif
}

std::string user_settings_path() {
    const std::string over = env("CANOPY_SETTINGS");
    if (!over.empty()) return over;
    return (fs::path(user_config_dir()) / "settings.bark").string();
}

std::string workspace_settings_path(const std::string& workspace_root) {
    return (fs::path(workspace_root) / ".canopy" / "settings.bark").string();
}

std::string user_settings_template() {
    std::string out =
        "// Your Canopy settings.\n"
        "//\n"
        "// Only put the settings you want to change here; everything else keeps its\n"
        "// built-in value. Uncomment a line (and its block) to change it.\n"
        "// `canopy settings` shows every value in effect and where it came from.\n";
    const Settings defaults = Settings::defaults();
    for (const std::string& section : defaults.sections()) {
        out += "\n// " + section + " {\n";
        for (const Setting& s : defaults.all()) {
            if (s.section != section) continue;
            out += "//     " + s.key + ": " + (s.quoted ? "\"" + s.value + "\"" : s.value) + ";\n";
        }
        out += "// }\n";
    }
    return out;
}

Settings load_settings(const std::string& workspace_root, grove::Diagnostics& diagnostics) {
    Settings settings = Settings::defaults();
    std::error_code ec;
    for (const std::string& file : user_settings_files()) settings.load_file(file, SettingSource::User, diagnostics);
    if (!workspace_root.empty()) {
        const std::string ws = workspace_settings_path(workspace_root);
        if (fs::is_regular_file(ws, ec)) settings.load_file(ws, SettingSource::Workspace, diagnostics);
        for (const std::string& name : settings_file_names()) {
            const std::string file = workspace_config_file(workspace_root, name);
            if (fs::is_regular_file(file, ec)) settings.load_file(file, SettingSource::Workspace, diagnostics);
        }
    }
    return settings;
}

std::string_view default_config_file(std::string_view name) noexcept {
    for (const ConfigFile& f : default_config_files()) {
        if (f.name == name) return f.text;
    }
    return {};
}

const std::vector<std::string>& settings_file_names() {
    static const std::vector<std::string> names = { "editor.bark", "compilers.bark", "build.bark", "workbench.bark", "highlighting.bark", "keys.bark" };
    return names;
}

std::string settings_file_for_section(const std::string& section) {
    if (section == "editor" || section == "viewer" || section == "gui") return "editor.bark";
    if (section == "toolchain" || starts_with(section, "language.")) return "compilers.bark";
    if (section == "build") return "build.bark";
    if (section == "explorer" || section == "terminal" || section == "server" || section == "git" || section == "history" || section == "layout") return "workbench.bark";
    if (section == "keys") return "keys.bark";
    if (section == "highlight" || starts_with(section, "highlight.")) return "highlighting.bark";
    return "settings.bark";
}

const std::vector<std::string>& highlight_kinds() {
    static const std::vector<std::string> kinds = {
        "keyword", "type", "function", "namespace", "variable", "constant", "string", "escape",
        "number", "comment", "preprocessor", "operator", "tag", "attribute", "property", "plain",
        "control-keyword", "builtin-type", "declaration", "self", "this", "cast", "builtin-operator", "overloaded-operator",
        "enum-member", "template-scope", "variable-template", "unresolved", "error-underline", "warning-underline",
        "error-line", "background", "foreground",
    };
    return kinds;
}

const std::vector<std::string>& highlight_languages() {
    static const std::vector<std::string> languages = { "c", "cpp", "walnut", "grove", "bark", "cmake" };
    return languages;
}

std::string highlight_fallback(const std::string& kind) {
    const std::optional<grove::CodeTokenKind> k = grove::code_token_kind_from_name(kind);
    if (!k) return "";
    const grove::CodeTokenKind base = grove::code_token_fallback(*k);
    return base == *k ? std::string() : std::string(grove::to_string(base));
}

bool is_theme_color_kind(const std::string& kind) {
    return kind == "background" || kind == "foreground";
}

const std::vector<std::string>& size_units() {
    static const std::vector<std::string> units = { "px", "pt", "%", "vmin", "vmax", "vdiag", "vw", "vh" };
    return units;
}

std::optional<SizeValue> parse_size(const std::string& raw) {
    std::string text = raw;
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.erase(text.begin());
    if (!text.empty() && text.front() == '"' && text.back() == '"' && text.size() >= 2) text = text.substr(1, text.size() - 2);
    std::size_t end = 0;
    while (end < text.size() && (std::isdigit(static_cast<unsigned char>(text[end])) || text[end] == '.')) ++end;
    if (end == 0) return std::nullopt;
    SizeValue size;
    size.value = std::atof(text.substr(0, end).c_str());
    std::string unit = text.substr(end);
    while (!unit.empty() && unit.front() == ' ') unit.erase(unit.begin());
    if (unit.empty()) unit = "px";
    if (unit == "vwidth") unit = "vw";
    if (unit == "vheight") unit = "vh";
    if (std::find(size_units().begin(), size_units().end(), unit) == size_units().end()) return std::nullopt;
    size.unit = unit;
    return size;
}

std::string size_number(double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.4f", value);
    std::string out = buffer;
    while (!out.empty() && out.back() == '0') out.pop_back();
    if (!out.empty() && out.back() == '.') out.pop_back();
    return out.empty() ? std::string("0") : out;
}

std::string size_text(const SizeValue& size) {
    return size_number(size.value) + size.unit;
}

std::pair<double, double> size_range(const std::string& unit) {
    if (unit == "px") return { 4.0, 200.0 };
    if (unit == "pt") return { 3.0, 150.0 };
    if (unit == "%") return { 0.0, 100.0 };
    return { 0.0, 1.0 };
}

std::optional<double> size_pixels(const SizeValue& size, double w, double h) {
    if (size.unit == "px") return size.value;
    if (size.unit == "pt") return size.value * 96.0 / 72.0;
    if (w <= 0.0 || h <= 0.0) return std::nullopt;
    if (size.unit == "vw") return size.value * w;
    if (size.unit == "vh") return size.value * h;
    if (size.unit == "vmin") return size.value * std::min(w, h);
    if (size.unit == "vmax") return size.value * std::max(w, h);
    if (size.unit == "vdiag") return size.value * std::sqrt(w * w + h * h);
    if (size.unit == "%") return size.value / 100.0 * std::min(w, h);
    return std::nullopt;
}

std::optional<SizeValue> convert_size(const SizeValue& size, const std::string& unit, double w, double h) {
    const std::optional<double> px = size_pixels(size, w, h);
    if (!px) return std::nullopt;
    const std::optional<double> one = size_pixels(SizeValue{ 1.0, unit }, w, h);
    if (!one || *one <= 0.0) return std::nullopt;
    SizeValue out{ *px / *one, unit };
    const auto range = size_range(unit);
    out.value = std::clamp(out.value, range.first, range.second);
    const double digits = unit == "px" || unit == "pt" ? 10.0 : unit == "%" ? 100.0 : 10000.0;
    out.value = std::round(out.value * digits) / digits;
    return out;
}

std::string grove_size(const SizeValue& size, double zoom) {
    const double z = zoom > 0.0 ? zoom : 1.0;
    if (size.unit == "px" || size.unit == "pt") return size_number(size.value / z) + size.unit;
    if (size.unit == "%") return size_number(size.value) + "vmin";
    return size_number(size.value * 100.0) + size.unit;
}

double gui_zoom(const Settings& settings, double w, double h) {
    const std::optional<SizeValue> size = parse_size(settings.get_or("gui", "size", "13px"));
    if (!size) return 1.0;
    const std::optional<double> px = size_pixels(*size, w, h);
    if (!px || *px <= 0.0) return 1.0;
    return std::clamp(*px / 13.0, 0.5, 3.0);
}

std::string editor_font_size(const Settings& settings, double zoom) {
    const std::optional<SizeValue> size = parse_size(settings.get_or("editor", "font-size", "14px"));
    return grove_size(size.value_or(SizeValue{ 14.0, "px" }), zoom);
}

std::string highlight_value(const std::map<std::string, std::string>& colors, const std::string& kind) {
    for (std::string k = kind; !k.empty(); k = highlight_fallback(k)) {
        const auto it = colors.find(k);
        if (it != colors.end() && !it->second.empty()) return it->second;
    }
    return "";
}

std::string user_config_file(const std::string& name) {
    return (fs::path(user_config_dir()) / name).string();
}

std::string workspace_config_file(const std::string& workspace_root, const std::string& name) {
    return (fs::path(workspace_root) / ".canopy" / name).string();
}

std::string user_themes_dir() {
    return (fs::path(user_config_dir()) / "themes").string();
}

std::vector<std::string> user_settings_files() {
    std::vector<std::string> out;
    std::error_code ec;
    const std::string legacy = user_settings_path();
    if (fs::is_regular_file(legacy, ec)) out.push_back(legacy);
    for (const std::string& name : settings_file_names()) {
        const std::string file = user_config_file(name);
        if (file != legacy && fs::is_regular_file(file, ec)) out.push_back(file);
    }
    return out;
}

std::string bark_setting_value(const std::string& value, bool quoted) {
    if (!quoted) return value;
    std::string out = "\"";
    for (char c : value) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

std::string set_setting_in_text(std::string_view text, const std::string& section, const std::string& key, const std::string& value) {
    const std::vector<char> kind = classify(text);
    const std::vector<Block> blocks = blocks_of(text, kind);
    const Block* target = nullptr;
    const Declaration* found = nullptr;
    std::vector<Declaration> decls;
    for (const Block& b : blocks) {
        if (b.selector != section) continue;
        std::vector<Declaration> here = declarations_of(text, kind, b);
        target = &b;
        decls = std::move(here);
        found = nullptr;
        for (const Declaration& d : decls) {
            if (d.property == key) found = &d;
        }
        if (found) break;
    }
    std::string out(text);
    const std::string line = key + ": " + value + ";";
    if (found) {
        if (!value.empty()) return out.replace(found->begin, found->end - found->begin, line);
        std::size_t begin = found->begin;
        std::size_t end = found->end;
        std::size_t line_start = begin;
        while (line_start > 0 && (text[line_start - 1] == ' ' || text[line_start - 1] == '\t')) --line_start;
        std::size_t line_end = end;
        while (line_end < text.size() && (text[line_end] == ' ' || text[line_end] == '\t' || text[line_end] == '\r')) ++line_end;
        if ((line_start == 0 || text[line_start - 1] == '\n') && line_end < text.size() && text[line_end] == '\n') {
            begin = line_start;
            end = line_end + 1;
        }
        return out.erase(begin, end - begin);
    }
    if (value.empty()) return out;
    if (target) {
        std::size_t close = target->close;
        std::size_t line_start = close;
        while (line_start > 0 && (text[line_start - 1] == ' ' || text[line_start - 1] == '\t')) --line_start;
        if (line_start > 0 && text[line_start - 1] == '\n') return out.insert(line_start, "    " + line + "\n");
        std::size_t trim = close;
        while (trim > target->open + 1 && space(text[trim - 1])) --trim;
        return out.replace(trim, close - trim, "\n    " + line + "\n");
    }
    if (!out.empty() && out.back() != '\n') out += '\n';
    if (!out.empty() && (out.size() < 2 || out.compare(out.size() - 2, 2, "\n\n") != 0)) out += '\n';
    return out + section + " {\n    " + line + "\n}\n";
}

bool write_setting(const std::string& path, const std::string& section, const std::string& key, const std::string& value) {
    std::error_code ec;
    std::string text;
    if (fs::is_regular_file(path, ec)) {
        const std::optional<std::string> current = grove::read_file(path);
        if (!current) return false;
        text = *current;
    } else {
        if (value.empty()) return true;
        text = file_header(path);
    }
    const std::string updated = set_setting_in_text(text, section, key, value);
    if (updated == text && fs::exists(path, ec)) return true;
    fs::create_directories(fs::path(path).parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << updated;
    return static_cast<bool>(out);
}

std::optional<Theme> parse_theme(std::string_view text, const std::string& id, const std::string& file, grove::Diagnostics& diagnostics) {
    grove::Diagnostics parsed;
    const grove::bark::Stylesheet sheet = grove::bark::parse_stylesheet(text, file, parsed);
    for (const grove::Diagnostic& d : parsed.items()) {
        if (d.severity == grove::Severity::Warning && (starts_with(d.message, "unknown property") || starts_with(d.message, "there is no <"))) continue;
        diagnostics.add(d.severity, d.file, d.span, d.message);
    }
    if (!parsed.ok()) return std::nullopt;
    Theme theme;
    theme.id = id;
    theme.name = id;
    theme.file = file;
    for (const grove::bark::Rule& rule : sheet.rules()) {
        for (const grove::bark::Selector& selector : rule.selectors) {
            const std::string section = selector.to_string();
            for (const grove::bark::Declaration& d : rule.declarations) {
                const std::string v = value_text(d.values);
                if (section == "theme") {
                    if (d.property == "name") theme.name = v;
                    else if (d.property == "background") theme.background = v;
                    else if (d.property == "foreground") theme.foreground = v;
                } else if (section == "highlight" || starts_with(section, "highlight.")) {
                    theme.highlight[section][d.property] = v;
                }
            }
        }
    }
    return theme;
}

std::vector<Theme> load_themes(grove::Diagnostics& diagnostics) {
    std::vector<Theme> themes;
    auto add = [&](Theme theme) {
        for (Theme& t : themes) {
            if (t.id == theme.id) {
                t = std::move(theme);
                return;
            }
        }
        themes.push_back(std::move(theme));
    };
    for (const ConfigFile& f : default_config_files()) {
        const std::string name(f.name);
        if (!starts_with(name, "themes/")) continue;
        const std::string id = fs::path(name).stem().string();
        if (auto theme = parse_theme(f.text, id, "<built-in " + name + ">", diagnostics)) add(std::move(*theme));
    }
    std::vector<Theme> builtin = std::move(themes);
    themes.clear();
    for (const std::string& id : theme_order()) {
        for (Theme& t : builtin) {
            if (t.id == id) add(std::move(t));
        }
    }
    for (Theme& t : builtin) {
        if (!t.id.empty() && !find_theme(themes, t.id)) add(std::move(t));
    }
    std::error_code ec;
    std::vector<fs::path> mine;
    for (fs::directory_iterator it(user_themes_dir(), ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().extension() == ".bark" || it->path().extension() == ".txt") mine.push_back(it->path());
    }
    std::sort(mine.begin(), mine.end());
    for (const fs::path& path : mine) {
        const std::optional<std::string> text = grove::read_file(path.string());
        if (!text) continue;
        if (auto theme = parse_theme(*text, path.stem().string(), path.string(), diagnostics)) {
            theme->builtin = false;
            add(std::move(*theme));
        }
    }
    return themes;
}

const Theme* find_theme(const std::vector<Theme>& themes, const std::string& id) {
    for (const Theme& t : themes) {
        if (t.id == id) return &t;
    }
    return nullptr;
}

std::map<std::string, std::string> resolve_highlight(const Settings& settings, const Theme* theme, const std::string& language) {
    std::map<std::string, std::string> out;
    const std::string own = "highlight." + language;
    if (theme && !theme->background.empty()) out["background"] = theme->background;
    if (theme && !theme->foreground.empty()) out["foreground"] = theme->foreground;
    if (theme) {
        for (const char* section : { "highlight", "" }) {
            const std::string name = *section ? std::string(section) : own;
            const auto it = theme->highlight.find(name);
            if (it == theme->highlight.end()) continue;
            for (const auto& [key, value] : it->second) out[key] = value;
        }
    }
    for (const std::string& section : { std::string("highlight"), own }) {
        for (const std::string& key : highlight_kinds()) {
            if (auto v = settings.get(section, key); v && !v->empty()) out[key] = *v;
        }
    }
    for (grove::CodeTokenKind kind : grove::code_token_kinds_for(grove::code_language_from_name(language))) {
        const std::string name = grove::to_string(kind);
        if (out.count(name) || highlight_fallback(name).empty()) continue;
        const std::string value = highlight_value(out, name);
        if (!value.empty()) out[name] = value;
    }
    if (!out.count("error-underline")) out["error-underline"] = "#f14c4c";
    if (!out.count("warning-underline")) out["warning-underline"] = "#cca700";
    if (!out.count("error-line")) out["error-line"] = out["error-underline"];
    return out;
}

} // namespace canopy
