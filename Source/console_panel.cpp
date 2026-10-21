#include "canopy/console_panel.hpp"
#include "canopy/input_text.hpp"

#include <grove/dom_tools.hpp>
#include <grove/input/input.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace fs = std::filesystem;

namespace canopy {

namespace {

constexpr std::size_t k_max_rows = 1000;

grove::Node* add_node(grove::Document& doc, grove::Node& parent, const std::string& tag, const std::string& text, const std::string& classes = {}) {
    grove::Node* el = doc.create_element(tag);
    if (!classes.empty()) el->set_attribute("class", classes);
    if (!text.empty()) el->set_inner_text(text);
    return doc.append_child(parent, *el);
}

const grove::Node* with_attribute(const grove::Node* node, const char* name) {
    for (const grove::Node* n = node; n; n = n->parent()) {
        if (n->is_element() && n->has_attribute(name)) return n;
    }
    return nullptr;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    const std::size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return std::string();
    const std::size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

const char* icon_for(const std::string& level) {
    if (level == "error") return "\xE2\x9C\x96";
    if (level == "warn") return "\xE2\x9A\xA0";
    if (level == "info") return "\xE2\x93\x98";
    if (level == "input") return "\xE2\x80\xBA";
    if (level == "result") return "\xE2\x80\xB9";
    if (level == "note") return "\xC2\xB7";
    return "";
}

int rank(const std::string& level) {
    if (level == "error") return 3;
    if (level == "warn") return 2;
    if (level == "debug") return 0;
    return 1;
}

int wanted(const std::string& filter) {
    if (filter == "errors") return 3;
    if (filter == "warnings") return 2;
    if (filter == "verbose") return 0;
    return 1;
}

} // namespace

ConsolePanel::ConsolePanel(grove::Document& doc, Hooks hooks) : m_doc(doc), m_hooks(std::move(hooks)) {}

void ConsolePanel::listen(const std::string& id, const char* type, grove::Listener fn) {
    if (grove::Node* n = m_doc.find_by_id(id)) m_doc.events().add_listener(n, type, std::move(fn));
}

void ConsolePanel::install() {
    listen("console-input", "key_down", [this](grove::Event& e) {
        if (e.key == "Enter") {
            e.default_prevented = true;
            run_input();
        } else if (e.key == "Up") {
            e.default_prevented = true;
            history_step(-1);
        } else if (e.key == "Down") {
            e.default_prevented = true;
            history_step(1);
        } else if ((e.key == "L" || e.key == "l") && (e.modifiers & (grove::ModifierControl | grove::ModifierMeta)) != 0) {
            e.default_prevented = true;
            clear();
        }
    });
    listen("console-run", "click", [this](grove::Event&) { run_input(); });
    listen("console-clear", "click", [this](grove::Event&) { clear(); });
    listen("console-level", "change", [this](grove::Event& e) {
        if (!e.target) return;
        m_level = grove::input_string(*e.target).value_or("info");
        apply_filter();
    });
    listen("console-filter", "input", [this](grove::Event& e) {
        if (!e.target) return;
        m_filter = lower(live_input_text(e));
        apply_filter();
    });
    listen("console-preserve", "change", [this](grove::Event& e) {
        if (e.target) m_preserve = grove::input_bool(*e.target).value_or(false);
    });
    listen("console-log", "click", [this](grove::Event& e) {
        if (const grove::Node* link = with_attribute(e.target, "data-console-file")) {
            const std::string file = link->attribute_or("data-console-file", "");
            const int line = std::max(1, std::atoi(link->attribute_or("data-console-line", "1").c_str()));
            const int column = std::max(1, std::atoi(link->attribute_or("data-console-column", "1").c_str()));
            if (!file.empty() && m_hooks.open_at) m_hooks.open_at(file, line, column);
            e.default_prevented = true;
            return;
        }
        if (with_attribute(e.target, "data-console-serve")) {
            if (m_hooks.serve_workspace) m_hooks.serve_workspace();
            e.default_prevented = true;
        }
    });
    if (grove::Node* level = m_doc.find_by_id("console-level")) m_level = grove::input_string(*level).value_or("info");
    if (grove::Node* keep = m_doc.find_by_id("console-preserve")) m_preserve = grove::input_bool(*keep).value_or(false);
    refresh_empty();
    refresh_tab();
}

void ConsolePanel::poll() {
    if (!m_hooks.servers) return;
    bool any = false;
    for (LiveServer* server : m_hooks.servers()) {
        if (!server) continue;
        for (const ConsoleMessage& message : server->take_console()) {
            any = true;
            if (message.level == "clear") {
                if (!m_preserve) clear();
                note("note", "The page cleared the console");
                continue;
            }
            if (message.level == "navigate") {
                if (!m_preserve) forget_page(message.page, std::string());
                else note("note", "Opened " + (message.page.empty() ? std::string("a page") : message.page));
                continue;
            }
            std::string file;
            if (!message.source.empty()) file = server->file_for_url(message.source);
            else if (!message.page.empty() && message.line > 0) file = server->file_for_url(message.page);
            add(message, file);
        }
    }
    if (any) {
        refresh_empty();
        refresh_tab();
    }
}

void ConsolePanel::add(const ConsoleMessage& message, const std::string& file) {
    grove::Node* log = m_doc.find_by_id("console-log");
    if (!log) return;
    if (!m_rows.empty()) {
        Row& last = m_rows.back();
        if (last.level == message.level && last.text == message.text && last.source == message.source && last.line == message.line && last.page == message.page &&
            message.level != "input" && message.level != "result") {
            ++last.repeats;
            if (last.count) {
                last.count->set_inner_text(std::to_string(last.repeats));
                grove::remove_class(m_doc, *last.count, "hidden");
            }
            if (message.level == "error") ++m_errors;
            if (message.level == "warn") ++m_warnings;
            m_doc.touch();
            return;
        }
    }
    Row row;
    row.level = message.level;
    row.text = message.text;
    row.source = message.source;
    row.page = message.page;
    row.page_id = message.page_id;
    row.line = message.line;
    row.node = add_node(m_doc, *log, "row", "", "console-row " + message.level);
    if (!message.page.empty()) row.node->set_attribute("title", "From " + message.page);
    add_node(m_doc, *row.node, "text", icon_for(message.level), "console-icon");
    add_node(m_doc, *row.node, "pre", message.text.empty() ? std::string(" ") : message.text, "console-text");
    row.count = add_node(m_doc, *row.node, "text", "1", "console-count hidden");
    std::string where;
    if (!message.source.empty()) where = message.source;
    else if (!file.empty()) where = message.page;
    if (!where.empty()) {
        std::string shown = fs::path(where.substr(0, where.find_first_of("?#"))).filename().string();
        if (shown.empty()) shown = where;
        if (message.line > 0) shown += ":" + std::to_string(message.line);
        grove::Node* link = add_node(m_doc, *row.node, "text", shown, file.empty() ? "console-source" : "console-source link");
        link->set_attribute("title", file.empty() ? where : file + (message.line > 0 ? ":" + std::to_string(message.line) : std::string()));
        if (!file.empty()) {
            link->set_attribute("data-console-file", file);
            link->set_attribute("data-console-line", std::to_string(std::max(1, message.line)));
            link->set_attribute("data-console-column", std::to_string(std::max(1, message.column)));
        }
    }
    if (message.level == "error") ++m_errors;
    if (message.level == "warn") ++m_warnings;
    m_rows.push_back(std::move(row));
    apply_filter(m_rows.back());
    while (m_rows.size() > k_max_rows) {
        if (m_rows.front().node) m_doc.remove(*m_rows.front().node);
        m_rows.erase(m_rows.begin());
    }
    m_doc.touch();
}

void ConsolePanel::note(const std::string& level, const std::string& text) {
    ConsoleMessage m;
    m.level = level;
    m.text = text;
    add(m, std::string());
}

void ConsolePanel::forget_page(const std::string& page, const std::string& keep_id) {
    bool changed = false;
    for (auto it = m_rows.begin(); it != m_rows.end();) {
        if (!it->page.empty() && it->page == page && (keep_id.empty() || it->page_id != keep_id)) {
            if (it->level == "error") m_errors -= std::min<std::size_t>(m_errors, static_cast<std::size_t>(it->repeats));
            if (it->level == "warn") m_warnings -= std::min<std::size_t>(m_warnings, static_cast<std::size_t>(it->repeats));
            if (it->node) m_doc.remove(*it->node);
            it = m_rows.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    if (changed) m_doc.touch();
}

void ConsolePanel::clear() {
    for (Row& row : m_rows) {
        if (row.node) m_doc.remove(*row.node);
    }
    m_rows.clear();
    m_errors = 0;
    m_warnings = 0;
    refresh_empty();
    refresh_tab();
    m_doc.touch();
}

void ConsolePanel::servers_changed() {
    refresh_empty();
}

void ConsolePanel::focus_input() {
    if (grove::Node* box = m_doc.find_by_id("console-input"); box && m_doc.host().focus) m_doc.host().focus(box);
}

std::string ConsolePanel::input_text() const {
    const grove::Node* box = m_doc.find_by_id("console-input");
    return box ? grove::input_string(*box).value_or("") : std::string();
}

void ConsolePanel::set_input(const std::string& text) {
    if (grove::Node* box = m_doc.find_by_id("console-input")) grove::set_input_string(m_doc, *box, text);
}

void ConsolePanel::run_input() {
    const std::string code = trim(input_text());
    if (code.empty()) return;
    if (m_history.empty() || m_history.back() != code) m_history.push_back(code);
    if (m_history.size() > 200) m_history.erase(m_history.begin());
    m_history_at = m_history.size();
    m_draft.clear();
    set_input("");
    note("input", code);
    std::vector<LiveServer*> servers = m_hooks.servers ? m_hooks.servers() : std::vector<LiveServer*>{};
    servers.erase(std::remove_if(servers.begin(), servers.end(), [](LiveServer* s) { return !s || !s->running() || !s->console(); }), servers.end());
    if (servers.empty()) {
        note("warn", "There's no page to run this in. Serve a folder from the Ports tab (or right-click an .html file and choose Serve with Local Server), open the page, and run JavaScript here.");
        refresh_empty();
        refresh_tab();
        return;
    }
    std::size_t pages = 0;
    for (LiveServer* s : servers) pages += s->evaluate(code);
    if (pages == 0) note("warn", "No page is open right now. Open the page in a browser and run it again.");
    else if (pages > 1) note("note", "Running in " + std::to_string(pages) + " open pages");
    refresh_empty();
    refresh_tab();
}

void ConsolePanel::history_step(int direction) {
    if (m_history.empty()) return;
    if (m_history_at >= m_history.size()) m_draft = input_text();
    if (direction < 0) {
        if (m_history_at == 0) return;
        --m_history_at;
    } else {
        if (m_history_at >= m_history.size()) return;
        ++m_history_at;
    }
    set_input(m_history_at >= m_history.size() ? m_draft : m_history[m_history_at]);
}

bool ConsolePanel::shown(const Row& row) const {
    if (row.level == "input" || row.level == "result" || row.level == "note") return m_filter.empty() || lower(row.text).find(m_filter) != std::string::npos;
    if (rank(row.level) < wanted(m_level)) return false;
    if (m_level == "errors" && row.level != "error") return false;
    if (m_filter.empty()) return true;
    return lower(row.text).find(m_filter) != std::string::npos || lower(row.source).find(m_filter) != std::string::npos;
}

void ConsolePanel::apply_filter(Row& row) {
    if (!row.node) return;
    if (shown(row)) grove::remove_class(m_doc, *row.node, "hidden");
    else grove::add_class(m_doc, *row.node, "hidden");
}

void ConsolePanel::apply_filter() {
    for (Row& row : m_rows) apply_filter(row);
    m_doc.touch();
}

void ConsolePanel::refresh_tab() {
    grove::Node* tab = m_doc.find_by_id("tab-console");
    if (!tab) return;
    std::string label = "CONSOLE";
    if (m_errors > 0) label += " " + std::to_string(m_errors);
    const auto& kids = tab->children();
    if (kids.size() == 1 && !kids.front()->is_element() && kids.front()->text() == label) return;
    tab->set_inner_text(label);
    m_doc.touch();
}

void ConsolePanel::refresh_empty() {
    grove::Node* empty = m_doc.find_by_id("console-empty");
    if (!empty) return;
    if (!m_rows.empty()) {
        grove::add_class(m_doc, *empty, "hidden");
        return;
    }
    grove::remove_class(m_doc, *empty, "hidden");
    bool serving = false;
    if (m_hooks.servers) {
        for (LiveServer* s : m_hooks.servers()) serving = serving || (s && s->running());
    }
    if (grove::Node* hint = m_doc.find_by_id("console-hint")) {
        const std::string text = serving
            ? std::string("Open a served page in your browser. Its console.log output, errors, failed scripts and stylesheets and missing files show up here, and you can run JavaScript in it below.")
            : std::string("The console shows what pages served by Canopy's local server log: console.log output, JavaScript errors, stylesheets and scripts that fail to load, and missing files. Serve a folder to start.");
        const auto& kids = hint->children();
        if (!(kids.size() == 1 && !kids.front()->is_element() && kids.front()->text() == text)) hint->set_inner_text(text);
    }
    if (grove::Node* serve = m_doc.find_by_id("console-serve")) serving ? (void)grove::add_class(m_doc, *serve, "hidden") : (void)grove::remove_class(m_doc, *serve, "hidden");
}

} // namespace canopy
