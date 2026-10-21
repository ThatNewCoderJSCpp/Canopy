#include "canopy/elements_panel.hpp"

#include <grove/dom_tools.hpp>
#include <grove/input/input.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace canopy {

namespace {

constexpr std::size_t k_max_rows = 4000;

const std::set<std::string>& void_tags() {
    static const std::set<std::string> tags = { "area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "param", "source", "track", "wbr" };
    return tags;
}

bool raw_text_tag(const std::string& tag) {
    return tag == "script" || tag == "style";
}

std::vector<std::string> split(const std::string& text, char sep) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (true) {
        const std::size_t at = text.find(sep, start);
        if (at == std::string::npos) {
            out.push_back(text.substr(start));
            return out;
        }
        out.push_back(text.substr(start, at - start));
        start = at + 1;
    }
}

std::string escape_text(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else out += c;
    }
    return out;
}

std::string escape_attribute(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        if (c == '&') out += "&amp;";
        else if (c == '"') out += "&quot;";
        else out += c;
    }
    return out;
}

std::string collapse(const std::string& text) {
    std::string out;
    bool space = false;
    for (const char c : text) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f') {
            space = !out.empty();
            continue;
        }
        if (space) out += ' ';
        space = false;
        out += c;
    }
    return out;
}

std::string clip(const std::string& text, std::size_t limit) {
    if (text.size() <= limit) return text;
    std::size_t cut = limit;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
    return text.substr(0, cut) + "\xE2\x80\xA6";
}

grove::Node* span(grove::Document& doc, grove::Node& parent, const std::string& text, const char* cls) {
    grove::Node* el = doc.create_element("span");
    el->set_attribute("class", cls);
    el->set_inner_text(text);
    return doc.append_child(parent, *el);
}

const grove::Node* with_attribute(const grove::Node* node, const char* name) {
    for (const grove::Node* n = node; n; n = n->parent()) {
        if (n->is_element() && n->has_attribute(name)) return n;
    }
    return nullptr;
}

long long id_attribute(const grove::Node* node, const char* name) {
    return node ? std::atoll(node->attribute_or(name, "0").c_str()) : 0;
}

bool inline_text_child(const std::vector<DomTreeNode>& nodes, const DomTreeNode& n) {
    if (n.children.size() != 1) return false;
    const DomTreeNode& kid = nodes[static_cast<std::size_t>(n.children.front())];
    if (kid.kind != 't') return false;
    const std::string shown = collapse(kid.text);
    return shown.size() <= 80 && kid.text.find('\n') == std::string::npos;
}

void serialize(const std::vector<DomTreeNode>& nodes, int index, int indent, bool pretty, std::string& out) {
    const DomTreeNode& n = nodes[static_cast<std::size_t>(index)];
    const std::string pad = pretty ? std::string(static_cast<std::size_t>(indent) * 2, ' ') : std::string();
    if (n.kind == 't') {
        const DomTreeNode* parent = n.parent >= 0 ? &nodes[static_cast<std::size_t>(n.parent)] : nullptr;
        out += pad + (parent && raw_text_tag(parent->name) ? n.text : escape_text(n.text));
        return;
    }
    if (n.kind == 'c') {
        out += pad + "<!--" + n.text + "-->";
        return;
    }
    if (n.kind == 'd') {
        out += pad + "<!DOCTYPE " + n.text + ">";
        return;
    }
    out += pad + "<" + n.name;
    for (const auto& [name, value] : n.attributes) {
        out += " " + name;
        if (!value.empty()) out += "=\"" + escape_attribute(value) + "\"";
    }
    out += ">";
    if (void_tags().count(n.name) && n.children.empty()) return;
    bool mixed = false;
    for (const int k : n.children) {
        if (nodes[static_cast<std::size_t>(k)].kind == 't') mixed = true;
    }
    if (!n.children.empty() && (!pretty || mixed)) {
        for (const int k : n.children) serialize(nodes, k, 0, false, out);
    } else if (!n.children.empty()) {
        for (const int k : n.children) {
            out += "\n";
            serialize(nodes, k, indent + 1, true, out);
        }
        out += "\n" + pad;
    }
    out += "</" + n.name + ">";
}

} // namespace

std::vector<DomTreeNode> parse_dom_tree(const std::string& tree) {
    std::vector<DomTreeNode> out;
    if (tree.empty()) return out;
    std::vector<int> stack;
    for (const std::string& record : split(tree, '\x1e')) {
        const std::vector<std::string> f = split(record, '\x1f');
        if (f.size() < 4 || f[0].size() != 1) continue;
        DomTreeNode n;
        n.kind = f[0][0];
        n.id = std::atoll(f[1].c_str());
        n.depth = std::max(0, std::atoi(f[2].c_str()));
        if (n.kind == 'e') {
            n.name = f[3];
            if (f.size() >= 5 && !f[4].empty()) {
                for (const std::string& pair : split(f[4], '\x1c')) {
                    const std::size_t at = pair.find('\x1d');
                    if (at == std::string::npos) n.attributes.emplace_back(pair, std::string());
                    else n.attributes.emplace_back(pair.substr(0, at), pair.substr(at + 1));
                }
            }
        } else if (n.kind == 't' || n.kind == 'c' || n.kind == 'd') {
            n.text = f[3];
        } else {
            continue;
        }
        if (static_cast<std::size_t>(n.depth) > stack.size()) n.depth = static_cast<int>(stack.size());
        stack.resize(static_cast<std::size_t>(n.depth));
        n.parent = stack.empty() ? -1 : stack.back();
        const int index = static_cast<int>(out.size());
        if (n.parent >= 0) out[static_cast<std::size_t>(n.parent)].children.push_back(index);
        const bool element = n.kind == 'e';
        out.push_back(std::move(n));
        if (element) stack.push_back(index);
        else stack.push_back(-1);
    }
    for (DomTreeNode& n : out) {
        n.children.erase(std::remove(n.children.begin(), n.children.end(), -1), n.children.end());
    }
    return out;
}

std::string dom_outer_html(const std::vector<DomTreeNode>& nodes, int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= nodes.size()) return std::string();
    std::string out;
    serialize(nodes, index, 0, true, out);
    return out;
}

std::string dom_node_label(const DomTreeNode& node) {
    if (node.kind == 't') return "#text";
    if (node.kind == 'c') return "#comment";
    if (node.kind == 'd') return "<!DOCTYPE " + node.text + ">";
    std::string out = node.name;
    for (const auto& [name, value] : node.attributes) {
        if (name == "id" && !value.empty()) out += "#" + value;
    }
    for (const auto& [name, value] : node.attributes) {
        if (name != "class") continue;
        std::string word;
        for (const char c : value + " ") {
            if (c == ' ' || c == '\t' || c == '\n') {
                if (!word.empty()) out += "." + word;
                word.clear();
            } else {
                word += c;
            }
        }
    }
    return out;
}

std::string js_string_literal(const std::string& text) {
    std::string out = "\"";
    for (std::size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "\\u%04x", c);
            out += buf;
        } else if (c == 0xE2 && i + 2 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0x80 && (static_cast<unsigned char>(text[i + 2]) == 0xA8 || static_cast<unsigned char>(text[i + 2]) == 0xA9)) {
            out += static_cast<unsigned char>(text[i + 2]) == 0xA8 ? "\\u2028" : "\\u2029";
            i += 2;
        } else {
            out += static_cast<char>(c);
        }
    }
    return out + "\"";
}

ElementsPanel::ElementsPanel(grove::Document& doc, Hooks hooks) : m_doc(doc), m_hooks(std::move(hooks)) {}

void ElementsPanel::listen(const std::string& id, const char* type, grove::Listener fn) {
    if (grove::Node* n = m_doc.find_by_id(id)) m_doc.events().add_listener(n, type, std::move(fn));
}

void ElementsPanel::install() {
    listen("elements-page", "change", [this](grove::Event& e) {
        if (e.target) choose_page(grove::input_string(*e.target).value_or(""));
    });
    listen("elements-refresh", "click", [this](grove::Event&) { command("__canopyDom.refresh()"); });
    listen("elements-reload", "click", [this](grove::Event&) { reload_page(); });
    listen("elements-apply", "click", [this](grove::Event&) { apply_edit(); });
    listen("elements-revert", "click", [this](grove::Event&) { refresh_side(true); });
    listen("elements-delete", "click", [this](grove::Event&) { delete_node(); });
    listen("elements-serve", "click", [this](grove::Event& e) {
        if (m_hooks.serve_workspace) m_hooks.serve_workspace();
        e.default_prevented = true;
    });
    listen("elements-html", "key_down", [this](grove::Event& e) {
        const unsigned mods = e.modifiers & (grove::ModifierControl | grove::ModifierMeta);
        if (e.key == "Enter" && mods != 0) {
            e.default_prevented = true;
            apply_edit();
        }
    });
    listen("elements-tree", "click", [this](grove::Event& e) {
        if (const grove::Node* t = with_attribute(e.target, "data-el-toggle")) {
            toggle(id_attribute(t, "data-el-toggle"));
            e.default_prevented = true;
            return;
        }
        if (const grove::Node* row = with_attribute(e.target, "data-el")) {
            select(id_attribute(row, "data-el"), true);
            e.default_prevented = true;
        }
    });
    listen("elements-tree", "double_click", [this](grove::Event& e) {
        if (const grove::Node* row = with_attribute(e.target, "data-el")) toggle(id_attribute(row, "data-el"));
    });
    listen("elements-tree", "mouse_move", [this](grove::Event& e) {
        const grove::Node* row = with_attribute(e.target, "data-el");
        hover(row ? id_attribute(row, "data-el") : 0);
    });
    listen("elements-tree", "mouse_leave", [this](grove::Event&) { hover(0); });
    if (grove::Node* editor = m_doc.find_by_id("elements-html"); editor && m_hooks.style_editor) m_hooks.style_editor(*editor);
    refresh_empty();
}

LiveServer* ElementsPanel::server() const {
    if (!m_server || !m_hooks.servers) return nullptr;
    for (LiveServer* s : m_hooks.servers()) {
        if (s == m_server) return s;
    }
    return nullptr;
}

void ElementsPanel::servers_changed() {
    if (m_server && !server()) {
        m_server = nullptr;
        m_page.clear();
        m_serial = 0;
        m_nodes.clear();
        m_roots.clear();
        render();
    }
    m_pages_key = "-";
    refresh_pages();
}

void ElementsPanel::shown() {
    if (grove::Node* editor = m_doc.find_by_id("elements-html"); editor && m_hooks.style_editor) m_hooks.style_editor(*editor);
    refresh_pages();
}

void ElementsPanel::poll() {
    const bool visible = m_hooks.visible && m_hooks.visible();
    if (!visible) {
        if (m_was_visible) {
            if (LiveServer* s = server()) s->stop_watching_dom();
            m_hovered = 0;
        }
        m_was_visible = false;
        return;
    }
    if (!m_was_visible) {
        m_was_visible = true;
        m_serial = 0;
        shown();
    }
    if (++m_tick % 3 == 0) refresh_pages();
    LiveServer* s = server();
    if (!s || m_page.empty()) return;
    s->watch_dom(m_page);
    PageDom dom;
    if (s->page_dom(m_page, m_serial, dom)) {
        m_serial = dom.serial;
        set_tree(dom.tree);
    }
}

void ElementsPanel::refresh_pages() {
    std::vector<LivePage> pages;
    std::vector<LiveServer*> owners;
    if (m_hooks.servers) {
        for (LiveServer* s : m_hooks.servers()) {
            if (!s || !s->running() || !s->console()) continue;
            for (LivePage& p : s->live_pages()) {
                pages.push_back(std::move(p));
                owners.push_back(s);
            }
        }
    }
    std::string key;
    for (std::size_t i = 0; i < pages.size(); ++i) key += pages[i].id + "|" + pages[i].path + "|" + std::to_string(reinterpret_cast<std::uintptr_t>(owners[i])) + "\n";
    m_pages = pages;
    m_page_servers = owners;
    std::string pick = m_page;
    bool present = false;
    for (const LivePage& p : pages) present = present || p.id == m_page;
    if (!present) {
        pick.clear();
        for (const LivePage& p : pages) {
            if (p.path == m_page_path) pick = p.id;
        }
        if (pick.empty() && !pages.empty()) pick = pages.back().id;
    }
    if (key != m_pages_key) {
        m_pages_key = key;
        if (grove::Node* picker = m_doc.find_by_id("elements-page")) {
            std::vector<grove::SelectOption> options;
            for (std::size_t i = 0; i < pages.size(); ++i) {
                std::size_t same = 0, nth = 0;
                for (std::size_t j = 0; j < pages.size(); ++j) {
                    if (pages[j].path != pages[i].path) continue;
                    ++same;
                    if (j <= i) ++nth;
                }
                std::string label = pages[i].path.empty() ? std::string("/") : pages[i].path;
                if (same > 1) label += "  (tab " + std::to_string(nth) + ")";
                label += "  \xC2\xB7  port " + std::to_string(owners[i]->port());
                options.push_back(grove::SelectOption{ pages[i].id, label, false });
            }
            if (options.empty()) options.push_back(grove::SelectOption{ "", "No open page", false });
            grove::set_select_options(m_doc, *picker, options);
            picker->set_attribute("value", pick);
        }
    }
    if (pick != m_page) choose_page(pick);
    refresh_empty();
}

void ElementsPanel::choose_page(const std::string& id) {
    LiveServer* owner = nullptr;
    std::string path;
    for (std::size_t i = 0; i < m_pages.size(); ++i) {
        if (m_pages[i].id == id) {
            owner = m_page_servers[i];
            path = m_pages[i].path;
        }
    }
    if (LiveServer* old = server(); old && (old != owner || id != m_page)) old->stop_watching_dom();
    const bool same_path = !id.empty() && path == m_page_path && owner == m_server;
    m_page = owner ? id : std::string();
    m_server = owner;
    m_serial = 0;
    m_hovered = 0;
    if (!same_path) {
        m_page_path = path;
        m_expanded.clear();
        m_fresh = true;
        m_selected = 0;
        m_selected_path.clear();
        m_nodes.clear();
        m_roots.clear();
        render();
        refresh_side(true);
    }
    if (grove::Node* picker = m_doc.find_by_id("elements-page")) picker->set_attribute("value", m_page);
    if (m_server && !m_page.empty()) m_server->watch_dom(m_page);
    refresh_empty();
    m_doc.touch();
}

void ElementsPanel::set_tree(const std::string& tree) {
    m_nodes = parse_dom_tree(tree);
    m_roots.clear();
    for (std::size_t i = 0; i < m_nodes.size(); ++i) {
        if (m_nodes[i].parent < 0) m_roots.push_back(static_cast<int>(i));
    }
    if (m_fresh) {
        m_fresh = false;
        for (const DomTreeNode& n : m_nodes) {
            if (n.kind == 'e' && (n.name == "html" || n.name == "body") && n.depth <= 1) m_expanded.insert(n.id);
        }
    }
    if (m_selected != 0 && index_of(m_selected) < 0) {
        const int at = index_at(m_selected_path);
        m_selected = at >= 0 ? m_nodes[static_cast<std::size_t>(at)].id : 0;
        if (at >= 0) expand_to(at);
    }
    if (m_selected != 0) m_selected_path = path_of(index_of(m_selected));
    render();
    refresh_side(false);
    refresh_empty();
}

int ElementsPanel::index_of(long long id) const {
    for (std::size_t i = 0; i < m_nodes.size(); ++i) {
        if (m_nodes[i].id == id) return static_cast<int>(i);
    }
    return -1;
}

std::vector<std::size_t> ElementsPanel::path_of(int index) const {
    std::vector<std::size_t> path;
    while (index >= 0) {
        const DomTreeNode& n = m_nodes[static_cast<std::size_t>(index)];
        const std::vector<int>& siblings = n.parent >= 0 ? m_nodes[static_cast<std::size_t>(n.parent)].children : m_roots;
        const auto it = std::find(siblings.begin(), siblings.end(), index);
        path.push_back(static_cast<std::size_t>(it - siblings.begin()));
        index = n.parent;
    }
    std::reverse(path.begin(), path.end());
    return path;
}

int ElementsPanel::index_at(const std::vector<std::size_t>& path) const {
    if (path.empty()) return -1;
    const std::vector<int>* level = &m_roots;
    int found = -1;
    for (const std::size_t step : path) {
        if (level->empty()) break;
        found = (*level)[std::min(step, level->size() - 1)];
        if (step >= level->size()) break;
        level = &m_nodes[static_cast<std::size_t>(found)].children;
    }
    return found;
}

void ElementsPanel::expand_to(int index) {
    for (int p = index >= 0 ? m_nodes[static_cast<std::size_t>(index)].parent : -1; p >= 0; p = m_nodes[static_cast<std::size_t>(p)].parent) m_expanded.insert(m_nodes[static_cast<std::size_t>(p)].id);
}

void ElementsPanel::render() {
    grove::Node* tree = m_doc.find_by_id("elements-tree");
    if (!tree) return;
    while (!tree->children().empty()) m_doc.remove(*tree->children().back());
    std::size_t rows = 0;
    for (const int r : m_roots) render_node(*tree, r, rows);
    if (rows >= k_max_rows) span(m_doc, *tree, "\xE2\x80\xA6 the page has more; collapse some elements to see the rest", "el-more");
    m_doc.touch();
}

void ElementsPanel::render_node(grove::Node& host, int index, std::size_t& rows) {
    if (rows >= k_max_rows) return;
    const DomTreeNode& n = m_nodes[static_cast<std::size_t>(index)];
    const std::string id = std::to_string(n.id);
    const bool selected = n.id == m_selected;
    const std::string pad = std::to_string(4 + n.depth * 14) + "px";
    auto make_row = [&]() {
        grove::Node* row = m_doc.create_element("p");
        row->set_attribute("class", selected ? "el-row selected" : "el-row");
        row->set_attribute("data-el", id);
        grove::set_inline_style(m_doc, *row, "padding", "1px 8px 1px " + pad);
        ++rows;
        return m_doc.append_child(host, *row);
    };
    grove::Node* row = make_row();
    if (n.kind == 't') {
        span(m_doc, *row, "  ", "el-twisty");
        const std::string shown = clip(collapse(n.text), 300);
        span(m_doc, *row, "\"" + shown + "\"", "el-text");
        return;
    }
    if (n.kind == 'c') {
        span(m_doc, *row, "  ", "el-twisty");
        span(m_doc, *row, "<!-- " + clip(collapse(n.text), 300) + " -->", "el-comment");
        return;
    }
    if (n.kind == 'd') {
        span(m_doc, *row, "  ", "el-twisty");
        span(m_doc, *row, "<!DOCTYPE " + n.text + ">", "el-comment");
        return;
    }
    const bool is_void = void_tags().count(n.name) && n.children.empty();
    const bool inline_text = inline_text_child(m_nodes, n);
    const bool has_kids = !n.children.empty() && !inline_text;
    const bool expanded = has_kids && m_expanded.count(n.id);
    grove::Node* twisty = span(m_doc, *row, has_kids ? (expanded ? "\xE2\x96\xBE " : "\xE2\x96\xB8 ") : "  ", "el-twisty");
    if (has_kids) twisty->set_attribute("data-el-toggle", id);
    span(m_doc, *row, "<", "el-punct");
    span(m_doc, *row, n.name, "el-tag");
    for (const auto& [name, value] : n.attributes) {
        span(m_doc, *row, " " + name, "el-attr");
        if (value.empty()) continue;
        span(m_doc, *row, "=\"", "el-punct");
        span(m_doc, *row, clip(value, 160), "el-value");
        span(m_doc, *row, "\"", "el-punct");
    }
    span(m_doc, *row, ">", "el-punct");
    if (is_void) return;
    if (inline_text) {
        span(m_doc, *row, m_nodes[static_cast<std::size_t>(n.children.front())].text, "el-text-inline");
    } else if (has_kids && !expanded) {
        span(m_doc, *row, "\xE2\x80\xA6", "el-ellipsis");
    } else if (expanded) {
        for (const int k : n.children) render_node(host, k, rows);
        row = make_row();
        span(m_doc, *row, "  ", "el-twisty");
    }
    span(m_doc, *row, "</", "el-punct");
    span(m_doc, *row, n.name, "el-tag");
    span(m_doc, *row, ">", "el-punct");
}

void ElementsPanel::toggle(long long id) {
    if (m_expanded.count(id)) m_expanded.erase(id);
    else m_expanded.insert(id);
    render();
}

void ElementsPanel::select(long long id, bool reveal) {
    const int index = index_of(id);
    if (index < 0) return;
    m_selected = id;
    m_selected_path = path_of(index);
    expand_to(index);
    render();
    refresh_side(true);
    command("__canopyDom.mark(" + std::to_string(id) + (reveal ? ", true)" : ")"));
}

void ElementsPanel::hover(long long id) {
    if (id == m_hovered) return;
    m_hovered = id;
    command("__canopyDom.mark(" + std::to_string(id) + ")");
}

void ElementsPanel::refresh_side(bool force) {
    grove::Node* side = m_doc.find_by_id("elements-side");
    grove::Node* editor = m_doc.find_by_id("elements-html");
    if (!side || !editor) return;
    const int index = m_selected != 0 ? index_of(m_selected) : -1;
    if (index < 0) {
        grove::add_class(m_doc, *side, "hidden");
        m_side_text.clear();
        m_doc.touch();
        return;
    }
    grove::remove_class(m_doc, *side, "hidden");
    const DomTreeNode& n = m_nodes[static_cast<std::size_t>(index)];
    const std::string text = n.kind == 'e' ? dom_outer_html(m_nodes, index) : n.kind == 'd' ? "<!DOCTYPE " + n.text + ">" : n.text;
    const std::string current = grove::input_string(*editor).value_or("");
    const bool untouched = current == m_side_text;
    const std::string language = n.kind == 'e' ? "html" : "plain";
    if (editor->attribute_or("language", "") != language) {
        editor->set_attribute("language", language);
        if (m_hooks.style_editor) m_hooks.style_editor(*editor);
    }
    if (n.kind == 'd') editor->set_attribute("readonly", "");
    else editor->remove_attribute("readonly");
    if (force || untouched) {
        if (current != text) grove::set_input_string(m_doc, *editor, text);
        m_side_text = text;
    }
    const std::string label = dom_node_label(n);
    if (grove::Node* title = m_doc.find_by_id("elements-selected")) {
        const std::string shown = n.kind == 'e' ? "Edit " + label + " as HTML" : n.kind == 't' ? "Edit text" : n.kind == 'c' ? "Edit comment" : label;
        const auto& kids = title->children();
        if (!(kids.size() == 1 && !kids.front()->is_element() && kids.front()->text() == shown)) title->set_inner_text(shown);
    }
    if (grove::Node* del = m_doc.find_by_id("elements-delete")) {
        if (n.kind == 'e' && n.name == "html") grove::add_class(m_doc, *del, "hidden");
        else grove::remove_class(m_doc, *del, "hidden");
    }
    m_doc.touch();
}

void ElementsPanel::apply_edit() {
    const int index = m_selected != 0 ? index_of(m_selected) : -1;
    grove::Node* editor = m_doc.find_by_id("elements-html");
    if (index < 0 || !editor) return;
    const DomTreeNode& n = m_nodes[static_cast<std::size_t>(index)];
    if (n.kind == 'd') return;
    const std::string text = grove::input_string(*editor).value_or("");
    m_side_text = text;
    if (n.kind == 'e') command("__canopyDom.html(" + std::to_string(n.id) + ", " + js_string_literal(text) + ")");
    else command("__canopyDom.text(" + std::to_string(n.id) + ", " + js_string_literal(text) + ")");
}

void ElementsPanel::delete_node() {
    const int index = m_selected != 0 ? index_of(m_selected) : -1;
    if (index < 0) return;
    command("__canopyDom.remove(" + std::to_string(m_selected) + ")");
}

void ElementsPanel::reload_page() {
    command("location.reload()");
}

void ElementsPanel::command(const std::string& code) {
    LiveServer* s = server();
    if (!s || m_page.empty()) return;
    s->page_command(m_page, code);
}

void ElementsPanel::refresh_empty() {
    grove::Node* empty = m_doc.find_by_id("elements-empty");
    grove::Node* body = m_doc.find_by_id("elements-body");
    if (!empty || !body) return;
    bool serving = false;
    bool console_off = false;
    if (m_hooks.servers) {
        for (LiveServer* s : m_hooks.servers()) {
            if (!s || !s->running()) continue;
            serving = true;
            console_off = console_off || !s->console();
        }
    }
    const bool ready = server() && !m_page.empty() && !m_nodes.empty();
    if (ready) {
        grove::add_class(m_doc, *empty, "hidden");
        grove::remove_class(m_doc, *body, "hidden");
        return;
    }
    grove::remove_class(m_doc, *empty, "hidden");
    grove::add_class(m_doc, *body, "hidden");
    std::string text;
    if (server() && !m_page.empty()) text = "Reading the page\xE2\x80\xA6";
    else if (serving && console_off && m_pages.empty()) text = "Turn on Page console (Settings, under Local server) to inspect pages here.";
    else if (serving) text = "Open a served page in your browser to see its elements here. You can change them in the page (it doesn't touch your files); reloading the page drops your changes.";
    else text = "Elements shows the HTML of a page Canopy's local server sent, live, and lets you try changes in the page without touching your files. Serve a folder (or press Run on an .html file) to start.";
    if (grove::Node* hint = m_doc.find_by_id("elements-hint")) {
        const auto& kids = hint->children();
        if (!(kids.size() == 1 && !kids.front()->is_element() && kids.front()->text() == text)) hint->set_inner_text(text);
    }
    if (grove::Node* serve = m_doc.find_by_id("elements-serve")) serving ? (void)grove::add_class(m_doc, *serve, "hidden") : (void)grove::remove_class(m_doc, *serve, "hidden");
    m_doc.touch();
}

} // namespace canopy
