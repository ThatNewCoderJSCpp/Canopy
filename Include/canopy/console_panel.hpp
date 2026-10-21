#ifndef CANOPY_CONSOLE_PANEL_HPP
#define CANOPY_CONSOLE_PANEL_HPP

#include "live_server.hpp"

#include <grove/markup/events.hpp>
#include <grove/markup/node.hpp>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace canopy {

class ConsolePanel {
public:
    struct Hooks {
        std::function<std::vector<LiveServer*>()> servers;
        std::function<void(const std::string& path, int line, int column)> open_at;
        std::function<void(const std::string&)> show_panel;
        std::function<void()> serve_workspace;
    };

    ConsolePanel(grove::Document& doc, Hooks hooks);

    void install();
    void poll();
    void clear();
    void focus_input();
    void servers_changed();
    void add(const ConsoleMessage& message, const std::string& file);
    std::size_t errors() const noexcept { return m_errors; }
    std::size_t warnings() const noexcept { return m_warnings; }

private:
    struct Row {
        grove::Node* node = nullptr;
        grove::Node* count = nullptr;
        std::string level;
        std::string text;
        std::string source;
        std::string page;
        std::string page_id;
        int line = 0;
        int repeats = 1;
    };

    void listen(const std::string& id, const char* type, grove::Listener fn);
    void run_input();
    void history_step(int direction);
    void apply_filter();
    void apply_filter(Row& row);
    bool shown(const Row& row) const;
    void forget_page(const std::string& page, const std::string& keep_id);
    void note(const std::string& level, const std::string& text);
    void refresh_tab();
    void refresh_empty();
    std::string input_text() const;
    void set_input(const std::string& text);

    grove::Document& m_doc;
    Hooks m_hooks;
    std::vector<Row> m_rows;
    std::vector<std::string> m_history;
    std::size_t m_history_at = 0;
    std::string m_draft;
    std::string m_level = "info";
    std::string m_filter;
    bool m_preserve = false;
    std::size_t m_errors = 0;
    std::size_t m_warnings = 0;
};

} // namespace canopy

#endif // CANOPY_CONSOLE_PANEL_HPP
