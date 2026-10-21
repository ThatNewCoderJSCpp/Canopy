#include <canopy/lang_intel.hpp>
#include <canopy/asm_info.hpp>
#include <canopy/web_docs.hpp>

#include <grove/input/code_buffer.hpp>

#include <algorithm>
#include <cctype>
#include <map>
#include <regex>
#include <set>

namespace canopy {

namespace {

bool ident_char(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    return std::isalnum(u) || c == '_' || c == '$' || u >= 0x80;
}

std::string trim(const std::string& s) {
    const std::size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return std::string();
    const std::size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

bool starts_with(const std::string& s, const std::string& head) { return s.rfind(head, 0) == 0; }

std::string code_block(const std::string& language, const std::string& code) { return "```" + language + "\n" + code + "\n```\n"; }

std::size_t offset_of(const std::string& text, std::size_t line, std::size_t column) {
    std::size_t at = 0;
    for (std::size_t l = 0; l < line && at < text.size(); ++l) {
        const std::size_t nl = text.find('\n', at);
        if (nl == std::string::npos) return text.size();
        at = nl + 1;
    }
    return std::min(text.size(), at + column);
}

bool has_owner(const std::string& owner) {
    return !web_docs_of(owner).empty();
}

std::size_t match_back(const std::string& t, std::size_t close) {
    const char c = t[close];
    const char open = c == ')' ? '(' : c == ']' ? '[' : '{';
    int depth = 0;
    for (std::size_t i = close + 1; i-- > 0;) {
        if (t[i] == c) ++depth;
        else if (t[i] == open && --depth == 0) return i;
    }
    return std::string::npos;
}

std::string expr_before(const std::string& t, std::size_t end) {
    std::size_t i = end;
    while (i > 0 && (t[i - 1] == ' ' || t[i - 1] == '\t')) --i;
    const std::size_t stop = i;
    while (i > 0) {
        const char c = t[i - 1];
        if (c == ')' || c == ']') {
            const std::size_t open = match_back(t, i - 1);
            if (open == std::string::npos) break;
            i = open;
            continue;
        }
        if (c == '!' && i > 1 && ident_char(t[i - 2])) {
            --i;
            continue;
        }
        if (ident_char(c)) {
            while (i > 0 && ident_char(t[i - 1])) --i;
            continue;
        }
        if (c == '.') {
            --i;
            continue;
        }
        if (c == ':' && i > 1 && t[i - 2] == ':') {
            i -= 2;
            continue;
        }
        if (c == '"' || c == '\'') {
            if (i != stop) break;
            std::size_t j = i - 1;
            while (j > 0 && t[j - 1] != c) --j;
            if (j == 0) break;
            i = j - 1;
            continue;
        }
        break;
    }
    return t.substr(i, stop - i);
}

std::size_t last_separator(const std::string& expr, std::size_t& width) {
    int depth = 0;
    for (std::size_t i = expr.size(); i-- > 0;) {
        const char c = expr[i];
        if (c == ')' || c == ']') ++depth;
        else if (c == '(' || c == '[') --depth;
        else if (depth == 0 && c == '.') {
            width = 1;
            return i;
        } else if (depth == 0 && c == ':' && i > 0 && expr[i - 1] == ':') {
            width = 2;
            return i - 1;
        }
    }
    return std::string::npos;
}

std::string string_owner(const std::string& language) {
    if (language == "python") return "python:str";
    if (language == "lua") return "lua:string";
    if (language == "rust") return "rust:String";
    if (language == "java") return "java:String";
    if (language == "csharp") return "csharp:string";
    return std::string();
}

std::string type_owner(const std::string& language, std::string type) {
    type = trim(type);
    while (!type.empty() && (type[0] == '&' || type[0] == '*')) type.erase(0, 1);
    if (starts_with(type, "mut ")) type = trim(type.substr(4));
    const std::size_t lt = type.find_first_of("<[(");
    if (lt != std::string::npos) type = type.substr(0, lt);
    const std::size_t sep = type.rfind("::");
    if (sep != std::string::npos) type = type.substr(sep + 2);
    const std::size_t dot = type.rfind('.');
    if (dot != std::string::npos && language != "python") type = type.substr(dot + 1);
    if (language == "python") {
        if (type == "str" || type == "list" || type == "dict") return "python:" + type;
        return std::string();
    }
    if (language == "rust") {
        if (type == "str") return "rust:String";
        for (const char* t : { "Vec", "String", "Option", "Result", "HashMap" }) {
            if (type == t) return "rust:" + type;
        }
        if (type == "VecDeque") return "rust:Vec";
        return std::string();
    }
    if (language == "java") {
        if (type == "String") return "java:String";
        if (type == "List" || type == "ArrayList" || type == "LinkedList") return "java:List";
        if (type == "Map" || type == "HashMap" || type == "TreeMap" || type == "LinkedHashMap") return "java:Map";
        if (type == "Scanner") return "java:Scanner";
        if (type == "PrintStream") return "java:PrintStream";
        return std::string();
    }
    if (language == "csharp") {
        if (type == "string" || type == "String") return "csharp:string";
        if (type == "List") return "csharp:List";
        if (type == "Dictionary") return "csharp:Dictionary";
        return std::string();
    }
    return std::string();
}

std::string resolve(const std::string& language, const std::string& text, std::string expr, std::size_t before, int depth);

std::string rhs_owner(const std::string& language, const std::string& text, std::string rhs, std::size_t before, int depth) {
    rhs = trim(rhs);
    while (!rhs.empty() && (rhs.back() == ';' || rhs.back() == ',')) rhs.pop_back();
    rhs = trim(rhs);
    if (rhs.empty()) return std::string();
    if (starts_with(rhs, "await ")) rhs = trim(rhs.substr(6));
    const char c = rhs[0];
    if (c == '"' || c == '\'' || ((c == 'f' || c == 'r' || c == 'b') && rhs.size() > 1 && (rhs[1] == '"' || rhs[1] == '\''))) {
        if (language == "rust" && rhs.find(".to_string()") == std::string::npos && rhs.find(".to_owned()") == std::string::npos) return "rust:String";
        return string_owner(language);
    }
    if (language == "python") {
        if (c == '[') return "python:list";
        if (c == '{') return rhs.find(':') != std::string::npos || rhs == "{}" ? "python:dict" : std::string();
        if (starts_with(rhs, "open(")) return "python:file";
        if (starts_with(rhs, "str(") || starts_with(rhs, "input(")) return "python:str";
        if (starts_with(rhs, "list(") || starts_with(rhs, "sorted(")) return "python:list";
        if (starts_with(rhs, "dict(")) return "python:dict";
    }
    if (language == "rust" && (starts_with(rhs, "vec!") || starts_with(rhs, "Vec::"))) return "rust:Vec";
    if (language == "rust" && starts_with(rhs, "format!")) return "rust:String";
    if (language == "zig" && starts_with(rhs, "@import(\"std\")")) return "zig:std";
    if ((language == "java" || language == "csharp") && starts_with(rhs, "new ")) return type_owner(language, rhs.substr(4));
    std::size_t end = 0;
    int nest = 0;
    while (end < rhs.size()) {
        const char ch = rhs[end];
        if (ch == '(' || ch == '[' || ch == '{') ++nest;
        else if (ch == ')' || ch == ']' || ch == '}') --nest;
        else if (nest == 0 && (ch == ' ' || ch == '+' || ch == '?' || ch == '=')) break;
        ++end;
    }
    return resolve(language, text, rhs.substr(0, end), before, depth + 1);
}

std::string variable_owner(const std::string& language, const std::string& text, const std::string& name, std::size_t before, int depth) {
    if (depth > 6 || name.empty()) return std::string();
    const std::string head = text.substr(0, std::min(before, text.size()));
    std::string found;
    auto escape = [](const std::string& s) {
        std::string out;
        for (const char c : s) {
            if (std::string("\\^$.|?*+()[]{}").find(c) != std::string::npos) out += '\\';
            out += c;
        }
        return out;
    };
    const std::string n = escape(name);
    try {
        if (language == "python") {
            const std::regex re("(?:^|\\n)[ \\t]*" + n + "[ \\t]*(?::[ \\t]*([\\w.]+)[^=\\n]*)?=(?!=)([^\\n]*)");
            for (auto it = std::sregex_iterator(head.begin(), head.end(), re); it != std::sregex_iterator(); ++it) {
                const std::smatch& m = *it;
                std::string owner = m[1].matched ? type_owner(language, m[1].str()) : std::string();
                if (owner.empty()) owner = rhs_owner(language, text, m[2].str(), static_cast<std::size_t>(m.position(0)), depth);
                found = owner;
            }
            const std::regex with_re("with\\s+open\\([^\\n]*\\)\\s+as\\s+" + n + "\\b");
            if (found.empty() && std::regex_search(head, with_re)) found = "python:file";
        } else if (language == "lua") {
            const std::regex re("(?:local[ \\t]+)?\\b" + n + "[ \\t]*=(?!=)([^\\n]*)");
            for (auto it = std::sregex_iterator(head.begin(), head.end(), re); it != std::sregex_iterator(); ++it) found = rhs_owner(language, text, (*it)[1].str(), static_cast<std::size_t>(it->position(0)), depth);
        } else if (language == "rust") {
            const std::regex re("let[ \\t]+(?:mut[ \\t]+)?" + n + "\\b[ \\t]*(?::[ \\t]*([^=;\\n]+))?(?:=([^;]*))?");
            for (auto it = std::sregex_iterator(head.begin(), head.end(), re); it != std::sregex_iterator(); ++it) {
                const std::smatch& m = *it;
                std::string owner = m[1].matched ? type_owner(language, m[1].str()) : std::string();
                if (owner.empty() && m[2].matched) owner = rhs_owner(language, text, m[2].str(), static_cast<std::size_t>(m.position(0)), depth);
                found = owner;
            }
            if (found.empty()) {
                const std::regex param("\\b" + n + "[ \\t]*:[ \\t]*([&\\w:<> ]+)");
                for (auto it = std::sregex_iterator(head.begin(), head.end(), param); it != std::sregex_iterator(); ++it) found = type_owner(language, (*it)[1].str());
            }
        } else if (language == "zig") {
            const std::regex re("(?:const|var)[ \\t]+" + n + "\\b[^=\\n]*=([^;\\n]*)");
            for (auto it = std::sregex_iterator(head.begin(), head.end(), re); it != std::sregex_iterator(); ++it) found = rhs_owner(language, text, (*it)[1].str(), static_cast<std::size_t>(it->position(0)), depth);
        } else if (language == "java" || language == "csharp") {
            const std::regex re("\\b([A-Za-z_][\\w.]*)(?:[ \\t]*<[^;=()]*>)?(?:\\[\\])?[ \\t]+" + n + "[ \\t]*(=([^;]*)|[;,)])");
            for (auto it = std::sregex_iterator(head.begin(), head.end(), re); it != std::sregex_iterator(); ++it) {
                const std::smatch& m = *it;
                const std::string type = m[1].str();
                if (type == "return" || type == "new" || type == "else") continue;
                std::string owner = type == "var" ? std::string() : type_owner(language, type);
                if (owner.empty() && m[3].matched) owner = rhs_owner(language, text, m[3].str(), static_cast<std::size_t>(m.position(0)), depth);
                found = owner;
            }
        }
    } catch (const std::regex_error&) {
        return std::string();
    }
    return found;
}

std::string base_owner(const std::string& language, const std::string& text, const std::string& name, std::size_t before, int depth) {
    if (language == "python") {
        if (has_owner("python:" + name) && name != "str" && name != "list" && name != "dict" && name != "file") return "python:" + name;
        if (name == "str" || name == "list" || name == "dict") return "python:" + name;
    } else if (language == "lua") {
        if (has_owner("lua:" + name)) return "lua:" + name;
    } else if (language == "rust") {
        if (name == "std") return "rust:std";
        if (has_owner("rust:" + name) && std::isupper(static_cast<unsigned char>(name[0]))) return "rust:" + name;
        if (name == "fs" || name == "env" || name == "io") return "rust:" + name;
    } else if (language == "zig") {
        if (name == "std") return "zig:std";
    } else if (language == "java") {
        if (name == "System" || name == "Math" || name == "String") return "java:" + name;
    } else if (language == "csharp") {
        if (name == "Console" || name == "Math") return "csharp:" + name;
        if (name == "string" || name == "String") return "csharp:string";
    }
    return variable_owner(language, text, name, before, depth);
}

std::string call_result(const std::string& language, const std::string& owner, const std::string& method) {
    static const std::set<std::string> rust_iters = { "iter", "iter_mut", "into_iter", "map", "filter", "rev", "zip", "enumerate", "skip", "take", "chars", "split", "lines", "keys", "values", "filter_map", "flat_map", "peekable", "chain", "cloned", "copied" };
    if (language == "rust") {
        if (rust_iters.count(method)) return "rust:Iterator";
        if (method == "new" || method == "with_capacity" || method == "from" || method == "clone") return owner;
        if (method == "get" || method == "first" || method == "last" || method == "pop" || method == "find" || method == "position" || method == "max" || method == "min") return "rust:Option";
        if (method == "parse") return "rust:Result";
        if (method == "to_string" || method == "to_uppercase" || method == "to_lowercase" || method == "replace" || method == "trim" || method == "to_owned" || method == "read_to_string") return "rust:String";
        if (method == "args") return "rust:Iterator";
        return std::string();
    }
    if (language == "python") {
        if (owner == "python:str" && (method == "strip" || method == "lower" || method == "upper" || method == "replace" || method == "format" || method == "join" || method == "lstrip" || method == "rstrip" || method == "title")) return "python:str";
        if (owner == "python:str" && (method == "split" || method == "splitlines")) return "python:list";
        if (owner == "python:file" && (method == "read" || method == "readline")) return "python:str";
        if (owner == "python:file" && method == "readlines") return "python:list";
        if (owner == "python:list" && method == "copy") return "python:list";
        if (owner == "python:dict" && method == "copy") return "python:dict";
        return std::string();
    }
    if (language == "java") {
        if (owner == "java:String" && (method == "substring" || method == "trim" || method == "toUpperCase" || method == "toLowerCase" || method == "replace" || method == "strip" || method == "repeat")) return "java:String";
        if (owner == "java:String" && method == "format") return "java:String";
        if (owner == "java:Scanner" && method == "nextLine") return "java:String";
        return std::string();
    }
    if (language == "csharp") {
        if (owner == "csharp:string" && (method == "Trim" || method == "ToUpper" || method == "ToLower" || method == "Replace" || method == "Substring")) return "csharp:string";
        if (owner == "csharp:Console" && method == "ReadLine") return "csharp:string";
        if (owner == "csharp:List" && (method == "Where" || method == "Select" || method == "ToList" || method == "OrderBy")) return "csharp:List";
        return std::string();
    }
    if (language == "lua") {
        if (owner == "lua:string" && (method == "sub" || method == "upper" || method == "lower" || method == "rep" || method == "format" || method == "gsub")) return "lua:string";
        return std::string();
    }
    return std::string();
}

std::string member_of(const std::string& language, const std::string& owner, const std::string& name) {
    if (owner.empty()) return std::string();
    if (language == "java" && owner == "java:System" && (name == "out" || name == "err")) return "java:PrintStream";
    if (language == "rust" && owner == "rust:std" && has_owner("rust:" + name)) return "rust:" + name;
    if (language == "rust" && owner == "rust:std" && name == "collections") return "rust:collections";
    const std::string dotted = owner + "." + name;
    if (has_owner(dotted)) return dotted;
    if (language == "csharp" && owner == "csharp:Dictionary" && (name == "Keys" || name == "Values")) return std::string();
    return std::string();
}

std::string resolve(const std::string& language, const std::string& text, std::string expr, std::size_t before, int depth) {
    expr = trim(expr);
    if (expr.empty() || depth > 8) return std::string();
    if (expr[0] == '"' || expr[0] == '\'') return language == "rust" ? std::string("rust:String") : string_owner(language);
    std::size_t width = 0;
    const std::size_t sep = last_separator(expr, width);
    std::string last = sep == std::string::npos ? expr : expr.substr(sep + width);
    const std::string left = sep == std::string::npos ? std::string() : expr.substr(0, sep);
    std::string args;
    bool call = false;
    const std::size_t paren = last.find('(');
    if (paren != std::string::npos) {
        call = true;
        args = last.substr(paren);
        last = last.substr(0, paren);
    }
    if (!last.empty() && last.back() == '!') {
        if (language == "rust" && (last == "vec!")) return "rust:Vec";
        if (language == "rust" && last == "format!") return "rust:String";
        return std::string();
    }
    if (sep == std::string::npos) {
        if (call) {
            if (language == "python") return rhs_owner(language, text, expr, before, depth);
            if (language == "rust" && (last == "Some")) return "rust:Option";
            return std::string();
        }
        return base_owner(language, text, last, before, depth);
    }
    const std::string owner = resolve(language, text, left, before, depth + 1);
    if (call) return call_result(language, owner, last);
    return member_of(language, owner, last);
}

std::string asm_arch_owner(const AsmInfo& info) {
    switch (info.arch) {
    case AsmArch::Arm64:
    case AsmArch::Arm: return "asm:arm64";
    case AsmArch::RiscV64:
    case AsmArch::Mips: return "asm:riscv";
    default: return "asm:x86";
    }
}

std::string asm_dialect_owner(const AsmInfo& info) {
    switch (info.dialect) {
    case AsmDialect::Nasm:
    case AsmDialect::Fasm: return "asm:nasm";
    case AsmDialect::Masm: return "asm:masm";
    default: return "asm:gas";
    }
}

std::string pretty_language(const std::string& language) {
    if (language == "csharp") return "csharp";
    if (language == "asm") return "asm";
    return language;
}

} // namespace

bool is_more_language(const std::string& language) {
    return language == "rust" || language == "zig" || language == "python" || language == "lua" || language == "java" || language == "csharp" || language == "asm";
}

std::string more_language_prefix(const std::string& language) {
    return language;
}

bool more_inside_text(const std::string& language, const std::string& text, std::size_t line, std::size_t column) {
    const grove::CodeLanguage lang = grove::code_language_from_name(language);
    grove::CodeLineState state;
    std::size_t at = 0;
    for (std::size_t l = 0; l <= line && at <= text.size(); ++l) {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        const std::string row = text.substr(at, end - at);
        const std::vector<grove::CodeToken> tokens = grove::highlight_code_line(lang, row, state);
        if (l == line) {
            if (column == 0) return false;
            for (const grove::CodeToken& t : tokens) {
                if (column - 1 >= t.begin && column - 1 < t.end) {
                    if (t.kind == grove::CodeTokenKind::Comment) return true;
                    if (t.kind == grove::CodeTokenKind::String || t.kind == grove::CodeTokenKind::Escape) return column < t.end || (t.end > t.begin && row[t.end - 1] != '"' && row[t.end - 1] != '\'');
                }
            }
            return false;
        }
        at = end + 1;
    }
    return false;
}

std::string more_member_owner(const std::string& language, const std::string& text, std::size_t separator, const std::string& separator_text) {
    if (language == "lua" && separator_text == ":") {
        const std::string expr = expr_before(text, separator);
        const std::string owner = resolve(language, text, expr, separator, 0);
        return owner.empty() ? std::string("lua:string") : owner;
    }
    const std::string expr = expr_before(text, separator);
    return resolve(language, text, expr, separator, 0);
}

std::vector<std::string> more_code_owners(const std::string& language, const std::string& text, const std::string& path, bool line_start) {
    if (language == "python") return { "python-keyword", "python-builtin" };
    if (language == "rust") return { "rust-keyword", "rust-macro", "rust-type" };
    if (language == "zig") return { "zig-keyword", "zig-builtin" };
    if (language == "lua") return { "lua-keyword", "lua-builtin" };
    if (language == "java") return { "java-keyword", "java-type" };
    if (language == "csharp") return { "csharp-keyword", "csharp-type" };
    if (language == "asm") {
        const AsmInfo info = detect_assembly(text, path);
        if (line_start) return { asm_arch_owner(info), asm_dialect_owner(info) };
        return { asm_arch_owner(info) + "#register", asm_dialect_owner(info) };
    }
    return {};
}

std::optional<HoverResult> more_hover(const std::string& language, const std::string& text, const std::string& path, std::size_t line, std::size_t column) {
    const std::optional<std::string> found = text_line_at(text, line);
    if (!found) return std::nullopt;
    const std::string& l = *found;
    if (column > l.size()) return std::nullopt;
    auto word_char = [&](char c) { return ident_char(c) || (language == "asm" && (c == '.' || c == '%')) || (language == "zig" && c == '@') || (language == "rust" && c == '!'); };
    std::size_t b = column;
    std::size_t e = column;
    while (b > 0 && word_char(l[b - 1])) --b;
    while (e < l.size() && word_char(l[e])) ++e;
    if (language == "rust") {
        while (b < e && l[b] == '!') ++b;
        const bool macro = e > b && l[e - 1] == '!' && !(e < l.size() && l[e] == '=');
        if (!macro) {
            while (e > b && l[e - 1] == '!') --e;
        }
    }
    if (b == e) return std::nullopt;
    std::string word = l.substr(b, e - b);
    HoverResult h{ line, b, e - b, std::string() };
    const std::string code_lang = pretty_language(language);
    auto show = [&](const WebDoc& d, const std::string& from) {
        h.markdown = code_block(code_lang, d.signature.empty() ? d.name : d.signature) + d.text + "\n";
        if (!from.empty()) h.markdown += "---\nOn " + from + "\n";
        return h;
    };
    if (language == "asm") {
        const AsmInfo info = detect_assembly(text, path);
        std::string lowered = word;
        for (char& c : lowered) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        std::string bare = lowered;
        while (!bare.empty() && bare[0] == '%') bare.erase(0, 1);
        for (const std::string& owner : { asm_arch_owner(info), asm_dialect_owner(info), std::string("asm:gas"), std::string("asm:nasm") }) {
            for (const std::string& key : { lowered, bare, word }) {
                if (const WebDoc* d = find_web_doc(owner, key)) {
                    h.markdown = code_block("asm", d->signature.empty() ? d->name : d->signature) + d->text + "\n---\n" + assembly_summary(info) + "\n";
                    return h;
                }
            }
        }
        return std::nullopt;
    }
    if (more_inside_text(language, text, line, b + 1)) return std::nullopt;
    const std::size_t at = offset_of(text, line, b);
    std::string sep;
    if (b > 0 && l[b - 1] == '.') sep = ".";
    else if (b > 1 && l[b - 1] == ':' && l[b - 2] == ':') sep = "::";
    else if (b > 0 && l[b - 1] == ':' && language == "lua") sep = ":";
    if (!sep.empty()) {
        const std::string owner = more_member_owner(language, text, at - sep.size(), sep);
        if (!owner.empty()) {
            if (const WebDoc* d = find_web_doc(owner, word)) {
                const std::size_t colon = owner.find(':');
                return show(*d, colon == std::string::npos ? owner : owner.substr(colon + 1));
            }
        }
        return std::nullopt;
    }
    for (const std::string& owner : more_code_owners(language, text, path, false)) {
        if (const WebDoc* d = find_web_doc(owner, word)) return show(*d, std::string());
    }
    if (language == "python") {
        if (const WebDoc* d = find_web_doc("python-module", word)) return show(*d, std::string());
    }
    return std::nullopt;
}

} // namespace canopy
