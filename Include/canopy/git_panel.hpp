#ifndef CANOPY_GIT_PANEL_HPP
#define CANOPY_GIT_PANEL_HPP

#include "git.hpp"
#include "history.hpp"
#include "tasks.hpp"

#include <grove/markup/events.hpp>
#include <grove/markup/node.hpp>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace canopy {

class GitPanel {
public:
    struct Hooks {
        std::function<void(const std::string&)> open_file;
        std::function<void(const std::string& key, const std::string& title, const std::string& subtitle, const std::string& file, const std::vector<DiffLine>& lines)> open_diff;
        std::function<void(const std::string& key, const std::string& title, const std::string& subtitle, const std::string& file, const std::vector<DiffLine>& lines, const std::string& mode)> open_change_diff;
        std::function<void(const std::string& key)> close_diff;
        std::function<void(const std::string& key, const std::string& title, const std::string& file, const std::string& text)> open_version;
        std::function<void(const std::string&)> output;
        std::function<void()> show_output;
        std::function<void(const std::string&)> show_panel;
        std::function<std::string()> active_file;
        std::function<std::optional<std::string>(const std::string&)> file_text;
        std::function<bool(const std::string&, const std::string&)> restore_file;
        std::function<void()> save_all;
        std::function<void()> files_changed;
        std::function<double()> refresh_seconds;
        std::function<std::vector<std::string>()> roots;
        std::function<void(const std::string&)> open_folder;
        std::function<std::string(const std::string&, const std::string&, const std::string&)> setting;
        std::function<void()> decorations_changed;
    };

    struct Version {
        bool git = false;
        long long time = 0;
        HistoryEntry entry;
        GitCommit commit;
    };

    GitPanel(grove::Document& doc, LocalHistory& history, Hooks hooks);

    void install();
    void set_root(const std::string& root);
    const std::string& root() const noexcept { return m_root; }
    const std::string& repo() const noexcept { return m_repo; }
    const std::vector<std::string>& repos() const noexcept { return m_repos; }
    void select_repo(const std::string& repo);
    void rescan_repos();
    const GitStatus& status() const noexcept { return m_status; }
    const TaskFile& tasks() const noexcept { return m_tasks; }
    const std::vector<Version>& versions() const noexcept { return m_versions; }
    std::string decoration(const std::string& path, bool directory) const;
    bool busy() const;

    void refresh();
    void poll(double now);
    void file_saved(const std::string& path);
    void active_changed(const std::string& path);
    void panel_shown(const std::string& name);
    void set_visible(bool git, bool history);
    void show_history(const std::string& path);

    void reload_tasks();
    std::string tasks_root() const;
    bool run_task(const std::string& id, std::function<void(bool)> done = {});
    void pick_task();
    void create_tasks_file();
    void open_tasks_file();

    void commit(bool push);
    void push();
    void pull();
    void fetch();
    void clone();
    void new_branch(const std::string& start = {});
    void new_tag(const std::string& commit = {});
    void diff_file(const std::string& relative, bool staged);
    void diff_action(const std::string& key, std::size_t hunk, const std::string& action);
    void show_commit(const std::string& hash);

private:
    struct Request {
        std::unique_ptr<CommandBatch> batch;
        std::function<void(std::vector<CommandOutput>&)> done;
    };

    struct TaskRun {
        Task task;
        TaskContext context;
        std::string folder;
        std::function<void(bool)> done;
        bool used_message = false;
    };

    struct DiffContext {
        std::string rel;
        std::string path;
        std::string mode;
        bool staged = false;
        std::string before;
        std::string after;
        std::string raw_before;
        std::string raw_after;
        std::vector<DiffLine> lines;
    };

    static std::vector<std::string> git(std::vector<std::string> args);
    static CommandBatch::Step step(std::vector<std::string> command, bool may_fail = false, const std::string& key = {});

    void listen(const std::string& id, const char* type, grove::Listener fn);
    bool request(std::vector<CommandBatch::Step> steps, std::function<void(std::vector<CommandOutput>&)> done);
    bool action(std::vector<CommandBatch::Step> steps, const std::string& label, std::function<void(bool)> done = {}, const std::string& folder = {});
    void finish_action(std::vector<CommandOutput>& results);
    void detect_git();
    void finish_refresh(std::vector<CommandOutput>& results);
    void refresh_others();
    void rebuild_decorations();
    void set_state(const std::string& text);

    void render();
    void render_bar();
    void render_changes();
    void render_tasks();
    void render_side();
    void render_log();
    void render_branches();
    void render_tags();
    void render_history();

    void changes_click(grove::Event& e);
    void side_click(grove::Event& e);
    void log_click(grove::Event& e);
    void log_menu(grove::Event& e);
    void log_menu_action(const std::string& action);
    void branch_action(const std::string& name, bool remote, const std::string& action);
    void tag_action(const std::string& name, const std::string& action);
    void history_click(grove::Event& e);
    void history_menu(grove::Event& e);
    void history_menu_action(const std::string& action);
    void more_menu(grove::Event& e);
    void more_action(const std::string& action);
    void branch_picked(const std::string& value);
    void repo_picked(const std::string& value);

    void stage(const std::string& relative, bool on, std::function<void(bool)> done = {});
    void discard(const std::string& relative, std::function<void(bool)> done = {});
    void discard_all();
    void ask_text(const std::string& title, const std::string& message, const std::string& value, std::function<void(const std::string&)> then, std::function<void()> cancel = {});
    void ask_choice(const std::string& title, const std::string& message, const std::vector<std::string>& options, std::function<void(const std::string&)> then);
    void confirm(const std::string& title, const std::string& message, std::function<void()> then, std::function<void()> cancel = {});
    void confirm_typed(const std::string& title, const std::string& message, const std::string& word, std::function<void()> then);
    bool protected_branch(const std::string& name) const;
    void ask_task_value(std::shared_ptr<TaskRun> run, std::size_t index);
    void start_task(std::shared_ptr<TaskRun> run);
    std::string message_text() const;
    void set_message(const std::string& text);
    std::vector<CommandBatch::Step> push_steps(bool& ok);
    std::string remote_name() const;
    std::string next_tag() const;
    void after_tag(const std::string& name);

    std::string absolute(const std::string& relative) const;
    std::string relative(const std::string& path) const;
    std::string current_text(const std::string& path) const;
    void version_text(const Version& v, std::function<void(std::optional<std::string>)> then);
    std::string version_label(const Version& v) const;
    void compare_versions(std::size_t index, bool previous);
    void load_versions();

    grove::Document& m_doc;
    LocalHistory& m_history;
    Hooks m_hooks;
    std::string m_root;
    std::string m_repo;
    std::vector<std::string> m_repos;
    bool m_git_checked = false;
    bool m_git_missing = false;
    GitStatus m_status;
    std::map<std::string, GitStatus> m_other_status;
    std::map<std::string, std::string> m_deco;
    std::set<std::string> m_ignored;
    std::set<std::string> m_repo_keys;
    std::string m_deco_sig;
    std::vector<GitCommit> m_log;
    std::vector<GitBranch> m_branches;
    std::vector<GitTag> m_tag_list;
    std::vector<std::string> m_tags;
    std::vector<std::string> m_remotes;
    std::size_t m_stashes = 0;
    TaskFile m_tasks;
    std::vector<Request> m_requests;
    bool m_refreshing = false;
    bool m_refresh_again = false;
    bool m_others_running = false;
    double m_next_refresh = 0.0;
    double m_next_rescan = 0.0;
    double m_now = 0.0;
    CommandBatch m_action;
    bool m_action_running = false;
    std::vector<bool> m_action_may_fail;
    std::string m_action_label;
    std::function<void(bool)> m_action_done;
    std::string m_panel;
    bool m_git_visible = false;
    bool m_history_visible = false;
    std::string m_side = "commits";
    std::string m_sig_repos;
    std::string m_sig_bar;
    std::string m_sig_changes;
    std::string m_sig_tasks;
    std::string m_sig_log;
    std::string m_sig_branches;
    std::string m_sig_tags;
    std::map<std::string, DiffContext> m_diffs;
    std::vector<std::pair<std::string, bool>> m_rediff;
    std::size_t m_patch_serial = 0;
    std::string m_history_path;
    std::vector<Version> m_versions;
    std::size_t m_history_serial = 0;
    bool m_history_stale = true;
    std::string m_menu_hash;
    std::size_t m_menu_version = 0;
    std::string m_state;
};

} // namespace canopy

#endif // CANOPY_GIT_PANEL_HPP
