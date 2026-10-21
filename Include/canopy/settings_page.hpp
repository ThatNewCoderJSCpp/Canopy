#ifndef CANOPY_SETTINGS_PAGE_HPP
#define CANOPY_SETTINGS_PAGE_HPP

#include "settings.hpp"
#include "workspace.hpp"

#include <grove/markup/events.hpp>
#include <grove/markup/node.hpp>

#include <functional>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace canopy {

struct HighlightLook {
    std::string color;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikethrough = false;
};

struct SettingDoc {
    std::string section;
    std::string key;
    std::string label;
    std::string help;
};

const std::vector<SettingDoc>& setting_docs();
std::optional<SettingDoc> find_setting_doc(const std::string& section, const std::string& key);

HighlightLook parse_highlight_look(const std::string& value);
std::string highlight_look_text(const HighlightLook& look);
std::string theme_id(const Settings& settings);
void apply_highlight(grove::Document& doc, grove::Node& editor, const Settings& settings, const std::vector<Theme>& themes, const std::string& language);
void apply_highlight_colors(grove::Document& doc, grove::Node& editor, const std::map<std::string, std::string>& colors, const Theme* theme);
std::string theme_slug(const std::string& name);
std::string theme_bark_text(const std::string& name, const std::string& background, const std::string& foreground, const std::map<std::string, std::map<std::string, std::string>>& sections, const std::string& made_from);
std::string theme_json_text(const std::string& name, const std::string& background, const std::string& foreground, const std::map<std::string, std::map<std::string, std::string>>& sections);
std::string highlight_sample(const std::string& language);
std::string highlight_sample_spans(const std::string& language);
std::string highlight_sample_problems(const std::string& language);

class SettingsPage {
public:
    struct Hooks {
        std::function<void()> changed;
        std::function<void(const std::string&)> open_file;
        std::function<std::string()> project_file;
        std::function<void(const std::string&)> project_names_changed;
    };

    SettingsPage(grove::Document& doc, Workspace& workspace, std::vector<Theme>& themes, Hooks hooks);

    grove::Node* create();
    grove::Node* root() const { return m_root; }
    void forget() { m_root = nullptr; }
    void show(const std::string& page);
    void refresh();
    void rebuild();
    const std::string& page() const { return m_page; }
    bool record_key(grove::Event& e);
    const std::string& recording() const { return m_recording; }

private:
    grove::Node* add(grove::Node& parent, const std::string& tag, const std::string& text, const std::string& classes = {});
    void build_nav(grove::Node& nav);
    void build_editor(grove::Node& body);
    void build_compilers(grove::Node& body);
    void build_build(grove::Node& body);
    void build_workbench(grove::Node& body);
    void build_viewers(grove::Node& body);
    void build_gui(grove::Node& body);
    void build_highlighting(grove::Node& body);
    void build_shortcuts(grove::Node& body);
    void build_characters(grove::Node& body);
    void fill_character_search(grove::Node& results, const std::string& query);
    void edit_project_name(std::size_t index, const std::string& name, const std::string& value);
    void remove_project_name(std::size_t index);
    void add_project_name();
    void set_names_for_code(std::uint32_t code, const std::string& typed);
    std::string character_file() const;
    void write_shortcut(const std::string& id, const std::string& text);
    void heading(grove::Node& body, const std::string& title, const std::string& note);
    grove::Node* setting_row(grove::Node& parent, const std::string& section, const std::string& key);
    grove::Node* setting_row(grove::Node& parent, const std::string& section, const std::string& key, const std::string& label, const std::string& help);
    void text_control(grove::Node& row, const std::string& section, const std::string& key, const std::string& width = "260px");
    void integer_control(grove::Node& row, const std::string& section, const std::string& key, int min, int max);
    void bool_control(grove::Node& row, const std::string& section, const std::string& key);
    void size_control(grove::Node& row, const std::string& section, const std::string& key, const std::string& fallback);
    void font_control(grove::Node& row, const std::string& section, const std::string& key);
    void on_size_change(grove::Node& target);
    double page_zoom() const;
    void choice_control(grove::Node& row, const std::string& section, const std::string& key, const std::vector<std::string>& choices);
    void multi_control(grove::Node& row, const std::string& section, const std::string& key, const std::vector<std::string>& choices);
    void tool_row(grove::Node& body, const std::string& key, const std::string& label);
    void viewer_tool_row(grove::Node& body, const std::string& key, const std::string& label, const std::string& kinds);
    void theme_cards(grove::Node& body);
    void color_row(grove::Node& list, const std::string& kind);

    void on_click(grove::Event& e);
    void on_change(grove::Event& e);
    void on_color_input(grove::Event& e);
    void on_preview_cursor(grove::Event& e);
    void mark(const std::string& kind);

    std::string target_file(const std::string& section, const std::string& key) const;
    void write(const std::string& section, const std::string& key, const std::string& value);
    void reset(const std::string& section, const std::string& key);
    std::string highlight_section() const;
    HighlightLook current_look(const std::string& kind) const;
    void write_look(const std::string& kind, const HighlightLook& look);

    using Sections = std::map<std::string, std::map<std::string, std::string>>;
    Sections saved_draft() const;
    void load_draft();
    bool draft_dirty() const;
    const Theme* draft_base() const;
    std::map<std::string, std::string> draft_colors(const std::string& language) const;
    Sections draft_theme_sections() const;
    void show_draft();
    void set_notice(const std::string& text);
    void apply_draft();
    void ask_theme_name();
    void save_theme(const std::string& name);
    void copy_theme(bool json);

    grove::Document& m_doc;
    Workspace& m_ws;
    std::vector<Theme>& m_themes;
    Hooks m_hooks;
    grove::Node* m_root = nullptr;
    grove::Node* m_body = nullptr;
    grove::Node* m_nav = nullptr;
    std::string m_body_page;
    grove::Node* m_preview = nullptr;
    std::string m_page = "editor";
    std::string m_language = "cpp";
    std::string m_marked;
    std::string m_picked;
    bool m_pending = false;
    bool m_draft_ready = false;
    std::string m_draft_theme;
    Sections m_draft;
    std::string m_saved_theme;
    Sections m_saved;
    std::string m_notice;
    std::string m_recording;
    std::string m_char_search;
    std::set<std::string> m_char_open;
    grove::Node* m_status = nullptr;
};

} // namespace canopy

#endif // CANOPY_SETTINGS_PAGE_HPP
