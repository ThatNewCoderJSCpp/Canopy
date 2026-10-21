#include <canopy/web_intel.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>

namespace fs = std::filesystem;

namespace canopy {

namespace {

bool ident_char(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    return std::isalnum(u) || c == '_' || c == '$' || u >= 0x80;
}

bool css_char(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    return std::isalnum(u) || c == '_' || c == '-' || u >= 0x80;
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

bool starts_with(const std::string& s, const std::string& head) { return s.rfind(head, 0) == 0; }

bool ends_with(const std::string& s, const std::string& tail) { return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0; }

std::size_t offset_of(const std::string& text, std::size_t line, std::size_t column) {
    std::size_t at = 0;
    for (std::size_t l = 0; l < line && at < text.size(); ++l) {
        const std::size_t nl = text.find('\n', at);
        if (nl == std::string::npos) return text.size();
        at = nl + 1;
    }
    return std::min(text.size(), at + column);
}

std::string code_block(const std::string& language, const std::string& code) { return "```" + language + "\n" + code + "\n```\n"; }

std::string inline_code(const std::string& text) { return "`" + text + "`"; }

std::string module_owner(std::string name) {
    if (starts_with(name, "node:")) name = name.substr(5);
    if (name == "express") return "express-static";
    if (name == "fs/promises") return "fs";
    if (name == "https") return "http";
    static const std::set<std::string> owners = { "fs", "path", "http", "os", "child_process", "crypto", "util", "events", "process" };
    if (owners.count(name)) return name;
    return std::string();
}

std::size_t match_open(const std::string& t, std::size_t close) {
    int depth = 0;
    for (std::size_t i = close + 1; i-- > 0;) {
        const char c = t[i];
        if (c == '"' || c == '\'' || c == '`') {
            while (i > 0 && !(t[i - 1] == c && (i < 2 || t[i - 2] != '\\'))) --i;
            if (i > 0) --i;
            continue;
        }
        if (c == ')' || c == ']' || c == '}') ++depth;
        else if (c == '(' || c == '[' || c == '{') {
            if (--depth == 0) return i;
        }
    }
    return std::string::npos;
}

std::string first_string(const std::string& s) {
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char q = s[i];
        if (q != '"' && q != '\'' && q != '`') continue;
        const std::size_t e = s.find(q, i + 1);
        if (e == std::string::npos) return std::string();
        return s.substr(i + 1, e - i - 1);
    }
    return std::string();
}

bool has_word(const std::string& text, const std::string& word) {
    std::size_t at = 0;
    while ((at = text.find(word, at)) != std::string::npos) {
        const bool left = at == 0 || !ident_char(text[at - 1]);
        const bool right = at + word.size() >= text.size() || !ident_char(text[at + word.size()]);
        if (left && right) return true;
        at += word.size();
    }
    return false;
}

std::string owner_before(const std::string& t, std::size_t end, int depth);

std::string rhs_owner(const std::string& t, const std::string& rhs, std::size_t rhs_at, int depth) {
    std::size_t r_at = rhs.find_first_not_of(" \t");
    if (r_at == std::string::npos) return std::string();
    if (rhs.compare(r_at, 6, "await ") == 0) {
        r_at = rhs.find_first_not_of(" \t", r_at + 6);
        if (r_at == std::string::npos) return std::string();
    }
    const std::string r = trim(rhs.substr(r_at));
    if (starts_with(r, "require(")) return module_owner(first_string(r));
    if (starts_with(r, "express()")) return "app";
    if (r.find("Router(") != std::string::npos && r.find('(') == r.find("Router(") + 6) return "app";
    if (starts_with(r, "$(") || starts_with(r, "jQuery(")) return "jquery";
    if (starts_with(r, "fetch(")) return rhs.find("await") != std::string::npos ? "response" : "promise";
    if (starts_with(r, "new Promise")) return "promise";
    if (starts_with(r, "new EventEmitter")) return "events-emitter";
    if (starts_with(r, "http.createServer(") || starts_with(r, "https.createServer(")) return "server";
    if (!r.empty() && (r[0] == '"' || r[0] == '\'' || r[0] == '`')) return "string";
    if (!r.empty() && r[0] == '[') return "array";
    static const char* const finders[] = { "getElementById(", "querySelector(", "createElement(", "closest(" };
    for (const char* f : finders) {
        const std::size_t at = r.find(f);
        if (at != std::string::npos && r.find('(') == at + std::string(f).size() - 1) return "element";
    }
    if (depth < 8) {
        const std::size_t semi = r.find_first_of(";\n");
        std::string expr = semi == std::string::npos ? r : r.substr(0, semi);
        while (!expr.empty() && (expr.back() == ',' || expr.back() == ' ')) expr.pop_back();
        if (!expr.empty() && (expr.back() == ')' || ident_char(expr.back())) && expr.find('.') != std::string::npos) {
            const std::size_t end_at = rhs_at + r_at + expr.size();
            if (end_at <= t.size()) return owner_before(t, end_at, depth + 1);
        }
    }
    return std::string();
}

std::string variable_owner(const std::string& t, const std::string& name, std::size_t before, int depth) {
    static const std::set<std::string> same = { "console", "document", "window", "Math", "JSON", "Object", "Array", "Number", "Promise", "Date", "localStorage", "location", "history", "navigator", "process" };
    if (same.count(name)) return name;
    if (name == "sessionStorage") return "localStorage";
    if (name == "$" || name == "jQuery") return "jquery-static";
    std::size_t at = 0;
    std::string found;
    while ((at = t.find(name, at)) != std::string::npos && at < before) {
        const std::size_t here = at;
        at += name.size();
        if (here > 0 && ident_char(t[here - 1])) continue;
        if (at < t.size() && ident_char(t[at])) continue;
        std::size_t k = at;
        while (k < t.size() && (t[k] == ' ' || t[k] == '\t')) ++k;
        if (k < t.size() && t[k] == '=' && (k + 1 >= t.size() || (t[k + 1] != '=' && t[k + 1] != '>'))) {
            const std::size_t nl = t.find('\n', k);
            const std::string rhs = t.substr(k + 1, (nl == std::string::npos ? t.size() : nl) - k - 1);
            const std::string owner = rhs_owner(t, rhs, k + 1, depth);
            if (!owner.empty()) found = owner;
            continue;
        }
        std::size_t s = here;
        while (s > 0 && (t[s - 1] == ' ' || t[s - 1] == '\t')) --s;
        const std::size_t ls = t.rfind('\n', s == 0 ? 0 : s - 1);
        const std::string head = trim(t.substr(ls == std::string::npos ? 0 : ls + 1, here - (ls == std::string::npos ? 0 : ls + 1)));
        if (starts_with(head, "import") && (ends_with(head, "import") || ends_with(head, "* as"))) {
            const std::size_t nl = t.find('\n', at);
            const std::string tail = t.substr(at, (nl == std::string::npos ? t.size() : nl) - at);
            if (tail.find("from") != std::string::npos) {
                const std::string owner = module_owner(first_string(tail));
                if (!owner.empty()) found = owner;
            }
        }
    }
    if (!found.empty()) return found;
    const std::string low = lower(name);
    if (low == "req" || low == "request") return "req";
    if (low == "res" || low == "response") return has_word(t, "express") || has_word(t, "http") ? "res" : "response";
    if (low == "e" || low == "ev" || low == "evt" || low == "event") return "event";
    if (low == "app" || low == "router") return has_word(t, "express") ? "app" : std::string();
    if (low == "server") return "server";
    if (low == "el" || low == "elem" || low == "element" || low == "btn" || low == "button" || low == "node" || ends_with(name, "El") || ends_with(name, "Element") || ends_with(name, "Btn") || ends_with(name, "Button") || ends_with(name, "Input")) return "element";
    return std::string();
}

std::string property_owner(const std::string& obj, const std::string& name) {
    static const std::set<std::string> element_links = { "parentElement", "parentNode", "firstElementChild", "lastElementChild", "nextElementSibling", "previousElementSibling", "offsetParent" };
    if (name == "classList") return "classList";
    if (obj == "element" && element_links.count(name)) return "element";
    if (obj == "document" && (name == "body" || name == "head" || name == "documentElement" || name == "activeElement")) return "element";
    if (obj == "event" && (name == "target" || name == "currentTarget" || name == "relatedTarget")) return "element";
    if (obj == "window") {
        if (name == "document" || name == "location" || name == "history" || name == "navigator" || name == "localStorage" || name == "console") return name;
        if (name == "sessionStorage") return "localStorage";
    }
    if (obj == "document" && name == "location") return "location";
    if (obj == "fs" && name == "promises") return "fs";
    if (obj == "jquery-static" && name == "fn") return "jquery";
    if (name == "length") return std::string();
    return std::string();
}

std::string call_owner(const std::string& obj, const std::string& name, const std::string& args) {
    if (obj.empty()) {
        if (name == "$" || name == "jQuery") return "jquery";
        if (name == "express") return "app";
        if (name == "Router") return "app";
        if (name == "require") return module_owner(first_string(args));
        if (name == "fetch") return "promise";
        return std::string();
    }
    if (obj == "jquery") {
        static const std::set<std::string> values = { "get", "is", "hasClass", "index", "serialize", "serializeArray", "offset", "position", "toArray" };
        static const std::set<std::string> getters = { "val", "text", "html", "attr", "css", "data", "prop", "width", "height", "scrollTop" };
        if (values.count(name)) return std::string();
        if (getters.count(name)) return trim(args).empty() || (args.find(',') == std::string::npos && args.find('{') == std::string::npos) ? std::string() : "jquery";
        return "jquery";
    }
    if ((obj == "document" || obj == "element") && (name == "getElementById" || name == "querySelector" || name == "closest" || name == "createElement")) return "element";
    if (obj == "res" && (name == "status" || name == "set" || name == "type" || name == "cookie" || name == "clearCookie" || name == "append")) return "res";
    if (obj == "express-static" && name == "Router") return "app";
    if (obj == "app" && name == "route") return "app";
    if (obj == "app" && name == "listen") return "server";
    if (obj == "http" && name == "createServer") return "server";
    if ((obj == "promise" || obj == "Promise") && (name == "then" || name == "catch" || name == "finally" || name == "all" || name == "race" || name == "allSettled" || name == "any" || name == "resolve" || name == "reject")) return "promise";
    if (obj == "response" && (name == "json" || name == "text" || name == "blob")) return "promise";
    if (obj == "string") {
        if (name == "split" || name == "match") return "array";
        return "string";
    }
    if (obj == "array") {
        if (name == "join") return "string";
        if (name == "map" || name == "filter" || name == "slice" || name == "concat" || name == "sort" || name == "reverse" || name == "flat") return "array";
    }
    if (obj == "Object" && (name == "keys" || name == "values" || name == "entries")) return "array";
    if (obj == "Array" && (name == "from" || name == "of")) return "array";
    if (obj == "JSON" && name == "stringify") return "string";
    return std::string();
}

std::string owner_before(const std::string& t, std::size_t end, int depth) {
    if (depth > 12 || end == 0 || end > t.size()) return std::string();
    std::size_t i = end;
    while (i > 0 && (t[i - 1] == ' ' || t[i - 1] == '\t' || t[i - 1] == '\n' || t[i - 1] == '\r')) --i;
    if (i == 0) return std::string();
    const char last = t[i - 1];
    if (last == '"' || last == '\'' || last == '`') return "string";
    if (last == ']') {
        const std::size_t open = match_open(t, i - 1);
        if (open == std::string::npos) return std::string();
        std::size_t k = open;
        while (k > 0 && (t[k - 1] == ' ' || t[k - 1] == '\t')) --k;
        if (k == 0 || !(ident_char(t[k - 1]) || t[k - 1] == ')' || t[k - 1] == ']')) return "array";
        return std::string();
    }
    if (last == ')') {
        const std::size_t open = match_open(t, i - 1);
        if (open == std::string::npos) return std::string();
        const std::string args = t.substr(open + 1, i - 2 - open);
        std::size_t p = open;
        while (p > 0 && ident_char(t[p - 1])) --p;
        const std::string name = t.substr(p, open - p);
        if (name.empty()) return std::string();
        if (p > 0 && t[p - 1] == '.') return call_owner(owner_before(t, p - 1, depth + 1), name, args);
        return call_owner(std::string(), name, args);
    }
    std::size_t p = i;
    while (p > 0 && ident_char(t[p - 1])) --p;
    const std::string name = t.substr(p, i - p);
    if (name.empty()) return std::string();
    if (p > 0 && t[p - 1] == '.') return property_owner(owner_before(t, p - 1, depth + 1), name);
    return variable_owner(t, name, p, depth);
}

std::string owner_label(const std::string& owner) {
    if (owner == "jquery") return "jQuery object";
    if (owner == "jquery-static") return "jQuery ($)";
    if (owner == "express-static") return "express";
    if (owner == "app") return "Express app";
    if (owner == "req") return "Express request";
    if (owner == "res") return "Express response";
    if (owner == "server") return "http.Server";
    if (owner == "element") return "Element";
    if (owner == "event") return "Event";
    if (owner == "response") return "fetch Response";
    if (owner == "promise") return "Promise";
    if (owner == "array") return "Array";
    if (owner == "string") return "String";
    return owner;
}

std::string doc_markdown(const WebDoc& d, const std::string& language, const std::string& from) {
    std::string md = code_block(language, d.signature.empty() ? d.name : d.signature);
    md += d.text + "\n";
    if (!from.empty()) md += "---\nOn " + from + "\n";
    return md;
}

std::optional<HoverResult> js_hover(const std::string& text, std::size_t line, std::size_t column, std::size_t offset, bool node) {
    const std::optional<std::string> found = text_line_at(text, line);
    if (!found) return std::nullopt;
    const std::string& l = *found;
    if (const std::optional<std::string> mod = js_module_string_at(l, column)) {
        const std::size_t at = l.rfind(*mod, column);
        if (const WebDoc* d = find_web_doc("node-module", starts_with(*mod, "node:") ? mod->substr(5) : *mod)) {
            HoverResult h{ line, at == std::string::npos ? column : at, mod->size(), code_block("javascript", "require(\"" + *mod + "\")") + d->text + "\n" };
            return h;
        }
        return std::nullopt;
    }
    if (column > l.size()) return std::nullopt;
    std::size_t b = column;
    std::size_t e = column;
    while (b > 0 && ident_char(l[b - 1])) --b;
    while (e < l.size() && ident_char(l[e])) ++e;
    if (b == e) return std::nullopt;
    const std::string word = l.substr(b, e - b);
    const std::size_t word_at = offset - (column - b);
    HoverResult h{ line, b, e - b, std::string() };
    if (b > 0 && l[b - 1] == '.') {
        const std::string owner = js_object_owner(text, word_at - 1);
        if (!owner.empty()) {
            if (const WebDoc* d = find_web_doc(owner, word)) {
                h.markdown = doc_markdown(*d, "javascript", owner_label(owner));
                return h;
            }
            if (owner == "jquery-static") {
                if (const WebDoc* d = find_web_doc("jquery", word)) {
                    h.markdown = doc_markdown(*d, "javascript", owner_label("jquery"));
                    return h;
                }
            }
        }
        for (const char* guess : { "element", "array", "string", "promise", "jquery", "res", "req", "app" }) {
            if (const WebDoc* d = find_web_doc(guess, word)) {
                h.markdown = doc_markdown(*d, "javascript", owner_label(guess) + " (probably)");
                return h;
            }
        }
        return std::nullopt;
    }
    for (const char* owner : { "js-keyword", "js-global", "node-global" }) {
        if (!node && std::string(owner) == "node-global") continue;
        if (const WebDoc* d = find_web_doc(owner, word)) {
            h.markdown = doc_markdown(*d, "javascript", std::string());
            return h;
        }
    }
    const std::string owner = variable_owner(text, word, word_at, 0);
    if (!owner.empty() && owner != word) {
        std::string what = owner_label(owner);
        if (const WebDoc* d = find_web_doc("node-module", owner == "express-static" ? std::string("express") : owner)) h.markdown = code_block("javascript", word) + "The " + inline_code(owner == "express-static" ? std::string("express") : owner) + " module. " + d->text + "\n";
        else h.markdown = code_block("javascript", word) + "Canopy reads this as " + (what.empty() ? owner : what) + ", so typing " + inline_code(word + ".") + " lists its members.\n";
        return h;
    }
    return std::nullopt;
}

std::optional<HoverResult> css_hover(const std::string& text, std::size_t line, std::size_t column, std::size_t offset, std::size_t region) {
    const std::optional<std::string> found = text_line_at(text, line);
    if (!found) return std::nullopt;
    const std::string& l = *found;
    if (column > l.size()) return std::nullopt;
    std::size_t b = column;
    std::size_t e = column;
    while (b > 0 && css_char(l[b - 1])) --b;
    while (e < l.size() && css_char(l[e])) ++e;
    if (b == e) return std::nullopt;
    const std::string word = l.substr(b, e - b);
    HoverResult h{ line, b, e - b, std::string() };
    std::size_t after = e;
    while (after < l.size() && (l[after] == ' ' || l[after] == '\t')) ++after;
    const std::size_t at = offset - (column - b);
    int depth = 0;
    for (std::size_t i = region; i < at && i < text.size(); ++i) {
        if (text[i] == '{') ++depth;
        else if (text[i] == '}' && depth > 0) --depth;
    }
    if (b > 0 && l[b - 1] == '@') {
        if (const WebDoc* d = find_web_doc("css-at", "@" + word)) {
            h.column = b - 1;
            h.length = word.size() + 1;
            h.markdown = doc_markdown(*d, "css", std::string());
            return h;
        }
    }
    if (after < l.size() && l[after] == '(') {
        if (const WebDoc* d = find_web_doc("css-func", word)) {
            h.markdown = doc_markdown(*d, "css", std::string());
            return h;
        }
    }
    if (b > 0 && l[b - 1] == ':') {
        const bool two = b > 1 && l[b - 2] == ':';
        const std::string name = (two ? "::" : ":") + word;
        if (const WebDoc* d = find_web_doc("css-pseudo", name)) {
            h.column = b - (two ? 2 : 1);
            h.length = name.size();
            h.markdown = doc_markdown(*d, "css", std::string());
            return h;
        }
        if (const WebDoc* d = find_web_doc("css-pseudo", name + "()")) {
            h.column = b - (two ? 2 : 1);
            h.length = name.size();
            h.markdown = doc_markdown(*d, "css", std::string());
            return h;
        }
    }
    if (depth > 0 && after < l.size() && l[after] == ':') {
        if (const WebDoc* d = find_web_doc("css-prop", word)) {
            h.markdown = code_block("css", word + ": " + d->signature + ";") + d->text + "\n";
            return h;
        }
        if (starts_with(word, "--")) {
            h.markdown = code_block("css", word + ": …;") + "A custom property; read it with " + inline_code("var(" + word + ")") + ". Children inherit it.\n";
            return h;
        }
        return std::nullopt;
    }
    if (depth == 0) {
        if (const WebDoc* d = find_web_doc("html-tag", lower(word))) {
            if (b == 0 || (l[b - 1] != '.' && l[b - 1] != '#')) {
                h.markdown = code_block("html", "<" + lower(word) + ">") + d->text + "\n";
                return h;
            }
        }
    }
    return std::nullopt;
}

std::optional<HoverResult> html_hover(const std::string& text, std::size_t line, std::size_t column, std::size_t offset) {
    const std::optional<std::string> found = text_line_at(text, line);
    if (!found) return std::nullopt;
    const std::string& l = *found;
    if (column > l.size()) return std::nullopt;
    std::size_t b = column;
    std::size_t e = column;
    while (b > 0 && css_char(l[b - 1])) --b;
    while (e < l.size() && css_char(l[e])) ++e;
    if (b == e) return std::nullopt;
    const std::string word = lower(l.substr(b, e - b));
    HoverResult h{ line, b, e - b, std::string() };
    if ((b > 0 && l[b - 1] == '<') || (b > 1 && l[b - 1] == '/' && l[b - 2] == '<')) {
        if (const WebDoc* d = find_web_doc("html-tag", word)) {
            h.markdown = code_block("html", "<" + word + ">") + d->text + "\n";
            return h;
        }
        return std::nullopt;
    }
    const std::size_t at = offset - (column - b);
    const std::string head = text.substr(0, at);
    const std::size_t lt = head.rfind('<');
    const std::size_t gt = head.rfind('>');
    if (lt == std::string::npos || (gt != std::string::npos && gt > lt)) return std::nullopt;
    const std::string inside = head.substr(lt + 1);
    if (std::count(inside.begin(), inside.end(), '"') % 2 == 1) return std::nullopt;
    std::size_t n = 0;
    while (n < inside.size() && css_char(inside[n])) ++n;
    const std::string tag = lower(inside.substr(0, n));
    const WebDoc* d = find_web_doc("html-attr", word);
    if (!d && starts_with(word, "data-")) d = find_web_doc("html-attr", "data-");
    if (!d && starts_with(word, "aria-")) {
        h.markdown = code_block("html", word + "=\"…\"") + "An ARIA attribute for assistive technology.\n";
        return h;
    }
    if (!d) return std::nullopt;
    h.markdown = code_block("html", word + (d->kind == "flag" ? std::string() : std::string("=\"…\""))) + d->text + "\n";
    if (!d->signature.empty()) {
        bool fits = false;
        std::string list;
        std::size_t w = 0;
        while (w < d->signature.size()) {
            std::size_t s = d->signature.find(' ', w);
            if (s == std::string::npos) s = d->signature.size();
            const std::string t = d->signature.substr(w, s - w);
            if (t == tag) fits = true;
            list += (list.empty() ? "" : ", ") + inline_code("<" + t + ">");
            w = s + 1;
        }
        h.markdown += "---\n" + std::string(fits ? "Used on " : "Not an attribute of " + inline_code("<" + tag + ">") + "; it's used on ") + list + "\n";
    }
    return h;
}


struct Positions {
    std::vector<std::size_t> starts;
    explicit Positions(const std::string& text) {
        starts.push_back(0);
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '\n') starts.push_back(i + 1);
        }
    }
    std::pair<std::size_t, std::size_t> at(std::size_t offset) const {
        const auto it = std::upper_bound(starts.begin(), starts.end(), offset);
        const std::size_t line = static_cast<std::size_t>(it - starts.begin());
        return { line, offset - starts[line - 1] + 1 };
    }
};

void report(std::vector<CompilerMessage>& out, const Positions& pos, const std::string& path, std::size_t offset, const char* severity, const std::string& message) {
    const auto [line, column] = pos.at(offset);
    out.push_back(CompilerMessage{ path, line, column, severity, message });
}

std::size_t distance(const std::string& a, const std::string& b) {
    std::vector<std::size_t> row(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) row[j] = j;
    for (std::size_t i = 1; i <= a.size(); ++i) {
        std::size_t prev = row[0];
        row[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const std::size_t keep = row[j];
            row[j] = std::min({ row[j] + 1, row[j - 1] + 1, prev + (a[i - 1] == b[j - 1] ? 0 : 1) });
            prev = keep;
        }
    }
    return row[b.size()];
}

const std::set<std::string>& known_css_properties() {
    static const std::set<std::string> names = [] {
        std::set<std::string> out;
        for (const WebDoc* d : web_docs_of("css-known")) out.insert(d->name);
        for (const WebDoc* d : web_docs_of("css-prop")) out.insert(d->name);
        return out;
    }();
    return names;
}

std::string nearest_property(const std::string& name) {
    std::string best;
    std::size_t best_d = 3;
    for (const std::string& k : known_css_properties()) {
        const std::size_t d = distance(name, k);
        if (d < best_d) {
            best_d = d;
            best = k;
        }
    }
    return best;
}

void lint_css_range(const std::string& text, std::size_t from, std::size_t to, const Positions& pos, const std::string& path, std::vector<CompilerMessage>& out) {
    struct Block {
        std::size_t at = 0;
        bool declarations = true;
    };
    std::vector<Block> stack;
    std::size_t segment = from;
    auto check_declaration = [&](std::size_t b, std::size_t e) {
        if (stack.empty() || !stack.back().declarations) return;
        std::size_t s = b;
        while (s < e && std::isspace(static_cast<unsigned char>(text[s]))) ++s;
        std::size_t f = e;
        while (f > s && std::isspace(static_cast<unsigned char>(text[f - 1]))) --f;
        if (s >= f) return;
        if (text[s] == '@' || text[s] == '&') return;
        const std::string decl = text.substr(s, f - s);
        const std::size_t colon = decl.find(':');
        if (colon == std::string::npos) {
            report(out, pos, path, s, "warning", "Expected property: value here");
            return;
        }
        const std::string name = lower(trim(decl.substr(0, colon)));
        const std::string value = decl.substr(colon + 1);
        if (name.empty()) return;
        if (name.find_first_of(" \t\n") != std::string::npos) {
            report(out, pos, path, s, "warning", "Expected property: value here");
            return;
        }
        if (trim(value).empty()) report(out, pos, path, s, "warning", "'" + name + "' has no value");
        if (!starts_with(name, "-") && !known_css_properties().count(name)) {
            const std::string near = nearest_property(name);
            report(out, pos, path, s, "warning", "Unknown property '" + name + "'" + (near.empty() ? std::string() : " (did you mean '" + near + "'?)"));
        }
        const std::size_t nl = value.find('\n');
        if (nl != std::string::npos) {
            const std::string next = trim(value.substr(nl + 1));
            std::size_t k = 0;
            while (k < next.size() && css_char(next[k])) ++k;
            if (k > 0 && k < next.size() && next[k] == ':' && (k + 1 >= next.size() || next[k + 1] != ':')) {
                std::size_t line_end = s + colon + 1 + nl;
                while (line_end > s && std::isspace(static_cast<unsigned char>(text[line_end - 1]))) --line_end;
                report(out, pos, path, line_end > s ? line_end - 1 : s, "error", "Missing ; after '" + name + "'");
            }
        }
    };
    for (std::size_t i = from; i < to; ++i) {
        const char c = text[i];
        if (c == '/' && i + 1 < to && text[i + 1] == '*') {
            const std::size_t e = text.find("*/", i + 2);
            if (e == std::string::npos || e + 2 > to) {
                report(out, pos, path, i, "error", "This comment is never closed");
                return;
            }
            i = e + 1;
            continue;
        }
        if (c == '"' || c == '\'') {
            std::size_t j = i + 1;
            while (j < to && text[j] != c && text[j] != '\n') {
                if (text[j] == '\\') ++j;
                ++j;
            }
            i = j < to ? j : to;
            continue;
        }
        if (c == '{') {
            const std::string prelude = lower(trim(text.substr(segment, i - segment)));
            Block b;
            b.at = i;
            const bool nested_rules = starts_with(prelude, "@media") || starts_with(prelude, "@supports") || starts_with(prelude, "@layer") || starts_with(prelude, "@container") || starts_with(prelude, "@keyframes") || starts_with(prelude, "@-webkit-keyframes") || starts_with(prelude, "@document") || starts_with(prelude, "@scope") || starts_with(prelude, "@starting-style");
            b.declarations = !nested_rules;
            stack.push_back(b);
            segment = i + 1;
            continue;
        }
        if (c == '}') {
            if (stack.empty()) {
                report(out, pos, path, i, "error", "Unexpected }: there's no { for it to close");
                segment = i + 1;
                continue;
            }
            check_declaration(segment, i);
            stack.pop_back();
            segment = i + 1;
            continue;
        }
        if (c == ';') {
            check_declaration(segment, i);
            segment = i + 1;
        }
    }
    for (const Block& b : stack) report(out, pos, path, b.at, "error", "This { is never closed");
}

const std::set<std::string>& optional_close() {
    static const std::set<std::string> tags = { "p", "li", "dt", "dd", "tr", "td", "th", "thead", "tbody", "tfoot", "option", "optgroup", "colgroup", "caption", "html", "head", "body", "rb", "rt", "rtc", "rp" };
    return tags;
}

const std::set<std::string>& void_html() {
    static const std::set<std::string> tags = { "area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "param", "source", "track", "wbr" };
    return tags;
}

std::size_t find_ci(const std::string& text, const std::string& needle, std::size_t from) {
    for (std::size_t i = from; i + needle.size() <= text.size(); ++i) {
        bool same = true;
        for (std::size_t k = 0; k < needle.size() && same; ++k) same = std::tolower(static_cast<unsigned char>(text[i + k])) == needle[k];
        if (same) return i;
    }
    return std::string::npos;
}

void lint_html(const std::string& text, const Positions& pos, const std::string& path, std::vector<CompilerMessage>& out) {
    struct Open {
        std::string name;
        std::size_t at = 0;
    };
    std::vector<Open> stack;
    std::map<std::string, std::size_t> ids;
    std::size_t foreign = 0;
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t lt = text.find('<', i);
        if (lt == std::string::npos) break;
        if (text.compare(lt, 4, "<!--") == 0) {
            const std::size_t e = text.find("-->", lt + 4);
            if (e == std::string::npos) {
                report(out, pos, path, lt, "warning", "This comment is never closed");
                break;
            }
            i = e + 3;
            continue;
        }
        if (lt + 1 < text.size() && (text[lt + 1] == '!' || text[lt + 1] == '?')) {
            const std::size_t e = text.find('>', lt);
            i = e == std::string::npos ? text.size() : e + 1;
            continue;
        }
        const bool closing = lt + 1 < text.size() && text[lt + 1] == '/';
        std::size_t j = lt + (closing ? 2 : 1);
        const std::size_t name_at = j;
        while (j < text.size() && (std::isalnum(static_cast<unsigned char>(text[j])) || text[j] == '-' || text[j] == ':' || text[j] == '_' || text[j] == '.')) ++j;
        if (j == name_at || !std::isalpha(static_cast<unsigned char>(text[name_at]))) {
            i = lt + 1;
            continue;
        }
        const std::string raw = text.substr(name_at, j - name_at);
        const std::string name = foreign ? raw : lower(raw);
        std::map<std::string, std::size_t> attrs;
        bool self_closing = false;
        std::size_t k = j;
        while (k < text.size() && text[k] != '>') {
            const char c = text[k];
            if (c == '"' || c == '\'') {
                const std::size_t e = text.find(c, k + 1);
                if (e == std::string::npos) {
                    k = text.size();
                    break;
                }
                k = e + 1;
                continue;
            }
            if (c == '/' && k + 1 < text.size() && text[k + 1] == '>') {
                self_closing = true;
                ++k;
                continue;
            }
            if (c == '<') break;
            if (!closing && (std::isalpha(static_cast<unsigned char>(c)) || c == '@' || c == ':' || c == '_')) {
                std::size_t e = k;
                while (e < text.size() && !std::isspace(static_cast<unsigned char>(text[e])) && text[e] != '=' && text[e] != '>' && text[e] != '/') ++e;
                const std::string attr = lower(text.substr(k, e - k));
                if (attrs.count(attr)) report(out, pos, path, k, "warning", "'" + attr + "' is set twice on <" + name + ">");
                else attrs[attr] = k;
                std::size_t v = e;
                while (v < text.size() && std::isspace(static_cast<unsigned char>(text[v]))) ++v;
                if (v < text.size() && text[v] == '=') {
                    ++v;
                    while (v < text.size() && std::isspace(static_cast<unsigned char>(text[v]))) ++v;
                    std::string value;
                    if (v < text.size() && (text[v] == '"' || text[v] == '\'')) {
                        const std::size_t q = text.find(text[v], v + 1);
                        value = text.substr(v + 1, (q == std::string::npos ? text.size() : q) - v - 1);
                        e = q == std::string::npos ? text.size() : q + 1;
                    } else {
                        std::size_t q = v;
                        while (q < text.size() && !std::isspace(static_cast<unsigned char>(text[q])) && text[q] != '>') ++q;
                        value = text.substr(v, q - v);
                        e = q;
                    }
                    if (attr == "id" && !value.empty() && value.find("{{") == std::string::npos && value.find("<%") == std::string::npos) {
                        const std::size_t line = pos.at(k).first;
                        const auto seen = ids.find(value);
                        if (seen != ids.end()) report(out, pos, path, k, "warning", "id \"" + value + "\" is already used on line " + std::to_string(seen->second) + "; ids should be unique");
                        else ids[value] = line;
                    }
                }
                k = std::max(e, k + 1);
                continue;
            }
            ++k;
        }
        if (k >= text.size()) {
            report(out, pos, path, lt, "error", "<" + std::string(closing ? "/" : "") + raw + " is never finished with >");
            break;
        }
        const std::size_t gt = text[k] == '>' ? k : k - 1;
        i = gt + 1;
        if (closing) {
            if (!foreign && void_html().count(name)) {
                report(out, pos, path, lt, "warning", "</" + name + "> isn't needed: <" + name + "> has no closing tag");
                continue;
            }
            std::size_t match = stack.size();
            for (std::size_t s = stack.size(); s > 0; --s) {
                if (stack[s - 1].name == name) {
                    match = s - 1;
                    break;
                }
            }
            if (match == stack.size()) {
                report(out, pos, path, lt, "error", "</" + name + "> has no matching <" + name + ">");
                continue;
            }
            for (std::size_t s = stack.size(); s > match + 1; --s) {
                const Open& o = stack[s - 1];
                if (optional_close().count(o.name) || foreign) continue;
                report(out, pos, path, o.at, "error", "<" + o.name + "> is never closed (</" + name + "> on line " + std::to_string(pos.at(lt).first) + " closes its parent first)");
            }
            for (std::size_t s = stack.size(); s > match; --s) {
                if (stack[s - 1].name == "svg" || stack[s - 1].name == "math") foreign = foreign > 0 ? foreign - 1 : 0;
            }
            stack.resize(match);
            continue;
        }
        if (!foreign && !find_web_doc("html-tag", name) && name.find('-') == std::string::npos && name != "svg" && name != "math") report(out, pos, path, name_at, "warning", "<" + name + "> isn't an HTML element");
        if (name == "img" && !attrs.count("alt")) report(out, pos, path, lt, "warning", "<img> has no alt text; screen readers read alt, and it shows when the image can't load");
        if (self_closing && (foreign || void_html().count(name))) continue;
        if (void_html().count(name) && !foreign) continue;
        if (name == "script" || name == "style" || name == "textarea" || name == "title") {
            const std::size_t close = find_ci(text, "</" + name, i);
            if (close == std::string::npos) {
                report(out, pos, path, lt, "error", "<" + name + "> is never closed");
                break;
            }
            if (name == "style") lint_css_range(text, i, close, pos, path, out);
            const std::size_t e = text.find('>', close);
            i = e == std::string::npos ? text.size() : e + 1;
            continue;
        }
        if (name == "svg" || name == "math") ++foreign;
        stack.push_back(Open{ name, lt });
    }
    for (const Open& o : stack) {
        if (optional_close().count(o.name)) continue;
        report(out, pos, path, o.at, "error", "<" + o.name + "> is never closed");
    }
}
} // namespace

bool is_web_language(const std::string& language) {
    return language == "html" || language == "css" || language == "javascript";
}

WebRegionAt web_region_at(const std::string& html, std::size_t offset) {
    WebRegionAt out;
    offset = std::min(offset, html.size());
    const std::string head = lower(html.substr(0, offset));
    for (const char* tag : { "script", "style" }) {
        const std::string open = std::string("<") + tag;
        const std::string close = std::string("</") + tag;
        std::size_t at = head.rfind(open);
        while (at != std::string::npos && at + open.size() < head.size() && css_char(head[at + open.size()])) at = at == 0 ? std::string::npos : head.rfind(open, at - 1);
        if (at == std::string::npos) continue;
        const std::size_t gt = head.find('>', at);
        if (gt == std::string::npos) continue;
        if (head.find(close, gt) != std::string::npos) continue;
        out.region = std::string(tag) == "script" ? WebRegion::Script : WebRegion::Css;
        out.start = gt + 1;
        return out;
    }
    return out;
}

std::string js_object_owner(const std::string& text, std::size_t dot) {
    return owner_before(text, dot, 0);
}

std::optional<std::string> js_module_string_at(const std::string& line, std::size_t column) {
    column = std::min(column, line.size());
    char quote = 0;
    std::size_t open = 0;
    for (std::size_t i = 0; i < column; ++i) {
        const char c = line[i];
        if (quote) {
            if (c == '\\') {
                ++i;
                continue;
            }
            if (c == quote) quote = 0;
            continue;
        }
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') return std::nullopt;
        if (c == '"' || c == '\'' || c == '`') {
            quote = c;
            open = i;
        }
    }
    if (!quote) {
        if (column < line.size() && (line[column] == '"' || line[column] == '\'')) return std::nullopt;
        return std::nullopt;
    }
    const std::string before = trim(line.substr(0, open));
    if (!(ends_with(before, "require(") || ends_with(before, "import(") || ends_with(before, "from") || ends_with(before, "import"))) return std::nullopt;
    const std::size_t close = line.find(quote, open + 1);
    return line.substr(open + 1, (close == std::string::npos ? line.size() : close) - open - 1);
}

std::string css_property_before(const std::string& text, std::size_t offset) {
    offset = std::min(offset, text.size());
    std::size_t s = offset;
    while (s > 0 && text[s - 1] != ';' && text[s - 1] != '{' && text[s - 1] != '}') --s;
    const std::string segment = text.substr(s, offset - s);
    const std::size_t colon = segment.find(':');
    if (colon == std::string::npos) return std::string();
    const std::string name = trim(segment.substr(0, colon));
    const std::size_t space = name.find_last_of(" \t\r\n");
    return lower(space == std::string::npos ? name : name.substr(space + 1));
}

std::vector<std::string> node_package_names(const std::string& file) {
    std::vector<std::string> out;
    std::error_code ec;
    for (fs::path d = fs::path(file).parent_path(); !d.empty(); d = d.parent_path()) {
        const fs::path modules = d / "node_modules";
        if (fs::is_directory(modules, ec)) {
            for (fs::directory_iterator it(modules, fs::directory_options::skip_permission_denied, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
                const std::string name = it->path().filename().string();
                if (name.empty() || name[0] == '.') continue;
                if (name[0] == '@') {
                    std::error_code ec2;
                    for (fs::directory_iterator sub(it->path(), fs::directory_options::skip_permission_denied, ec2); !ec2 && sub != fs::directory_iterator(); sub.increment(ec2)) {
                        out.push_back(name + "/" + sub->path().filename().string());
                        if (out.size() > 3000) return out;
                    }
                    continue;
                }
                out.push_back(name);
                if (out.size() > 3000) return out;
            }
            return out;
        }
        if (d == d.parent_path()) break;
    }
    return out;
}

std::vector<CompilerMessage> lint_web(const std::string& language, const std::string& text, const std::string& path) {
    std::vector<CompilerMessage> out;
    const Positions pos(text);
    if (language == "css") lint_css_range(text, 0, text.size(), pos, path, out);
    else if (language == "html") lint_html(text, pos, path, out);
    if (out.size() > 200) out.resize(200);
    return out;
}

std::string web_script_text(const std::string& html, bool& module) {
    module = false;
    std::string out(html.size(), ' ');
    for (std::size_t i = 0; i < html.size(); ++i) {
        if (html[i] == '\n' || html[i] == '\r') out[i] = html[i];
    }
    std::size_t at = 0;
    bool any = false;
    while ((at = find_ci(html, "<script", at)) != std::string::npos) {
        const std::size_t gt = html.find('>', at);
        if (gt == std::string::npos) break;
        const std::string open = lower(html.substr(at, gt - at));
        const std::size_t close = find_ci(html, "</script", gt + 1);
        const std::size_t end = close == std::string::npos ? html.size() : close;
        at = end;
        if (open.find(" src") != std::string::npos) continue;
        const std::size_t type_at = open.find("type");
        if (type_at != std::string::npos) {
            const std::string type = open.substr(type_at);
            if (type.find("module") != std::string::npos) module = true;
            else if (type.find("javascript") == std::string::npos && type.find("ecmascript") == std::string::npos && type.find("babel") == std::string::npos) continue;
            if (type.find("babel") != std::string::npos || type.find("jsx") != std::string::npos) continue;
        }
        for (std::size_t i = gt + 1; i < end; ++i) out[i] = html[i];
        any = true;
    }
    if (!any) return std::string();
    return out;
}

bool javascript_is_module(const std::string& js, const std::string& path) {
    const std::string ext = lower(fs::path(path).extension().string());
    if (ext == ".mjs") return true;
    if (ext == ".cjs") return false;
    std::size_t at = 0;
    while (at < js.size()) {
        std::size_t e = js.find('\n', at);
        if (e == std::string::npos) e = js.size();
        const std::string line = trim(js.substr(at, e - at));
        if ((starts_with(line, "import ") && line.find("from") != std::string::npos) || starts_with(line, "import \"") || starts_with(line, "import '") || starts_with(line, "import {") || starts_with(line, "export ")) return true;
        at = e + 1;
    }
    std::error_code ec;
    for (fs::path d = fs::path(path).parent_path(); !d.empty(); d = d.parent_path()) {
        const fs::path pkg = d / "package.json";
        if (fs::is_regular_file(pkg, ec)) {
            std::ifstream in(pkg, std::ios::binary);
            std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            const std::size_t t = content.find("\"type\"");
            if (t == std::string::npos) return false;
            const std::size_t m = content.find("module", t);
            const std::size_t comma = content.find_first_of(",}", t);
            return m != std::string::npos && m < comma;
        }
        if (d == d.parent_path()) break;
    }
    return false;
}

std::vector<CompilerMessage> parse_node_check(const std::string& output, const std::string& path) {
    std::vector<CompilerMessage> out;
    std::vector<std::string> lines;
    std::size_t at = 0;
    while (at <= output.size()) {
        std::size_t e = output.find('\n', at);
        if (e == std::string::npos) e = output.size();
        std::string l = output.substr(at, e - at);
        if (!l.empty() && l.back() == '\r') l.pop_back();
        lines.push_back(l);
        at = e + 1;
    }
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string& l = lines[i];
        const std::size_t colon = l.rfind(':');
        if (colon == std::string::npos || colon + 1 >= l.size()) continue;
        if (!(starts_with(l, "[stdin]:") || starts_with(l, "[eval]:") || l.find(".js:") != std::string::npos || l.find(".mjs:") != std::string::npos || l.find(".cjs:") != std::string::npos)) continue;
        const std::string num = l.substr(colon + 1);
        if (num.empty() || num.find_first_not_of("0123456789") != std::string::npos) continue;
        CompilerMessage m;
        m.file = path;
        m.line = static_cast<std::size_t>(std::stoul(num));
        m.severity = "error";
        if (i + 2 < lines.size()) {
            const std::size_t caret = lines[i + 2].find('^');
            if (caret != std::string::npos && lines[i + 2].find_first_not_of(" ^") == std::string::npos) m.column = caret + 1;
        }
        for (std::size_t k = i + 1; k < lines.size() && k < i + 8; ++k) {
            const std::string& t = lines[k];
            const std::size_t c = t.find("Error: ");
            if (c != std::string::npos && c > 0 && std::isalpha(static_cast<unsigned char>(t[0]))) {
                m.message = t;
                break;
            }
        }
        if (m.message.empty()) continue;
        out.push_back(std::move(m));
        break;
    }
    return out;
}

std::optional<HoverResult> web_hover(const std::string& language, const std::string& text, std::size_t line, std::size_t column) {
    const std::size_t offset = offset_of(text, line, column);
    if (language == "javascript") return js_hover(text, line, column, offset, true);
    if (language == "css") return css_hover(text, line, column, offset, 0);
    if (language != "html") return std::nullopt;
    const WebRegionAt region = web_region_at(text, offset);
    if (region.region == WebRegion::Script) return js_hover(text, line, column, offset, false);
    if (region.region == WebRegion::Css) return css_hover(text, line, column, offset, region.start);
    return html_hover(text, line, column, offset);
}

} // namespace canopy
