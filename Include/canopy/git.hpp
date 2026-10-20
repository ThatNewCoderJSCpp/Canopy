#ifndef CANOPY_GIT_HPP
#define CANOPY_GIT_HPP

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace canopy {

struct CommandOutput {
    int code = -1;
    std::string text;
};

CommandOutput run_in_folder(const std::vector<std::string>& command, const std::string& folder, bool merge_errors);
CommandOutput run_shell_in_folder(const std::string& line, const std::string& folder);
std::string git_command_line(const std::vector<std::string>& command, const std::string& folder, bool merge_errors);
void set_git_askpass(const std::string& program);
std::string git_askpass();

class CommandBatch {
public:
    struct Step {
        std::string key;
        std::vector<std::string> command;
        std::string shell;
        std::function<bool(std::string&)> internal;
        std::string label;
        bool quiet = false;
        bool may_fail = false;
    };

    CommandBatch();
    ~CommandBatch();
    CommandBatch(const CommandBatch&) = delete;
    CommandBatch& operator=(const CommandBatch&) = delete;

    bool start(std::vector<Step> steps, const std::string& folder, bool stop_on_error, bool merge_errors);
    bool running() const;
    std::string take_output();
    std::optional<std::vector<CommandOutput>> take_results();
    std::vector<std::string> keys() const;

private:
    struct State;
    std::shared_ptr<State> m_state;
};

struct GitFile {
    std::string path;
    std::string old_path;
    char index = '.';
    char worktree = '.';
    bool untracked = false;
    bool conflicted = false;

    bool staged() const noexcept { return !untracked && !conflicted && index != '.'; }
    bool changed() const noexcept { return untracked || conflicted || worktree != '.'; }
};

struct GitStatus {
    bool repo = false;
    std::string head;
    std::string branch;
    std::string upstream;
    bool detached = false;
    bool initial = false;
    int ahead = 0;
    int behind = 0;
    std::vector<GitFile> files;
    std::vector<std::string> ignored;
};

struct GitCommit {
    std::string hash;
    std::string short_hash;
    std::vector<std::string> parents;
    std::string author;
    std::string email;
    long long time = 0;
    std::string refs;
    std::string subject;
    std::string path;
    std::string status;
};

struct GitBranch {
    std::string name;
    std::string upstream;
    bool current = false;
    bool remote = false;
    long long time = 0;
    std::string track;
};

struct GitTag {
    std::string name;
    std::string hash;
    long long time = 0;
    std::string subject;
};

struct DiffHunk {
    long long old_start = 0;
    long long old_count = 0;
    long long new_start = 0;
    long long new_count = 0;
};

struct DiffLine {
    enum class Kind {
        Context,
        Added,
        Removed,
        Hunk,
        File,
        Meta
    };

    Kind kind = Kind::Context;
    std::string text;
    long long old_line = 0;
    long long new_line = 0;
};

std::string git_log_format();
GitStatus parse_git_status(const std::string& porcelain_v2_z);
std::vector<GitCommit> parse_git_log(const std::string& text);
std::vector<GitBranch> parse_git_branches(const std::string& text);
std::vector<GitTag> parse_git_tags(const std::string& text);
std::string git_branch_format();
std::string git_tag_format();
std::size_t diff_hunk_count(const std::vector<DiffLine>& lines);
std::optional<DiffHunk> diff_hunk(const std::vector<DiffLine>& lines, std::size_t index);
std::string hunk_patch(const std::string& path, const std::vector<DiffLine>& lines, std::size_t index, const std::string& before, const std::string& after);
std::optional<std::string> revert_hunk(const std::vector<DiffLine>& lines, std::size_t index, const std::string& before, const std::string& after);
std::string repo_name_from_url(const std::string& url);
std::vector<std::string> parse_lines(const std::string& text);
std::vector<DiffLine> parse_unified_diff(const std::string& text);
std::vector<DiffLine> diff_texts(const std::string& before, const std::string& after, std::size_t context = 3);
std::string relative_time(long long unix_seconds, long long now_seconds);
std::string short_ref_names(const std::string& refs);
char git_status_letter(const GitFile& file);
std::string git_status_word(const GitFile& file);
std::string relative_to(const std::string& path, const std::string& root);

} // namespace canopy

#endif // CANOPY_GIT_HPP
