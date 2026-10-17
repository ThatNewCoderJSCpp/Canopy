#ifndef CANOPY_SETTINGS_HPP
#define CANOPY_SETTINGS_HPP

#include <grove/common/diagnostics.hpp>

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
    std::vector<std::string> sections() const;
    std::vector<Setting> all() const;

private:
    std::map<std::string, std::map<std::string, Setting>> m_values;
    std::map<std::string, std::vector<std::string>> m_schema;
};

std::string_view default_settings_text() noexcept;

std::string user_config_dir();
std::string user_settings_path();
std::string workspace_settings_path(const std::string& workspace_root);
std::string user_settings_template();

Settings load_settings(const std::string& workspace_root, grove::Diagnostics& diagnostics);

std::string closest_name(const std::string& name, const std::vector<std::string>& candidates);

} // namespace canopy

#endif // CANOPY_SETTINGS_HPP
