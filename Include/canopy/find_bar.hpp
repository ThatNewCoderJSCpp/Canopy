#ifndef CANOPY_FIND_BAR_HPP
#define CANOPY_FIND_BAR_HPP

#include <grove/markup/events.hpp>
#include <grove/markup/node.hpp>

#include <cstddef>
#include <functional>
#include <string>

namespace canopy {

class FindBar {
public:
    struct Hooks {
        std::function<grove::Node*()> editor;
        std::function<void()> focus_editor;
    };

    FindBar(grove::Document& doc, Hooks hooks);

    void install();
    void open(bool replace);
    void close();
    bool is_open() const noexcept { return m_open; }
    void next();
    void previous();
    void select_all();
    void replace_one();
    void replace_all();
    void editor_changed();
    void forget(const grove::Node* editor);

private:
    void listen(const std::string& id, const char* type, grove::Listener fn);
    void apply(grove::Node* editor);
    void clear(grove::Node* editor);
    void command(const std::string& name);
    void set_query(const std::string& text);
    void toggle(bool& flag, const std::string& id);
    void show_replace(bool on);
    void set_status(const std::string& text, bool bad);
    void focus_query();
    std::string field(const std::string& id) const;
    void set_field(const std::string& id, const std::string& text);

    grove::Document& m_doc;
    Hooks m_hooks;
    bool m_open = false;
    bool m_replace = false;
    bool m_case = false;
    bool m_word = false;
    bool m_regex = false;
    std::string m_query;
    std::string m_replacement;
    grove::Node* m_editor = nullptr;
    std::size_t m_serial = 0;
    std::size_t m_current = 0;
    std::size_t m_count = 0;
    bool m_replaced = false;
};

} // namespace canopy

#endif // CANOPY_FIND_BAR_HPP
