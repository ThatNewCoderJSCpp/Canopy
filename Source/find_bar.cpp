#include "canopy/find_bar.hpp"
#include "canopy/input_text.hpp"

#include <grove/dom_tools.hpp>
#include <grove/input/input.hpp>

namespace canopy {

FindBar::FindBar(grove::Document& doc, Hooks hooks) : m_doc(doc), m_hooks(std::move(hooks)) {}

void FindBar::listen(const std::string& id, const char* type, grove::Listener fn) {
    if (grove::Node* n = m_doc.find_by_id(id)) m_doc.events().add_listener(n, type, std::move(fn));
}

void FindBar::install() {
    listen("find-query", "input", [this](grove::Event& e) {
        m_query = live_input_text(e);
        apply(m_editor);
    });
    listen("replace-query", "input", [this](grove::Event& e) {
        m_replacement = live_input_text(e);
        apply(m_editor);
    });
    listen("find-query", "key_down", [this](grove::Event& e) {
        const bool alt = (e.modifiers & grove::ModifierAlt) != 0;
        const bool shift = (e.modifiers & grove::ModifierShift) != 0;
        if (e.key == "Enter") {
            e.default_prevented = true;
            m_query = field("find-query");
            apply(m_editor);
            if (alt) {
                select_all();
                return;
            }
            shift ? previous() : next();
            focus_query();
        } else if (e.key == "Escape") {
            e.default_prevented = true;
            close();
        } else if (alt && (e.key == "C" || e.key == "c")) {
            e.default_prevented = true;
            toggle(m_case, "find-case");
        } else if (alt && (e.key == "W" || e.key == "w")) {
            e.default_prevented = true;
            toggle(m_word, "find-word");
        } else if (alt && (e.key == "R" || e.key == "r")) {
            e.default_prevented = true;
            toggle(m_regex, "find-regex");
        }
    });
    listen("replace-query", "key_down", [this](grove::Event& e) {
        if (e.key == "Enter") {
            e.default_prevented = true;
            m_replacement = field("replace-query");
            apply(m_editor);
            const unsigned both = grove::ModifierControl | grove::ModifierAlt;
            if ((e.modifiers & both) == both) replace_all();
            else replace_one();
            if (grove::Node* box = m_doc.find_by_id("replace-query"); box && m_doc.host().focus) m_doc.host().focus(box);
        } else if (e.key == "Escape") {
            e.default_prevented = true;
            close();
        }
    });
    listen("find-case", "click", [this](grove::Event&) { toggle(m_case, "find-case"); });
    listen("find-word", "click", [this](grove::Event&) { toggle(m_word, "find-word"); });
    listen("find-regex", "click", [this](grove::Event&) { toggle(m_regex, "find-regex"); });
    listen("find-next", "click", [this](grove::Event&) { next(); });
    listen("find-prev", "click", [this](grove::Event&) { previous(); });
    listen("find-select-all", "click", [this](grove::Event&) { select_all(); });
    listen("find-close", "click", [this](grove::Event&) { close(); });
    listen("find-toggle-replace", "click", [this](grove::Event&) { show_replace(!m_replace); });
    listen("replace-one", "click", [this](grove::Event&) {
        m_replacement = field("replace-query");
        apply(m_editor);
        replace_one();
    });
    listen("replace-all", "click", [this](grove::Event&) {
        m_replacement = field("replace-query");
        apply(m_editor);
        replace_all();
    });
    listen("editors", "find", [this](grove::Event& e) {
        if (!m_open || e.target != m_editor) return;
        m_current = static_cast<std::size_t>(e.x);
        m_count = static_cast<std::size_t>(e.y);
        if (m_replaced && m_count == 0) return;
        m_replaced = false;
        if (e.key == "bad-pattern") set_status("Invalid pattern", true);
        else if (m_query.empty()) set_status("", false);
        else if (m_count == 0) set_status("No results", true);
        else if (m_current == 0) set_status(std::to_string(m_count) + (m_count == 1 ? " match" : " matches"), false);
        else set_status(std::to_string(m_current) + " of " + std::to_string(m_count), false);
    });
    listen("editors", "replace", [this](grove::Event& e) {
        if (e.target != m_editor) return;
        const std::size_t n = static_cast<std::size_t>(e.x);
        m_replaced = true;
        set_status("Replaced " + std::to_string(n) + (n == 1 ? " match" : " matches"), false);
    });
    listen("editors", "find-seed", [this](grove::Event& e) {
        if (e.target != m_editor || e.text.empty()) return;
        set_query(e.text);
    });
}

void FindBar::open(bool replace) {
    grove::Node* bar = m_doc.find_by_id("find-bar");
    if (!bar) return;
    m_editor = m_hooks.editor ? m_hooks.editor() : nullptr;
    const bool was_open = m_open;
    m_open = true;
    grove::remove_class(m_doc, *bar, "hidden");
    show_replace(replace || (was_open && m_replace));
    apply(m_editor);
    command("seed");
    focus_query();
    m_doc.touch();
}

void FindBar::close() {
    if (!m_open) return;
    m_open = false;
    if (grove::Node* bar = m_doc.find_by_id("find-bar")) grove::add_class(m_doc, *bar, "hidden");
    clear(m_editor);
    if (m_hooks.focus_editor) m_hooks.focus_editor();
    m_doc.touch();
}

void FindBar::next() {
    if (!m_open) {
        open(false);
        if (m_query.empty()) return;
    }
    command("next");
}

void FindBar::previous() {
    if (!m_open) {
        open(false);
        if (m_query.empty()) return;
    }
    command("prev");
}

void FindBar::select_all() {
    if (!m_open || m_query.empty()) return;
    command("select-all");
    if (m_hooks.focus_editor) m_hooks.focus_editor();
}

void FindBar::replace_one() {
    if (!m_open || m_query.empty()) return;
    command("replace");
}

void FindBar::replace_all() {
    if (!m_open || m_query.empty()) return;
    command("replace-all");
}

void FindBar::editor_changed() {
    grove::Node* now = m_hooks.editor ? m_hooks.editor() : nullptr;
    if (now == m_editor) return;
    clear(m_editor);
    m_editor = now;
    if (m_open) {
        set_status("", false);
        apply(m_editor);
    }
}

void FindBar::forget(const grove::Node* editor) {
    if (editor && editor == m_editor) m_editor = nullptr;
}

void FindBar::apply(grove::Node* editor) {
    if (!editor || !m_open) return;
    if (editor->attribute_or("find", "") != m_query) m_replaced = false;
    editor->set_attribute("find", m_query);
    editor->set_attribute("find-case", m_case ? "true" : "false");
    editor->set_attribute("find-word", m_word ? "true" : "false");
    editor->set_attribute("find-regex", m_regex ? "true" : "false");
    editor->set_attribute("replace", m_replacement);
    m_doc.touch();
}

void FindBar::clear(grove::Node* editor) {
    if (!editor) return;
    editor->set_attribute("find", "");
    m_doc.touch();
}

void FindBar::command(const std::string& name) {
    if (!m_editor) return;
    m_editor->set_attribute("find-command", name + "#" + std::to_string(++m_serial));
    m_doc.touch();
}

void FindBar::set_query(const std::string& text) {
    m_query = text;
    set_field("find-query", text);
    apply(m_editor);
}

void FindBar::toggle(bool& flag, const std::string& id) {
    flag = !flag;
    if (grove::Node* button = m_doc.find_by_id(id)) {
        if (flag) grove::add_class(m_doc, *button, "on");
        else grove::remove_class(m_doc, *button, "on");
    }
    apply(m_editor);
}

void FindBar::show_replace(bool on) {
    m_replace = on;
    if (grove::Node* row = m_doc.find_by_id("replace-row")) {
        if (on) grove::remove_class(m_doc, *row, "hidden");
        else grove::add_class(m_doc, *row, "hidden");
    }
    if (grove::Node* arrow = m_doc.find_by_id("find-toggle-replace")) {
        arrow->set_inner_text(on ? "\xE2\x96\xBE" : "\xE2\x96\xB8");
        arrow->set_attribute("title", on ? "Hide replace" : "Show replace (Ctrl+H)");
    }
    m_doc.touch();
}

void FindBar::set_status(const std::string& text, bool bad) {
    grove::Node* status = m_doc.find_by_id("find-status");
    if (!status) return;
    const auto& kids = status->children();
    if (!(kids.size() == 1 && !kids.front()->is_element() && kids.front()->text() == text)) status->set_inner_text(text.empty() ? std::string(" ") : text);
    if (bad) grove::add_class(m_doc, *status, "bad");
    else grove::remove_class(m_doc, *status, "bad");
    m_doc.touch();
}

void FindBar::focus_query() {
    if (grove::Node* box = m_doc.find_by_id("find-query"); box && m_doc.host().focus) m_doc.host().focus(box);
}

std::string FindBar::field(const std::string& id) const {
    const grove::Node* box = m_doc.find_by_id(id);
    return box ? grove::input_string(*box).value_or("") : std::string();
}

void FindBar::set_field(const std::string& id, const std::string& text) {
    if (grove::Node* box = m_doc.find_by_id(id)) grove::set_input_string(m_doc, *box, text);
}

} // namespace canopy
