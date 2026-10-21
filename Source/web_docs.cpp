#include <canopy/web_docs.hpp>

#include <iterator>
#include <map>

namespace canopy {

namespace {

std::string trim(std::string_view s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) --e;
    return std::string(s.substr(b, e - b));
}

const std::map<std::string, std::vector<std::size_t>>& owner_index() {
    static const std::map<std::string, std::vector<std::size_t>> index = [] {
        std::map<std::string, std::vector<std::size_t>> out;
        const std::vector<WebDoc>& docs = web_docs();
        for (std::size_t i = 0; i < docs.size(); ++i) out[docs[i].owner].push_back(i);
        return out;
    }();
    return index;
}

} // namespace

std::vector<WebDoc> parse_web_docs(std::string_view text) {
    std::vector<WebDoc> out;
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string_view::npos) end = text.size();
        const std::string_view line = text.substr(at, end - at);
        at = end + 1;
        if (trim(line).empty()) continue;
        const std::size_t a = line.find('|');
        if (a == std::string_view::npos) continue;
        const std::size_t b = line.find('|', a + 1);
        if (b == std::string_view::npos) continue;
        const std::string owner = trim(line.substr(0, a));
        if (owner == "css-values" || owner == "css-known") {
            const std::string property = owner == "css-known" ? std::string() : trim(line.substr(a + 1, b - a - 1));
            const std::string words = trim(line.substr(b + 1));
            std::size_t w = 0;
            while (w < words.size()) {
                std::size_t e = words.find(' ', w);
                if (e == std::string::npos) e = words.size();
                if (e > w) out.push_back(WebDoc{ owner == "css-known" ? owner : "css-value:" + property, owner == "css-known" ? "property" : "value", words.substr(w, e - w), std::string(), std::string() });
                w = e + 1;
            }
            continue;
        }
        const std::size_t c = line.find('|', b + 1);
        const std::size_t last = line.rfind('|');
        if (c == std::string_view::npos || last <= c) continue;
        WebDoc d;
        d.owner = owner;
        d.kind = trim(line.substr(a + 1, b - a - 1));
        d.name = trim(line.substr(b + 1, c - b - 1));
        d.signature = trim(line.substr(c + 1, last - c - 1));
        d.text = trim(line.substr(last + 1));
        if (!d.name.empty()) out.push_back(std::move(d));
    }
    return out;
}

const std::vector<WebDoc>& web_docs() {
    static const std::vector<WebDoc> docs = [] {
        std::vector<WebDoc> all = parse_web_docs(web_docs_text());
        std::vector<WebDoc> more = parse_web_docs(language_docs_text());
        all.insert(all.end(), std::make_move_iterator(more.begin()), std::make_move_iterator(more.end()));
        return all;
    }();
    return docs;
}

std::vector<const WebDoc*> web_docs_of(const std::string& owner) {
    std::vector<const WebDoc*> out;
    const auto& index = owner_index();
    const auto it = index.find(owner);
    if (it == index.end()) return out;
    for (const std::size_t i : it->second) out.push_back(&web_docs()[i]);
    return out;
}

const WebDoc* find_web_doc(const std::string& owner, const std::string& name) {
    for (const WebDoc* d : web_docs_of(owner)) {
        if (d->name == name) return d;
    }
    return nullptr;
}

} // namespace canopy
