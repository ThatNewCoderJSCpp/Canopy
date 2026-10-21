#ifndef CANOPY_TASKS_HPP
#define CANOPY_TASKS_HPP

#include "git.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace canopy {

struct TaskAsk {
    std::string name;
    std::string prompt;
    std::string value;
};

struct TaskStep {
    enum class Kind {
        Run,
        Try,
        Shell,
        SetVersion
    };

    Kind kind = Kind::Run;
    std::string text;
};

enum class TaskHook {
    None,
    Before,
    After,
    Instead
};

struct Task {
    std::string id;
    std::string label;
    std::string description;
    std::vector<TaskAsk> asks;
    std::string confirm;
    std::vector<TaskStep> steps;
    std::string version_file;
    std::string cwd;
    TaskHook on_build = TaskHook::None;
    TaskHook on_run = TaskHook::None;
    bool keep_going = false;
    std::size_t line = 0;
    std::string file;
    bool personal = false;

    std::string title() const { return label.empty() ? id : label; }
};

struct TaskFile {
    std::string path;
    std::vector<Task> tasks;
    std::vector<std::string> problems;
    bool exists = false;
    std::string local_path;
    bool shared_exists = false;
    bool local_exists = false;

    const Task* find(const std::string& id) const;
    const Task* hooked_build(TaskHook hook) const;
    const Task* hooked_run(TaskHook hook) const;
};

struct TaskContext {
    std::string root;
    std::string file;
    std::string branch;
    std::map<std::string, std::string> values;
};

std::string tasks_path(const std::string& workspace_root);
std::string tasks_template();
std::string local_tasks_path(const std::string& workspace_root);
bool keep_local_tasks_out_of_git(const std::string& workspace_root);
TaskFile parse_tasks(const std::string& text, const std::string& path);
TaskFile load_tasks(const std::string& workspace_root);
TaskHook parse_task_hook(const std::string& text);
std::string task_hook_name(TaskHook hook);

std::vector<std::string> tokenize_command(const std::string& text);
std::string substitute(const std::string& text, const std::map<std::string, std::string>& values);
std::string substitute_quoted(const std::string& text, const std::map<std::string, std::string>& values);

std::optional<std::string> find_version(const std::string& text);
std::optional<std::string> replace_version(const std::string& text, const std::string& version);
std::string bump_version(const std::string& version, const std::string& part);
std::string task_version_file(const Task& task, const std::string& root);
std::optional<std::string> current_version(const Task& task, const std::string& root);

std::map<std::string, std::string> task_values(const Task& task, const TaskContext& context);
std::string task_folder(const Task& task, const std::string& root);
std::vector<CommandBatch::Step> task_steps(const Task& task, const TaskContext& context);
std::string task_summary(const Task& task);

} // namespace canopy

#endif // CANOPY_TASKS_HPP
