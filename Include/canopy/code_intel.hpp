#ifndef CANOPY_CODE_INTEL_HPP
#define CANOPY_CODE_INTEL_HPP

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace canopy {

struct CodeLocation {
    std::string file;
    std::size_t line = 0;
    std::size_t column = 0;
    std::size_t length = 0;
};

struct HoverResult {
    std::size_t line = 0;
    std::size_t column = 0;
    std::size_t length = 0;
    std::string markdown;
};

struct IntelRequest {
    std::string file;
    std::string language;
    std::string text;
    std::size_t line = 0;
    std::size_t column = 0;
};

struct Symbol {
    std::string name;
    std::string kind;
    std::string file;
    std::size_t line = 0;
    std::size_t column = 0;
    std::string signature;
    std::string doc;
    std::string value;
    bool definition = true;
};

std::vector<std::string> split_text_lines(const std::string& text);
std::optional<std::string> text_line_at(const std::string& text, std::size_t index);
std::pair<std::size_t, std::size_t> word_bounds(const std::string& line, std::size_t column, bool dashes = false);
std::string mask_c_like(const std::string& text);
std::string mask_c_like_line(const std::string& text, std::size_t line);
std::vector<Symbol> scan_c_symbols(const std::string& text, const std::string& file);
std::vector<Symbol> scan_cmake_symbols(const std::string& text, const std::string& file);
std::vector<Symbol> scan_bark_symbols(const std::string& text, const std::string& file);
std::optional<std::int64_t> evaluate_integer(const std::string& expression, const std::function<std::optional<std::string>(const std::string&)>& lookup = {});
std::string number_preview(const std::string& literal);
std::string color_preview(const std::string& literal);
std::string hover_link(const std::string& label, const CodeLocation& where);
std::optional<HoverResult> literal_hover(const std::string& language, const std::string& text, std::size_t line, std::size_t column);
std::optional<CodeLocation> parse_hover_link(const std::string& action);
std::string format_signature(const std::string& signature, std::size_t columns);
std::string format_hover_signatures(const std::string& markdown, std::size_t columns);

class CodeIntel {
public:
    void set_workspace(const std::string& root);
    void set_open_texts(std::function<std::optional<std::string>(const std::string&)> provider);
    void set_include_dirs(std::vector<std::string> dirs);
    void set_system_include_dirs(std::function<std::vector<std::string>()> provider);
    void set_bark_settings(std::function<std::optional<std::string>(const std::string& section, const std::string& key, const std::string& file, const std::string& text)> provider);

    std::optional<HoverResult> hover(const IntelRequest& request);
    std::optional<CodeLocation> definition(const IntelRequest& request);

    std::vector<Symbol> find(const std::string& name, const std::string& family, const std::string& near_file);
    std::optional<std::string> include_path(const std::string& from_file, const std::string& name, bool angled);
    std::optional<std::string> text_of(const std::string& path);
    std::vector<Symbol> symbols_near(const std::string& near_file, const std::string& family);
    std::vector<std::string> include_dirs_for(const std::string& from_file, bool angled);

private:
    struct FileEntry {
        std::filesystem::file_time_type time;
        std::uintmax_t size = 0;
        std::vector<Symbol> symbols;
        std::size_t hash = 0;
        std::string family;
        bool scanned = false;
        std::chrono::steady_clock::time_point at;
    };

    std::vector<std::string> workspace_files(const std::string& family);
    const std::vector<Symbol>& symbols_in(const std::string& path, const std::string& family);
    std::vector<std::string> includes_of(const std::string& path, const std::string& text);

    std::optional<HoverResult> c_hover(const IntelRequest& request);
    std::optional<CodeLocation> c_definition(const IntelRequest& request);
    std::optional<HoverResult> cmake_hover(const IntelRequest& request);
    std::optional<CodeLocation> cmake_definition(const IntelRequest& request);
    std::optional<HoverResult> bark_hover(const IntelRequest& request);
    std::optional<CodeLocation> bark_definition(const IntelRequest& request);
    std::optional<HoverResult> grove_hover(const IntelRequest& request);
    std::optional<CodeLocation> grove_definition(const IntelRequest& request);

    std::optional<std::string> macro_value(const std::string& name, const std::string& near_file, int depth);
    std::vector<std::pair<CodeLocation, std::string>> bark_rules(const std::string& selector);
    std::vector<std::pair<CodeLocation, std::string>> grove_uses(const std::string& kind, const std::string& name);
    std::vector<std::pair<CodeLocation, std::string>> quoted_uses(const std::string& word, std::size_t most);
    std::vector<std::pair<CodeLocation, std::string>> bark_property_uses(const std::string& property, const std::string& skip_file, std::size_t skip_line, std::size_t most);
    std::string bark_property_places(const std::string& property, const IntelRequest& request);

    std::string m_root;
    std::function<std::optional<std::string>(const std::string&)> m_open;
    std::vector<std::string> m_include_dirs;
    std::function<std::vector<std::string>()> m_system_provider;
    std::function<std::optional<std::string>(const std::string&, const std::string&, const std::string&, const std::string&)> m_bark_settings;
    std::optional<std::vector<std::string>> m_system_dirs;
    std::map<std::string, FileEntry> m_files;
    std::map<std::string, std::vector<std::string>> m_listing;
    std::map<std::string, std::chrono::steady_clock::time_point> m_listed_at;
    std::map<std::string, std::pair<std::chrono::steady_clock::time_point, std::vector<std::string>>> m_near_files;
    bool m_allow_stale = false;
};

} // namespace canopy

#endif // CANOPY_CODE_INTEL_HPP
