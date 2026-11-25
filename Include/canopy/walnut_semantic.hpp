#ifndef CANOPY_WALNUT_SEMANTIC_HPP
#define CANOPY_WALNUT_SEMANTIC_HPP

#include "build.hpp"
#include "code_intel.hpp"

#include <grove/input/code_buffer.hpp>
#include <grove/input/completion.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace canopy {

struct WalnutHover {
    std::size_t line = 0;
    std::size_t column = 0;
    std::size_t length = 0;
    std::string line_text;
    std::string code;
    std::vector<std::string> types;
    std::vector<std::string> values;
    std::vector<std::string> results;
    std::vector<std::string> overloads;
    bool guess = false;
    std::string doc;
    std::string expansion;
};

struct WalnutLink {
    std::size_t line = 0;
    std::size_t column = 0;
    std::size_t length = 0;
    std::string line_text;
    CodeLocation definition;
    std::vector<CodeLocation> declarations;
};

struct SemanticResult {
    bool ok = false;
    bool semantic = false;
    std::string error;
    std::vector<grove::CodeSpan> spans;
    std::vector<grove::CodeProblem> problems;
    std::vector<CompilerMessage> messages;
    std::vector<WalnutHover> hovers;
    std::vector<WalnutLink> links;
};

grove::CodeTokenKind walnut_semantic_kind(const std::string& type, const std::vector<std::string>& modifiers);
std::size_t utf16_to_byte(const std::string& line, std::size_t units);
std::vector<grove::CodeSpan> walnut_unresolved_spans(const std::string& text, const std::vector<grove::CodeSpan>& covered);
SemanticResult parse_walnut_highlight(const std::string& output, const std::string& text, const std::string& file, bool mark_unresolved = true);
std::vector<std::string> walnut_highlight_command(const std::string& walnut, const std::string& file, const std::string& flags);
std::vector<std::string> walnut_complete_command(const std::string& walnut, const std::string& file, const std::string& flags, std::size_t line, std::size_t column);

struct WalnutCompletions {
    bool ok = false;
    std::size_t start = 0;
    std::string mode;
    std::vector<grove::CompletionItem> items;
};

WalnutCompletions parse_walnut_complete(const std::string& output);

struct ProfileLine {
    std::string file;
    std::size_t line = 0;
    double ns = 0;
    double hits = 0;
    double percent = 0;
    std::string function;
    std::string text;
};

struct ProfileFunction {
    std::string name;
    std::string file;
    std::size_t line = 0;
    std::size_t last_line = 0;
    double calls = 0;
    double total_ns = 0;
    double own_ns = 0;
    double percent = 0;
};

struct ProfileEdit {
    std::size_t line = 0;
    std::string before;
    std::string after;
};

struct ProfileMemoryLine {
    std::string file;
    std::size_t line = 0;
    double bytes = 0;
    double blocks = 0;
    double peak = 0;
    double held = 0;
    double percent = 0;
    std::string function;
    std::string text;
};

struct ProfileComplexity {
    std::string name;
    std::string file;
    std::size_t line = 0;
    std::string time;
    std::string memory;
    std::string reason;
    std::string measured_time;
    std::string measured_memory;
    std::string measured_note;
};

struct ProfileSuggestion {
    std::string file;
    std::size_t line = 0;
    std::string function;
    std::string kind;
    std::string message;
    std::string why;
    double percent = 0;
    std::string before;
    std::string after;
    std::vector<ProfileEdit> edits;
};

struct WalnutProfile {
    bool ok = false;
    int exit_code = 0;
    double wall_ns = 0;
    double total_ns = 0;
    std::vector<ProfileLine> lines;
    std::vector<ProfileFunction> functions;
    std::vector<ProfileSuggestion> suggestions;
    bool memory = false;
    double bytes = 0;
    double blocks = 0;
    double peak = 0;
    double held = 0;
    std::vector<ProfileMemoryLine> memory_lines;
    std::vector<ProfileComplexity> complexity;
};

WalnutProfile parse_walnut_profile(const std::string& json);
std::string profile_duration(double ns);
std::string profile_bytes(double n);
std::string apply_profile_suggestion(const std::string& text, const ProfileSuggestion& s, bool& applied);
std::vector<std::string> text_lines(const std::string& text);
std::string walnut_hover_markdown(const WalnutHover& hover, std::size_t page);
const WalnutHover* walnut_hover_at(const std::vector<WalnutHover>& hovers, const std::vector<std::string>& lines, std::size_t line, std::size_t column);
const WalnutLink* walnut_link_at(const std::vector<WalnutLink>& links, const std::vector<std::string>& lines, std::size_t line, std::size_t column);
std::string walnut_library_file(const std::string& walnut, const std::string& file);
std::string walnut_library_cache(const std::string& walnut);
std::string walnut_library_path(const std::string& walnut, const std::string& file);
bool is_walnut_library_copy(const std::string& path);
std::string walnut_library_name(const std::string& file);

class SemanticJob {
public:
    SemanticJob();
    ~SemanticJob();
    SemanticJob(const SemanticJob&) = delete;
    SemanticJob& operator=(const SemanticJob&) = delete;

    bool start(const std::string& path, std::vector<std::string> command, std::string text);
    bool start_with_errors(const std::string& path, std::vector<std::string> command, std::string text);
    bool running() const;

    struct Done {
        std::string path;
        std::string text;
        std::string output;
        int exit_code = 0;
    };

    std::optional<Done> take();

private:
    struct State;
    std::shared_ptr<State> m_state;
};

} // namespace canopy

#endif // CANOPY_WALNUT_SEMANTIC_HPP
