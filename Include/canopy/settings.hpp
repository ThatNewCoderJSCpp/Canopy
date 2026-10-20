#ifndef CANOPY_SETTINGS_HPP
#define CANOPY_SETTINGS_HPP

#include <grove/common/diagnostics.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace canopy {

enum class SettingSource {
    Default,
    User,
    Workspace
};

const char* to_string(SettingSource source) noexcept;

struct Setting {
    std::string section;
    std::string key;
    std::string value;
    SettingSource source = SettingSource::Default;
    std::string file;
    std::size_t line = 0;
    bool quoted = false;
};

class Settings {
public:
    static Settings defaults();

    bool load_text(std::string_view text, const std::string& file, SettingSource source, grove::Diagnostics& diagnostics);
    bool load_file(const std::string& path, SettingSource source, grove::Diagnostics& diagnostics);

    const Setting* find(const std::string& section, const std::string& key) const;
    std::optional<std::string> get(const std::string& section, const std::string& key) const;
    std::string get_or(const std::string& section, const std::string& key, const std::string& fallback) const;
    std::optional<double> number(const std::string& section, const std::string& key) const;
    bool flag(const std::string& section, const std::string& key, bool fallback = false) const;

    bool is_known(const std::string& section, const std::string& key) const;
    void allow(const std::string& section, const std::vector<std::string>& keys);
    std::vector<std::string> sections() const;
    std::vector<std::string> known_sections() const;
    std::vector<std::string> keys(const std::string& section) const;
    std::vector<Setting> all() const;
    std::uint64_t revision() const noexcept { return m_revision; }

private:
    static std::uint64_t next_revision() noexcept;

    std::map<std::string, std::map<std::string, Setting>> m_values;
    std::map<std::string, std::vector<std::string>> m_schema;
    std::uint64_t m_revision = next_revision();
};

struct ConfigFile {
    std::string_view name;
    std::string_view text;
};

struct Theme {
    std::string id;
    std::string name;
    std::string file;
    std::string background;
    std::string foreground;
    std::map<std::string, std::map<std::string, std::string>> highlight;
    bool builtin = true;
};

std::string_view default_settings_text() noexcept;
const std::vector<ConfigFile>& default_config_files() noexcept;
std::string_view default_config_file(std::string_view name) noexcept;

const std::vector<std::string>& settings_file_names();
std::string settings_file_for_section(const std::string& section);
const std::vector<std::string>& highlight_kinds();
const std::vector<std::string>& highlight_languages();
std::string highlight_fallback(const std::string& kind);
std::string highlight_value(const std::map<std::string, std::string>& colors, const std::string& kind);
bool is_theme_color_kind(const std::string& kind);

struct SizeValue {
    double value = 0.0;
    std::string unit = "px";
};

const std::vector<std::string>& size_units();
std::optional<SizeValue> parse_size(const std::string& text);
std::string size_number(double value);
std::string size_text(const SizeValue& size);
std::pair<double, double> size_range(const std::string& unit);
std::optional<double> size_pixels(const SizeValue& size, double viewport_width, double viewport_height);
std::optional<SizeValue> convert_size(const SizeValue& size, const std::string& unit, double viewport_width, double viewport_height);
std::string grove_size(const SizeValue& size, double zoom);
double gui_zoom(const Settings& settings, double viewport_width, double viewport_height);
std::string editor_font_size(const Settings& settings, double zoom);

std::string user_config_file(const std::string& name);
std::string workspace_config_file(const std::string& workspace_root, const std::string& name);
std::string user_themes_dir();
std::vector<std::string> user_settings_files();

std::string bark_setting_value(const std::string& value, bool quoted);
std::string set_setting_in_text(std::string_view text, const std::string& section, const std::string& key, const std::string& value);
bool write_setting(const std::string& path, const std::string& section, const std::string& key, const std::string& value);

std::optional<Theme> parse_theme(std::string_view text, const std::string& id, const std::string& file, grove::Diagnostics& diagnostics);
std::vector<Theme> load_themes(grove::Diagnostics& diagnostics);
const Theme* find_theme(const std::vector<Theme>& themes, const std::string& id);
std::map<std::string, std::string> resolve_highlight(const Settings& settings, const Theme* theme, const std::string& language);

std::string user_config_dir();
std::string user_settings_path();
std::string workspace_settings_path(const std::string& workspace_root);
std::string user_settings_template();

Settings load_settings(const std::string& workspace_root, grove::Diagnostics& diagnostics);

std::string closest_name(const std::string& name, const std::vector<std::string>& candidates);

} // namespace canopy

#endif // CANOPY_SETTINGS_HPP
