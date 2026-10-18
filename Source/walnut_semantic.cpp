#include <canopy/walnut_semantic.hpp>
#include <canopy/build.hpp>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace canopy {

namespace {

struct Json {
    enum class Kind {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    Kind kind = Kind::Null;
    bool boolean = false;
    double number = 0;
    std::string text;
    std::vector<Json> items;
    std::map<std::string, Json> fields;

    const Json* get(const std::string& key) const {
        const auto it = fields.find(key);
        return it == fields.end() ? nullptr : &it->second;
    }

    std::size_t index() const { return number < 0 ? 0 : static_cast<std::size_t>(number); }
};

class JsonReader {
public:
    explicit JsonReader(const std::string& text) : m_text(text) {}

    bool read(Json& out) {
        skip();
        if (!value(out, 0)) return false;
        skip();
        return true;
    }

private:
    void skip() {
        while (m_at < m_text.size() && (m_text[m_at] == ' ' || m_text[m_at] == '\n' || m_text[m_at] == '\r' || m_text[m_at] == '\t')) ++m_at;
    }

    bool literal(const char* word) {
        const std::string w(word);
        if (m_text.compare(m_at, w.size(), w) != 0) return false;
        m_at += w.size();
        return true;
    }

    static void put_utf8(std::string& out, unsigned code) {
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (code >> 18));
            out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }

    bool hex4(unsigned& code) {
        if (m_at + 4 > m_text.size()) return false;
        code = static_cast<unsigned>(std::strtoul(m_text.substr(m_at, 4).c_str(), nullptr, 16));
        m_at += 4;
        return true;
    }

    bool string(std::string& out) {
        if (m_at >= m_text.size() || m_text[m_at] != '"') return false;
        ++m_at;
        while (m_at < m_text.size()) {
            const char c = m_text[m_at++];
            if (c == '"') return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (m_at >= m_text.size()) return false;
            const char e = m_text[m_at++];
            switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    unsigned code = 0;
                    if (!hex4(code)) return false;
                    if (code >= 0xD800 && code < 0xDC00 && m_text.compare(m_at, 2, "\\u") == 0) {
                        m_at += 2;
                        unsigned low = 0;
                        if (!hex4(low)) return false;
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    }
                    put_utf8(out, code);
                    break;
                }
                default: out += e; break;
            }
        }
        return false;
    }

    bool value(Json& out, int depth) {
        if (depth > 64 || m_at >= m_text.size()) return false;
        const char c = m_text[m_at];
        if (c == '{') {
            out.kind = Json::Kind::Object;
            ++m_at;
            skip();
            if (m_at < m_text.size() && m_text[m_at] == '}') {
                ++m_at;
                return true;
            }
            while (true) {
                skip();
                std::string key;
                if (!string(key)) return false;
                skip();
                if (m_at >= m_text.size() || m_text[m_at] != ':') return false;
                ++m_at;
                skip();
                Json item;
                if (!value(item, depth + 1)) return false;
                out.fields[key] = std::move(item);
                skip();
                if (m_at < m_text.size() && m_text[m_at] == ',') {
                    ++m_at;
                    continue;
                }
                if (m_at < m_text.size() && m_text[m_at] == '}') {
                    ++m_at;
                    return true;
                }
                return false;
            }
        }
        if (c == '[') {
            out.kind = Json::Kind::Array;
            ++m_at;
            skip();
            if (m_at < m_text.size() && m_text[m_at] == ']') {
                ++m_at;
                return true;
            }
            while (true) {
                skip();
                Json item;
                if (!value(item, depth + 1)) return false;
                out.items.push_back(std::move(item));
                skip();
                if (m_at < m_text.size() && m_text[m_at] == ',') {
                    ++m_at;
                    continue;
                }
                if (m_at < m_text.size() && m_text[m_at] == ']') {
                    ++m_at;
                    return true;
                }
                return false;
            }
        }
        if (c == '"') {
            out.kind = Json::Kind::String;
            return string(out.text);
        }
        if (literal("true")) {
            out.kind = Json::Kind::Bool;
            out.boolean = true;
            return true;
        }
        if (literal("false")) {
            out.kind = Json::Kind::Bool;
            return true;
        }
        if (literal("null")) return true;
        const char* begin = m_text.c_str() + m_at;
        char* end = nullptr;
        out.number = std::strtod(begin, &end);
        if (end == begin) return false;
        out.kind = Json::Kind::Number;
        m_at += static_cast<std::size_t>(end - begin);
        return true;
    }

    const std::string& m_text;
    std::size_t m_at = 0;
};

const std::set<std::string>& walnut_words() {
    static const std::set<std::string> k = {
        "self", "this",
        "fallthrough", "co_return", "co_yield", "continue", "default", "repeat", "return", "switch", "await", "break", "catch", "label", "throw",
        "while", "case", "down", "else", "goto", "then", "end", "for", "try", "as", "do", "if", "in", "is", "invalidate",
        "constructor", "destructor", "namespace", "function", "inherits", "operator", "requires", "template", "typename", "concept", "extends",
        "include", "inherit", "typedef", "define", "export", "import", "module", "struct", "class", "union", "using", "enum", "def", "let",
        "thread_local", "threadlocal", "no_discard", "threadsafe", "automatic", "consteval", "constexpr", "constinit", "nodiscard", "protected",
        "abstract", "constant", "explicit", "implicit", "noexcept", "override", "volatile", "hoisted", "mutable", "private", "virtual", "extern",
        "friend", "global", "hidden", "inline", "public", "static", "async", "const", "hoist", "local", "lazy",
        "character", "iterable", "unsigned", "variable", "boolean", "decimal", "dynamic", "floater", "integer", "special", "variant", "double",
        "string", "array", "float", "short", "auto", "bool", "char", "long", "text", "void", "any", "arr", "int", "str", "txt", "var",
        "indeterminate", "undefined", "infinity", "nullptr", "FALSE", "False", "false", "TRUE", "True", "true", "inf", "nan",
        "reinterpret_cast", "dynamic_cast", "static_cast", "const_cast", "bit_cast", "bitcast",
        "discard_no_discard", "discard_nodiscard", "discard_const", "static_assert", "align_as", "align_of", "count_of", "decltype", "alignas",
        "alignof", "countof", "size_of", "type_id", "type_of", "delete", "sizeof", "typeid", "typeof", "new",
    };
    return k;
}

bool name_start(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; }

bool name_part(char c) { return name_start(c) || (c >= '0' && c <= '9'); }

std::string clean_message(const std::string& message) {
    const std::size_t on = message.find(" on line ");
    if (on == std::string::npos) return message;
    std::size_t colon = on + 9;
    while (colon < message.size() && message[colon] >= '0' && message[colon] <= '9') ++colon;
    if (colon < message.size() && message[colon] == ':') return message.substr(0, on) + message.substr(colon);
    return message;
}

std::string first_line(const std::string& message) {
    const std::size_t nl = message.find('\n');
    return nl == std::string::npos ? message : message.substr(0, nl);
}

} // namespace

std::vector<std::string> text_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (true) {
        const std::size_t end = text.find('\n', start);
        std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return lines;
}

grove::CodeTokenKind walnut_semantic_kind(const std::string& type, const std::vector<std::string>& modifiers) {
    using K = grove::CodeTokenKind;
    auto has = [&](const char* m) { return std::find(modifiers.begin(), modifiers.end(), m) != modifiers.end(); };
    if (has("templateQualifier")) return K::TemplateScope;
    if (type == "variable" || type == "parameter" || type == "property") {
        if (has("variableTemplate")) return K::VariableTemplate;
        if (has("declaration")) return K::Declaration;
        return K::Variable;
    }
    if (type == "unresolved") return K::Unresolved;
    if (type == "operator") return has("overloaded") ? K::OverloadedOperator : K::BuiltinOperator;
    if (type == "function" || type == "method") return K::Function;
    if (type == "class" || type == "struct" || type == "enum" || type == "type" || type == "typeParameter" || type == "concept") return K::Type;
    if (type == "namespace") return K::Namespace;
    if (type == "enumMember") return K::EnumMember;
    return K::Plain;
}

std::size_t utf16_to_byte(const std::string& line, std::size_t units) {
    std::size_t i = 0;
    std::size_t counted = 0;
    while (i < line.size() && counted < units) {
        const unsigned char c = static_cast<unsigned char>(line[i]);
        std::size_t len = 1;
        if (c >= 0xF0) len = 4;
        else if (c >= 0xE0) len = 3;
        else if (c >= 0xC0) len = 2;
        counted += len == 4 ? 2 : 1;
        i = std::min(line.size(), i + len);
    }
    return i;
}

std::vector<grove::CodeSpan> walnut_unresolved_spans(const std::string& text, const std::vector<grove::CodeSpan>& covered) {
    std::vector<grove::CodeSpan> names;
    const std::size_t n = text.size();
    std::size_t i = 0;
    std::size_t line = 0;
    std::size_t line_start = 0;
    bool line_has_code = false;
    std::string previous;
    std::string before_previous;
    auto newline = [&] {
        ++line;
        line_start = i + 1;
        line_has_code = false;
        previous.clear();
        before_previous.clear();
    };
    auto to_line_end = [&] {
        while (i < n && text[i] != '\n') ++i;
    };
    while (i < n) {
        const char c = text[i];
        if (c == '\n') {
            newline();
            ++i;
            continue;
        }
        if (c == '/' && i + 1 < n && text[i + 1] == '/') {
            to_line_end();
            continue;
        }
        if (c == '/' && i + 1 < n && text[i + 1] == '*') {
            int depth = 0;
            while (i < n) {
                if (text[i] == '/' && i + 1 < n && text[i + 1] == '*') {
                    ++depth;
                    i += 2;
                    continue;
                }
                if (text[i] == '*' && i + 1 < n && text[i + 1] == '/') {
                    --depth;
                    i += 2;
                    if (depth == 0) break;
                    continue;
                }
                if (text[i] == '\n') newline();
                ++i;
            }
            continue;
        }
        if (c == '"' || c == '\'' || c == '`') {
            ++i;
            while (i < n) {
                const char d = text[i];
                if (d == c) {
                    ++i;
                    break;
                }
                if (d == '\\') {
                    i += 2;
                    continue;
                }
                if (d == '\n') {
                    if (c != '`') break;
                    newline();
                }
                ++i;
            }
            line_has_code = true;
            previous.clear();
            before_previous.clear();
            continue;
        }
        if (c == '#' && !line_has_code) {
            to_line_end();
            continue;
        }
        if (c >= '0' && c <= '9') {
            while (i < n && (name_part(text[i]) || text[i] == '.' || text[i] == '\'')) ++i;
            line_has_code = true;
            previous.clear();
            before_previous.clear();
            continue;
        }
        if (name_start(c)) {
            const std::size_t start = i;
            while (i < n && name_part(text[i])) ++i;
            const std::string word = text.substr(start, i - start);
            if (word == "import" || word == "include") {
                to_line_end();
                continue;
            }
            const bool named = before_previous == "end" || previous == "end" || previous == "goto" || previous == "label";
            if (!walnut_words().count(word) && !named) names.push_back(grove::CodeSpan{ line, start - line_start, word.size(), grove::CodeTokenKind::Unresolved });
            before_previous = previous;
            previous = word;
            line_has_code = true;
            continue;
        }
        if (c != ' ' && c != '\t' && c != '\r') {
            line_has_code = true;
            previous.clear();
            before_previous.clear();
        }
        ++i;
    }
    std::vector<grove::CodeSpan> out;
    for (const grove::CodeSpan& name : names) {
        bool hit = false;
        for (const grove::CodeSpan& s : covered) {
            if (s.line == name.line && name.column < s.column + s.length && name.column + name.length > s.column) {
                hit = true;
                break;
            }
        }
        if (!hit) out.push_back(name);
    }
    return out;
}

SemanticResult parse_walnut_highlight(const std::string& output, const std::string& text, const std::string& file, bool mark_unresolved) {
    SemanticResult result;
    const std::size_t start = output.find('{');
    if (start == std::string::npos) {
        result.error = "the compiler printed no result";
        return result;
    }
    Json root;
    const std::string body = output.substr(start);
    JsonReader reader(body);
    if (!reader.read(root) || root.kind != Json::Kind::Object) {
        result.error = "the compiler's result couldn't be read";
        return result;
    }
    result.ok = true;
    const std::vector<std::string> lines = text_lines(text);
    if (const Json* semantic = root.get("semantic")) result.semantic = semantic->boolean;
    if (const Json* tokens = root.get("tokens")) {
        for (const Json& t : tokens->items) {
            if (t.items.size() < 4) continue;
            const std::size_t line = t.items[0].index();
            if (line >= lines.size()) continue;
            std::vector<std::string> modifiers;
            if (t.items.size() > 4) {
                for (const Json& m : t.items[4].items) modifiers.push_back(m.text);
            }
            const grove::CodeTokenKind kind = walnut_semantic_kind(t.items[3].text, modifiers);
            if (kind == grove::CodeTokenKind::Plain) continue;
            const std::size_t begin = utf16_to_byte(lines[line], t.items[1].index());
            const std::size_t end = utf16_to_byte(lines[line], t.items[1].index() + t.items[2].index());
            if (end <= begin) continue;
            result.spans.push_back(grove::CodeSpan{ line, begin, end - begin, kind });
        }
    }
    if (mark_unresolved && result.semantic) {
        bool any = false;
        for (const grove::CodeSpan& s : result.spans) any = any || s.kind == grove::CodeTokenKind::Unresolved;
        if (!any) {
            for (const grove::CodeSpan& u : walnut_unresolved_spans(text, result.spans)) {
                if (u.line < lines.size()) result.spans.push_back(u);
            }
        }
    }
    auto strings = [](const Json* list) {
        std::vector<std::string> out;
        if (!list) return out;
        for (const Json& item : list->items) {
            if (item.kind == Json::Kind::String) out.push_back(item.text);
        }
        return out;
    };
    if (const Json* hovers = root.get("hovers")) {
        for (const Json& h : hovers->items) {
            const Json* l = h.get("line");
            if (!l || l->index() >= lines.size()) continue;
            WalnutHover hover;
            hover.line = l->index();
            const std::string& row = lines[hover.line];
            const std::size_t col = h.get("col") ? h.get("col")->index() : 0;
            const std::size_t len = h.get("len") ? h.get("len")->index() : 0;
            if (len == 0) continue;
            hover.column = utf16_to_byte(row, col);
            hover.length = utf16_to_byte(row, col + len) - hover.column;
            hover.line_text = row;
            if (const Json* code = h.get("code")) hover.code = code->text;
            hover.types = strings(h.get("types"));
            hover.values = strings(h.get("values"));
            hover.results = strings(h.get("results"));
            hover.overloads = strings(h.get("overloads"));
            if (const Json* g = h.get("guess")) hover.guess = g->kind == Json::Kind::Bool && g->boolean;
            if (const Json* d = h.get("doc")) hover.doc = d->text;
            result.hovers.push_back(std::move(hover));
        }
    }
    if (const Json* links = root.get("links")) {
        const Json* locations = root.get("locations");
        std::map<std::string, std::vector<std::string>> other_files;
        auto place = [&](std::size_t index) -> std::optional<CodeLocation> {
            if (!locations || index >= locations->items.size()) return std::nullopt;
            const Json& p = locations->items[index];
            if (p.items.size() < 4) return std::nullopt;
            CodeLocation where;
            where.file = p.items[0].text.empty() ? file : p.items[0].text;
            where.line = p.items[1].index();
            const std::vector<std::string>* rows = &lines;
            if (!p.items[0].text.empty()) {
                auto found = other_files.find(where.file);
                if (found == other_files.end()) {
                    std::ifstream in(where.file, std::ios::binary);
                    std::ostringstream body;
                    if (in) body << in.rdbuf();
                    found = other_files.emplace(where.file, text_lines(body.str())).first;
                }
                rows = &found->second;
            }
            const std::string row = where.line < rows->size() ? (*rows)[where.line] : std::string();
            where.column = utf16_to_byte(row, p.items[2].index());
            where.length = utf16_to_byte(row, p.items[2].index() + p.items[3].index()) - where.column;
            return where;
        };
        for (const Json& k : links->items) {
            if (k.items.size() < 4 || k.items[0].index() >= lines.size() || k.items[2].index() == 0) continue;
            WalnutLink link;
            link.line = k.items[0].index();
            const std::string& row = lines[link.line];
            link.column = utf16_to_byte(row, k.items[1].index());
            link.length = utf16_to_byte(row, k.items[1].index() + k.items[2].index()) - link.column;
            link.line_text = row;
            const std::optional<CodeLocation> definition = place(k.items[3].index());
            if (!definition) continue;
            link.definition = *definition;
            if (k.items.size() > 4) {
                for (const Json& d : k.items[4].items) {
                    if (auto where = place(d.index())) link.declarations.push_back(*where);
                }
            }
            result.links.push_back(std::move(link));
        }
    }
    if (const Json* diagnostics = root.get("diagnostics")) {
        for (const Json& d : diagnostics->items) {
            const Json* sev = d.get("severity");
            const bool warning = sev && sev->text == "warning";
            const std::size_t line = std::min(d.get("line") ? d.get("line")->index() : 0, lines.empty() ? 0 : lines.size() - 1);
            const std::string& row = lines.empty() ? std::string() : lines[line];
            std::size_t begin = 0;
            std::size_t end = 0;
            const std::size_t len = d.get("len") ? d.get("len")->index() : 0;
            const std::size_t col = d.get("col") ? d.get("col")->index() : 0;
            if (len > 0) {
                begin = utf16_to_byte(row, col);
                end = std::min(row.size(), utf16_to_byte(row, col + len));
            } else {
                const std::size_t lead = row.find_first_not_of(" \t");
                begin = lead == std::string::npos ? row.size() : lead;
                end = row.size();
            }
            result.problems.push_back(grove::CodeProblem{ line, begin, end > begin ? end - begin : 0, warning ? grove::CodeProblemKind::Warning : grove::CodeProblemKind::Error });
            CompilerMessage m;
            m.file = file;
            m.line = line + 1;
            m.column = begin + 1;
            m.severity = warning ? "warning" : "error";
            m.message = first_line(clean_message(d.get("message") ? d.get("message")->text : std::string()));
            result.messages.push_back(std::move(m));
        }
    }
    return result;
}

std::vector<std::string> walnut_highlight_command(const std::string& walnut, const std::string& file, const std::string& flags) {
    std::vector<std::string> command = { walnut, "highlight", file, "--stdin" };
    const std::vector<std::string> parts = split_flags(flags);
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const std::string& f = parts[i];
        const bool takes = f == "-I" || f == "-D" || f == "-M" || f == "--module-dir" || f == "--random-seed";
        const bool joined = f.size() > 2 && (f.rfind("-I", 0) == 0 || f.rfind("-D", 0) == 0) && f.rfind("--", 0) != 0;
        if (joined) command.push_back(f);
        else if (takes && i + 1 < parts.size()) {
            command.push_back(f);
            command.push_back(parts[++i]);
        }
    }
    return command;
}

namespace {

std::string shorten_text(std::string text) {
    if (text.size() > 400) text = text.substr(0, 400) + "\xE2\x80\xA6";
    return text;
}

std::string inline_code_text(std::string text) {
    text = shorten_text(text);
    std::replace(text.begin(), text.end(), '\n', ' ');
    std::replace(text.begin(), text.end(), '`', '\'');
    return "`" + text + "`";
}

template <typename T>
const T* entry_at(const std::vector<T>& entries, const std::vector<std::string>& lines, std::size_t line, std::size_t column) {
    if (line >= lines.size()) return nullptr;
    const std::string& now = lines[line];
    const T* best = nullptr;
    std::size_t distance = static_cast<std::size_t>(-1);
    for (const T& e : entries) {
        if (e.line_text != now) continue;
        if (column < e.column || column >= e.column + e.length) continue;
        const std::size_t d = e.line > line ? e.line - line : line - e.line;
        if (d < distance) {
            distance = d;
            best = &e;
        }
    }
    return best;
}

} // namespace

std::string walnut_hover_markdown(const WalnutHover& h, std::size_t page) {
    std::string md;
    const bool paged = h.overloads.size() > 1;
    const std::size_t at = paged ? page % h.overloads.size() : 0;
    if (paged) {
        md += "```walnut\n" + shorten_text(h.overloads[at]) + "\n```\n";
        md += "[\xE2\x80\xB9](overload:previous) " + std::to_string(at + 1) + " of " + std::to_string(h.overloads.size()) + " [\xE2\x80\xBA](overload:next)\n";
        if (h.guess) md += at == 0 ? "**Most likely.** This call can't be decided here, so these are the candidates, most likely first.\n" : "**Candidate " + std::to_string(at + 1) + "** of the ones this call might mean.\n";
        if (at > 0) return md;
    } else if (!h.code.empty()) {
        md += "```walnut\n" + shorten_text(h.code) + "\n```\n";
        if (h.guess) md += "**Most likely.** This call can't be decided here.\n";
    }
    auto add = [&](const char* one, const char* many, const std::vector<std::string>& items) {
        if (items.empty()) return;
        std::string line = std::string("**") + (items.size() > 1 ? many : one) + ":** ";
        for (std::size_t i = 0; i < items.size(); ++i) line += (i ? ", " : "") + inline_code_text(items[i]);
        md += line + "\n";
    };
    if (!h.doc.empty()) md += h.doc + "\n";
    add("Type", "Types", h.types);
    add("Value", "Values", h.values);
    add("Result", "Results", h.results);
    return md;
}

const WalnutHover* walnut_hover_at(const std::vector<WalnutHover>& hovers, const std::vector<std::string>& lines, std::size_t line, std::size_t column) { return entry_at(hovers, lines, line, column); }

const WalnutLink* walnut_link_at(const std::vector<WalnutLink>& links, const std::vector<std::string>& lines, std::size_t line, std::size_t column) { return entry_at(links, lines, line, column); }

std::string walnut_library_file(const std::string& walnut, const std::string& file) {
    const std::string root = "<walnut>/";
    if (file.rfind(root, 0) != 0) return file;
    const std::string rest = file.substr(root.size());
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path dir = fs::weakly_canonical(walnut, ec).parent_path();
    for (int i = 0; i < 6 && !dir.empty(); ++i) {
        for (const fs::path& folder : { fs::path("Library"), fs::path("library"), fs::path("share") / "walnut" / "Library" }) {
            const fs::path candidate = dir / folder / rest;
            if (fs::is_regular_file(candidate, ec)) return candidate.string();
        }
        const fs::path parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }
    return std::string();
}

namespace {

std::string library_cache_root() {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path base = fs::temp_directory_path(ec);
    if (ec) base = fs::path(".");
    return (base / "canopy-walnut-library").string();
}

} // namespace

std::string walnut_library_cache(const std::string& walnut) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path exe = fs::weakly_canonical(walnut, ec);
    std::string key = exe.string();
    const auto size = fs::file_size(exe, ec);
    if (!ec) key += "#" + std::to_string(size);
    const auto when = fs::last_write_time(exe, ec);
    if (!ec) key += "#" + std::to_string(static_cast<long long>(when.time_since_epoch().count()));
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%016llx", static_cast<unsigned long long>(std::hash<std::string>()(key)));
    return (fs::path(library_cache_root()) / buffer).string();
}

std::string walnut_library_path(const std::string& walnut, const std::string& file) {
    const std::string root = "<walnut>/";
    if (file.rfind(root, 0) != 0) return file;
    const std::string on_disk = walnut_library_file(walnut, file);
    if (!on_disk.empty()) return on_disk;
    if (walnut.empty()) return std::string();
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = walnut_library_cache(walnut);
    const fs::path target = dir / fs::path(file.substr(root.size()));
    if (fs::is_regular_file(target, ec)) return target.string();
    if (fs::exists(dir / ".done", ec)) return std::string();
    fs::create_directories(dir, ec);
    std::string output;
    const int code = run_command_capture({ walnut, "library", "--extract", dir.string() }, output);
    std::ofstream(dir / ".done") << code;
    return fs::is_regular_file(target, ec) ? target.string() : std::string();
}

bool is_walnut_library_copy(const std::string& path) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const std::string root = fs::weakly_canonical(library_cache_root(), ec).generic_string() + "/";
    const std::string here = fs::weakly_canonical(path, ec).generic_string();
    return here.rfind(root, 0) == 0;
}

std::string walnut_library_name(const std::string& file) {
    const std::string root = "<walnut>/";
    return file.rfind(root, 0) == 0 ? file.substr(root.size()) : file;
}

struct SemanticJob::State {
    std::mutex mutex;
    std::optional<Done> done;
    std::atomic<bool> running{ false };
    std::thread thread;
};

SemanticJob::SemanticJob() : m_state(std::make_shared<State>()) {}

SemanticJob::~SemanticJob() {
    if (m_state && m_state->thread.joinable()) m_state->thread.detach();
}

bool SemanticJob::start(const std::string& path, std::vector<std::string> command, std::string text) {
    if (m_state->running) return false;
    if (m_state->thread.joinable()) m_state->thread.join();
    m_state->running = true;
    std::shared_ptr<State> state = m_state;
    state->thread = std::thread([state, path, command = std::move(command), text = std::move(text)] {
        Done done;
        done.path = path;
        done.text = text;
        done.exit_code = run_command_capture_input(command, text, done.output);
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->done = std::move(done);
        }
        state->running = false;
    });
    return true;
}

bool SemanticJob::running() const { return m_state->running; }

std::optional<SemanticJob::Done> SemanticJob::take() {
    std::lock_guard<std::mutex> lock(m_state->mutex);
    std::optional<Done> out = std::move(m_state->done);
    m_state->done.reset();
    return out;
}

} // namespace canopy
