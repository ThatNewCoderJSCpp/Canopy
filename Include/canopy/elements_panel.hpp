#ifndef CANOPY_ELEMENTS_PANEL_HPP
#define CANOPY_ELEMENTS_PANEL_HPP

#include "live_server.hpp"

#include <grove/markup/events.hpp>
#include <grove/markup/node.hpp>

#include <cstddef>
#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace canopy {

struct DomTreeNode {
    char kind = 'e';
    long long id = 0;
    int depth = 0;
    std::string name;
    std::vector<std::pair<std::string, std::string>> attributes;
    std::string text;
    int parent = -1;
    std::vector<int> children;
};

std::vector<DomTreeNode> parse_dom_tree(const std::string& tree);
std::string dom_outer_html(const std::vector<DomTreeNode>& nodes, int index);
std::string dom_node_label(const DomTreeNode& node);
std::string js_string_literal(const std::string& text);

class ElementsPanel {
public:
    struct Hooks {
        std::function<std::vector<LiveServer*>()> servers;
        std::function<bool()> visible;
        std::function<void()> serve_workspace;
        std::function<void(grove::Node&)> style_editor;
    };

    ElementsPanel(grove::Document& doc, Hooks hooks);

    void install();
    void poll();
    void servers_changed();
    void shown();

private:
    void listen(const std::string& id, const char* type, grove::Listener fn);
    void refresh_pages();
    void choose_page(const std::string& id);
    void set_tree(const std::string& tree);
    void render();
    void render_node(grove::Node& host, int index, std::size_t& rows);
    void select(long long id, bool reveal);
    void refresh_side(bool force);
    void apply_edit();
    void delete_node();
    void reload_page();
    void command(const std::string& code);
    void hover(long long id);
    void toggle(long long id);
    void expand_to(int index);
    void refresh_empty();
    int index_of(long long id) const;
    std::vector<std::size_t> path_of(int index) const;
    int index_at(const std::vector<std::size_t>& path) const;
    LiveServer* server() const;

    grove::Document& m_doc;
    Hooks m_hooks;
    std::vector<LivePage> m_pages;
    std::vector<LiveServer*> m_page_servers;
    std::string m_page;
    std::string m_page_path;
    LiveServer* m_server = nullptr;
    unsigned long long m_serial = 0;
    std::vector<DomTreeNode> m_nodes;
    std::vector<int> m_roots;
    std::set<long long> m_expanded;
    bool m_fresh = true;
    long long m_selected = 0;
    std::vector<std::size_t> m_selected_path;
    long long m_hovered = 0;
    std::string m_side_text;
    std::string m_side_label;
    std::string m_pages_key;
    int m_tick = 0;
    bool m_was_visible = false;
};

} // namespace canopy

#endif // CANOPY_ELEMENTS_PANEL_HPP
