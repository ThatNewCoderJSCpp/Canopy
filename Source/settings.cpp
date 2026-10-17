#include <canopy/settings.hpp>

#include <grove/bark/parser.hpp>
#include <grove/project.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

namespace canopy {

namespace {

std::string value_text(const grove::bark::ValueList& values) {
    if (values.size() == 1 && values.front().is_string()) return values.front().text;
    std::string out;
    for (const grove::bark::Value& v : values) {
        if (!out.empty()) out += ' ';
        out += v.is_string() ? v.text : grove::bark::to_string(v);
    }
    return out;
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
    settings.load_text(default_settings_text(), "<built-in settings>", SettingSource::Default, diagnostics);
    return settings;
}

bool Settings::load_text(std::string_view text, const std::string& file, SettingSource source, grove::Diagnostics& diagnostics) {
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

std::vector<Setting> Settings::all() const {
    std::vector<Setting> out;
    for (const auto& section : m_values) {
        for (const auto& key : section.second) out.push_back(key.second);
    }
    return out;
}

std::string user_config_dir() {
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
    const std::string user = user_settings_path();
    if (fs::is_regular_file(user, ec)) settings.load_file(user, SettingSource::User, diagnostics);
    if (!workspace_root.empty()) {
        const std::string ws = workspace_settings_path(workspace_root);
        if (fs::is_regular_file(ws, ec)) settings.load_file(ws, SettingSource::Workspace, diagnostics);
    }
    return settings;
}

} // namespace canopy
