#include "canopy/git_panel.hpp"

#include "canopy/build.hpp"
#include "canopy/desktop.hpp"
#include "canopy/secure_store.hpp"
#include "canopy/workspace.hpp"

#include <grove/dom_tools.hpp>
#include <grove/input/input.hpp>
#include <grove/native.hpp>
#include <grove/windowing.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>

namespace fs = std::filesystem;

namespace canopy {

namespace {

grove::Node* add(grove::Document& doc, grove::Node& parent, const std::string& tag, const std::string& text, const std::string& classes = {}) {
    grove::Node* el = doc.create_element(tag);
    if (!classes.empty()) el->set_attribute("class", classes);
    if (!text.empty()) el->set_inner_text(text);
    return doc.append_child(parent, *el);
}

void clear(grove::Document& doc, grove::Node& node) {
    while (!node.children().empty()) {
        grove::Node* last = node.children().back().get();
        if (!doc.remove(*last)) {
            node.clear_children();
            break;
        }
    }
    doc.touch();
}

void set_text(grove::Document& doc, const std::string& id, const std::string& text) {
    grove::Node* el = doc.find_by_id(id);
    if (!el) return;
    const auto& kids = el->children();
    if (kids.size() == 1 && !kids.front()->is_element() && kids.front()->text() == text) return;
    if (kids.empty() && text.empty()) return;
    el->set_inner_text(text);
    doc.touch();
}

const grove::Node* with_attribute(const grove::Node* node, const char* name) {
    for (const grove::Node* n = node; n; n = n->parent()) {
        if (n->is_element() && n->has_attribute(name)) return n;
    }
    return nullptr;
}

std::string trim(const std::string& s) {
    const std::size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return std::string();
    const std::size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string format_date(long long seconds) {
    const std::time_t t = static_cast<std::time_t>(seconds);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &tm);
    return buffer;
}

std::string size_text(std::size_t bytes) {
    if (bytes < 1024) return std::to_string(bytes) + " B";
    if (bytes < 1024 * 1024) return std::to_string((bytes + 512) / 1024) + " KB";
    return std::to_string((bytes + 512 * 1024) / (1024 * 1024)) + " MB";
}

std::string unix_newlines(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') continue;
        out += text[i];
    }
    return out;
}

bool has_crlf(const std::string& text) {
    return text.find("\r\n") != std::string::npos;
}

bool binary_text(const std::string& text) {
    return text.find('\0') != std::string::npos;
}

std::string path_key(const std::string& path) {
    return fs::path(path).lexically_normal().generic_string();
}

std::vector<DiffLine> compare(const std::string& before, const std::string& after) {
    if (before.find('\0') != std::string::npos || after.find('\0') != std::string::npos) {
        std::vector<DiffLine> out;
        DiffLine d;
        d.kind = DiffLine::Kind::Meta;
        d.text = before == after ? "The binary files are the same." : "Binary files differ (" + size_text(before.size()) + " \xE2\x86\x92 " + size_text(after.size()) + ").";
        out.push_back(d);
        return out;
    }
    if (has_crlf(before) != has_crlf(after)) return diff_texts(unix_newlines(before), unix_newlines(after));
    return diff_texts(before, after);
}

std::string status_kind(const GitFile& f) {
    if (f.conflicted) return "conflict";
    if (f.untracked || f.index == 'A') return "added";
    if (f.worktree == 'D' || (f.worktree == '.' && f.index == 'D')) return "deleted";
    return "modified";
}

int kind_rank(const std::string& kind) {
    if (kind == "conflict") return 4;
    if (kind == "modified" || kind == "deleted") return 3;
    if (kind == "added") return 2;
    return 0;
}

std::string home_folder() {
#if defined(_WIN32)
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
    return home ? std::string(home) : std::string();
}

long long unix_now() {
    return static_cast<long long>(std::time(nullptr));
}

std::string file_name(const std::string& path) {
    const std::string name = fs::path(path).filename().string();
    return name.empty() ? path : name;
}

std::string canonical(const std::string& path) {
    if (path.empty() || path.rfind("canopy:", 0) == 0) return path;
    std::error_code ec;
    const fs::path p = fs::weakly_canonical(fs::absolute(path, ec), ec);
    return ec ? path : p.string();
}

std::string version_tag(const std::string& tag) {
    std::string t = tag;
    if (!t.empty() && (t[0] == 'v' || t[0] == 'V')) t.erase(0, 1);
    if (t.empty() || !std::isdigit(static_cast<unsigned char>(t[0]))) return std::string();
    return t;
}

} // namespace

GitPanel::GitPanel(grove::Document& doc, LocalHistory& history, Hooks hooks) : m_doc(doc), m_history(history), m_hooks(std::move(hooks)) {}

std::vector<std::string> GitPanel::git(std::vector<std::string> args) {
    std::vector<std::string> out{ "git", "-c", "core.quotepath=false", "-c", "color.ui=false" };
    out.insert(out.end(), args.begin(), args.end());
    return out;
}

CommandBatch::Step GitPanel::step(std::vector<std::string> command, bool may_fail, const std::string& key) {
    CommandBatch::Step s;
    s.key = key;
    s.command = std::move(command);
    s.may_fail = may_fail;
    s.quiet = !key.empty();
    if (!s.command.empty() && s.command.front() == "git" && s.command.size() > 5) {
        std::vector<std::string> shown{ "git" };
        shown.insert(shown.end(), s.command.begin() + 5, s.command.end());
        s.label = command_line(shown);
    }
    return s;
}

void GitPanel::listen(const std::string& id, const char* type, grove::Listener fn) {
    if (grove::Node* n = m_doc.find_by_id(id)) m_doc.events().add_listener(n, type, std::move(fn));
}

void GitPanel::install() {
    listen("git-branch", "change", [this](grove::Event& e) {
        if (e.target) branch_picked(grove::input_string(*e.target).value_or(""));
    });
    listen("git-repo", "change", [this](grove::Event& e) {
        if (e.target) repo_picked(grove::input_string(*e.target).value_or(""));
    });
    listen("git-side", "click", [this](grove::Event& e) { side_click(e); });
    listen("git-branches", "mouse_down", [this](grove::Event& e) {
        if (e.button != 2) return;
        const grove::Node* row = with_attribute(e.target, "data-branch");
        grove::Node* owner = m_doc.find_by_id("git-branches");
        if (!row || !owner) return;
        const std::string name = row->attribute_or("data-branch", "");
        const bool remote = row->attribute_or("data-remote", "0") == "1";
        m_menu_hash = (remote ? "R" : "L") + name;
        std::vector<grove::MenuItem> items;
        if (remote) {
            items.push_back(grove::menu_item("checkout", "Check Out"));
            items.push_back(grove::menu_item("merge", "Merge into " + (m_status.branch.empty() ? std::string("HEAD") : m_status.branch)));
            items.push_back(grove::menu_separator());
            items.push_back(grove::menu_item("delete-remote", "Delete from Remote\xE2\x80\xA6"));
        } else {
            const bool current = name == m_status.branch;
            items.push_back(grove::menu_item("switch", "Switch To", {}, current));
            items.push_back(grove::menu_item("merge", "Merge into " + (m_status.branch.empty() ? std::string("HEAD") : m_status.branch), {}, current));
            items.push_back(grove::menu_item("push", "Push"));
            items.push_back(grove::menu_item("rename", "Rename\xE2\x80\xA6"));
            items.push_back(grove::menu_separator());
            items.push_back(grove::menu_item("delete", "Delete\xE2\x80\xA6", {}, current));
        }
        grove::open_menu(m_doc, *owner, items, e.x, e.y);
        e.default_prevented = true;
    });
    listen("git-tags", "mouse_down", [this](grove::Event& e) {
        if (e.button != 2) return;
        const grove::Node* row = with_attribute(e.target, "data-tag");
        grove::Node* owner = m_doc.find_by_id("git-tags");
        if (!row || !owner) return;
        m_menu_hash = "T" + row->attribute_or("data-tag", "");
        std::vector<grove::MenuItem> items{
            grove::menu_item("show", "Show Changes"),
            grove::menu_item("checkout", "Check Out\xE2\x80\xA6"),
            grove::menu_item("push", "Push", {}, m_remotes.empty()),
            grove::menu_separator(),
            grove::menu_item("delete", "Delete Here\xE2\x80\xA6"),
            grove::menu_item("delete-remote", "Delete Here and on the Remote\xE2\x80\xA6", {}, m_remotes.empty()),
        };
        grove::open_menu(m_doc, *owner, items, e.x, e.y);
        e.default_prevented = true;
    });
    listen("git-tags", "menu", [this](grove::Event& e) {
        if (m_menu_hash.size() < 2 || m_menu_hash[0] != 'T') return;
        tag_action(m_menu_hash.substr(1), e.key);
    });
    listen("git-branches", "menu", [this](grove::Event& e) {
        if (m_menu_hash.size() < 2 || (m_menu_hash[0] != 'R' && m_menu_hash[0] != 'L')) return;
        branch_action(m_menu_hash.substr(1), m_menu_hash[0] == 'R', e.key);
    });
    listen("git-fetch", "click", [this](grove::Event&) { fetch(); });
    listen("git-pull", "click", [this](grove::Event&) { pull(); });
    listen("git-push", "click", [this](grove::Event&) { push(); });
    listen("git-more", "click", [this](grove::Event& e) { more_menu(e); });
    listen("git-more", "menu", [this](grove::Event& e) { more_action(e.key); });
    listen("git-commit", "click", [this](grove::Event&) { commit(false); });
    listen("git-commit-push", "click", [this](grove::Event&) { commit(true); });
    listen("git-message", "key_down", [this](grove::Event& e) {
        if (e.key != "Enter") return;
        e.default_prevented = true;
        commit((e.modifiers & (grove::ModifierControl | grove::ModifierMeta)) != 0);
    });
    listen("git-changes", "click", [this](grove::Event& e) { changes_click(e); });
    listen("git-tasks", "click", [this](grove::Event& e) {
        if (const grove::Node* run = with_attribute(e.target, "data-task")) {
            run_task(run->attribute_or("data-task", ""));
            return;
        }
        if (const grove::Node* make = with_attribute(e.target, "data-tasks-create")) {
            create_tasks_file(make->attribute_or("data-tasks-create", "") == "local");
            return;
        }
        if (const grove::Node* line = with_attribute(e.target, "data-task-line")) {
            const std::string file = line->attribute_or("data-task-file", m_tasks.path);
            if (m_tasks.exists && m_hooks.open_file) m_hooks.open_file(file.empty() ? m_tasks.path : file);
        }
    });
    listen("git-tasks-edit", "click", [this](grove::Event&) { open_tasks_file(); });
    listen("git-log", "mouse_down", [this](grove::Event& e) {
        if (e.button == 2) log_menu(e);
    });
    listen("git-log", "menu", [this](grove::Event& e) { log_menu_action(e.key); });
    listen("history-list", "click", [this](grove::Event& e) { history_click(e); });
    listen("history-list", "mouse_down", [this](grove::Event& e) {
        if (e.button == 2) history_menu(e);
    });
    listen("history-list", "menu", [this](grove::Event& e) { history_menu_action(e.key); });
    render();
}

void GitPanel::set_root(const std::string& root) {
    if (root == m_root && !m_repo.empty()) {
        rescan_repos();
        return;
    }
    m_root = root;
    m_repo.clear();
    m_repos.clear();
    m_other_status.clear();
    rescan_repos();
}

void GitPanel::rescan_repos() {
    detect_git();
    std::vector<std::string> found;
    std::vector<std::string> nested;
    auto add_repo = [&](const std::string& path, bool child) {
        if (path.empty()) return;
        const std::string norm = fs::path(path).lexically_normal().make_preferred().string();
        if (std::find(found.begin(), found.end(), norm) != found.end() || std::find(nested.begin(), nested.end(), norm) != nested.end()) return;
        (child ? nested : found).push_back(norm);
    };
    std::vector<std::string> roots = m_hooks.roots ? m_hooks.roots() : std::vector<std::string>{};
    if (roots.empty() && !m_root.empty()) roots.push_back(m_root);
    if (!m_git_missing) {
        for (const std::string& r : roots) {
            if (r.empty()) continue;
            const CommandOutput top = run_in_folder({ "git", "rev-parse", "--show-toplevel" }, r, false);
            if (top.code == 0) add_repo(trim(top.text), false);
            std::error_code ec;
            std::size_t looked = 0;
            for (fs::directory_iterator it(r, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end && looked < 400; it.increment(ec), ++looked) {
                std::error_code e2;
                if (!it->is_directory(e2)) continue;
                const std::string name = it->path().filename().string();
                if (name.empty() || name[0] == '.' || name == "node_modules") continue;
                if (fs::exists(it->path() / ".git", e2)) add_repo(it->path().string(), true);
            }
        }
    }
    std::sort(nested.begin(), nested.end());
    for (const std::string& n : nested) {
        if (std::find(found.begin(), found.end(), n) == found.end()) found.push_back(n);
    }
    const bool changed = found != m_repos;
    m_repos = found;
    std::string keep = m_repo;
    if (std::find(m_repos.begin(), m_repos.end(), keep) == m_repos.end()) keep = m_repos.empty() ? std::string() : m_repos.front();
    if (keep != m_repo || changed) {
        if (keep != m_repo) select_repo(keep);
        else {
            m_sig_repos.clear();
            render_bar();
            refresh();
        }
    } else if (m_repo.empty()) {
        reload_tasks();
        render();
    }
}

void GitPanel::select_repo(const std::string& repo) {
    m_repo = repo;
    m_status = GitStatus{};
    m_log.clear();
    m_branches.clear();
    m_tags.clear();
    m_tag_list.clear();
    m_remotes.clear();
    m_stashes = 0;
    m_sig_repos.clear();
    m_sig_bar.clear();
    m_sig_changes.clear();
    m_sig_tasks.clear();
    m_sig_log.clear();
    m_sig_branches.clear();
    m_sig_tags.clear();
    reload_tasks();
    m_history_stale = true;
    render();
    refresh();
}

void GitPanel::repo_picked(const std::string& value) {
    if (value == "__clone__") {
        m_sig_repos.clear();
        m_doc.events().set_timeout(1.0, [this] {
            render_bar();
            clone();
        });
        return;
    }
    if (value.empty() || value == m_repo) return;
    select_repo(value);
}

bool GitPanel::busy() const { return m_action_running; }

void GitPanel::detect_git() {
    if (m_git_checked) return;
    m_git_checked = true;
    m_git_missing = run_in_folder({ "git", "--version" }, std::string(), false).code != 0;
}

std::string GitPanel::decoration(const std::string& path, bool directory) const {
    if (m_deco.empty() && m_ignored.empty()) return std::string();
    const std::string key = path_key(path);
    const auto it = m_deco.find(key);
    if (it != m_deco.end()) return it->second;
    if (m_ignored.empty()) return std::string();
    std::string at = key;
    while (!at.empty()) {
        if (m_ignored.count(at)) return "ignored";
        if (m_repo_keys.count(at)) break;
        const std::size_t slash = at.find_last_of('/');
        if (slash == std::string::npos || slash == 0) break;
        at.erase(slash);
    }
    (void)directory;
    return std::string();
}

void GitPanel::rebuild_decorations() {
    std::map<std::string, std::string> deco;
    std::set<std::string> ignored;
    std::set<std::string> repo_keys;
    auto mark = [&](const std::string& repo, const GitStatus& st) {
        const std::string root = path_key(repo);
        repo_keys.insert(root);
        for (const GitFile& f : st.files) {
            const std::string kind = status_kind(f);
            const std::string file = path_key(repo + "/" + f.path);
            deco[file] = kind;
            const std::string folder_kind = kind == "deleted" ? std::string("modified") : kind;
            std::string at = file;
            for (;;) {
                const std::size_t slash = at.find_last_of('/');
                if (slash == std::string::npos || slash == 0) break;
                at.erase(slash);
                if (at.size() < root.size()) break;
                std::string& d = deco[at];
                if (kind_rank(folder_kind) > kind_rank(d)) d = folder_kind;
                if (at == root) break;
            }
        }
        for (const std::string& i : st.ignored) ignored.insert(path_key(repo + "/" + i));
    };
    if (!m_repo.empty()) mark(m_repo, m_status);
    for (const auto& [repo, st] : m_other_status) {
        if (repo != m_repo) mark(repo, st);
    }
    std::string sig;
    for (const auto& [k, v] : deco) sig += k + "=" + v + "\x1e";
    for (const std::string& i : ignored) sig += i + "!\x1e";
    m_deco = std::move(deco);
    m_ignored = std::move(ignored);
    m_repo_keys = std::move(repo_keys);
    if (sig != m_deco_sig) {
        m_deco_sig = sig;
        if (m_hooks.decorations_changed) m_hooks.decorations_changed();
    }
}

void GitPanel::refresh_others() {
    if (m_others_running) return;
    std::vector<CommandBatch::Step> steps;
    std::vector<std::string> repos;
    for (const std::string& r : m_repos) {
        if (r == m_repo) continue;
        repos.push_back(r);
        steps.push_back(step({ "git", "-C", r, "-c", "core.quotepath=false", "status", "--porcelain=v2", "-z", "--untracked-files=all", "--ignored=matching" }, true, "other"));
    }
    for (auto it = m_other_status.begin(); it != m_other_status.end();) {
        if (std::find(repos.begin(), repos.end(), it->first) == repos.end()) it = m_other_status.erase(it);
        else ++it;
    }
    if (steps.empty()) return;
    m_others_running = true;
    if (!request(std::move(steps), [this, repos](std::vector<CommandOutput>& r) {
            m_others_running = false;
            for (std::size_t i = 0; i < r.size() && i < repos.size(); ++i) {
                if (r[i].code == 0) m_other_status[repos[i]] = parse_git_status(r[i].text);
                else m_other_status.erase(repos[i]);
            }
            rebuild_decorations();
        })) {
        m_others_running = false;
    }
}

bool GitPanel::request(std::vector<CommandBatch::Step> steps, std::function<void(std::vector<CommandOutput>&)> done) {
    Request r;
    r.batch = std::make_unique<CommandBatch>();
    r.done = std::move(done);
    for (CommandBatch::Step& s : steps) {
        s.quiet = true;
        s.may_fail = true;
    }
    if (!r.batch->start(std::move(steps), m_repo.empty() ? m_root : m_repo, false, false)) return false;
    m_requests.push_back(std::move(r));
    return true;
}

bool GitPanel::action(std::vector<CommandBatch::Step> steps, const std::string& label, std::function<void(bool)> done, const std::string& folder) {
    if (m_action_running) {
        set_state("Wait for " + m_action_label + " to finish");
        return false;
    }
    if (steps.empty()) return false;
    m_action_may_fail.clear();
    for (CommandBatch::Step& s : steps) {
        s.quiet = false;
        m_action_may_fail.push_back(s.may_fail);
    }
    const std::string where = !folder.empty() ? folder : m_repo.empty() ? m_root : m_repo;
    if (!m_action.start(std::move(steps), where, true, true)) return false;
    m_action_running = true;
    m_action_label = label;
    m_action_done = std::move(done);
    if (m_hooks.output) m_hooks.output("\n" + label + "\xE2\x80\xA6\n");
    set_state(label + "\xE2\x80\xA6");
    return true;
}

void GitPanel::finish_action(std::vector<CommandOutput>& results) {
    bool ok = results.size() == m_action_may_fail.size();
    for (std::size_t i = 0; i < results.size(); ++i) {
        if (results[i].code != 0 && !(i < m_action_may_fail.size() && m_action_may_fail[i])) ok = false;
    }
    m_action_running = false;
    if (m_hooks.output) m_hooks.output(m_action_label + (ok ? " finished.\n" : " failed.\n"));
    set_state(ok ? m_action_label + " finished" : m_action_label + " failed \xE2\x80\x94 see Output");
    if (!ok && m_hooks.show_output) m_hooks.show_output();
    bool auth_failed = false;
    for (const CommandOutput& r : results) auth_failed = auth_failed || (r.code != 0 && github_auth_failure(r.text));
    if (!ok && auth_failed && !(m_github && m_github->busy())) {
        const std::optional<GitHubLogin> saved = load_github_login();
        if (saved) forget_github_login();
        const std::string label = m_action_label;
        const std::string why = saved ? "GitHub didn't accept the saved sign-in for " + saved->user + " (the token may have expired or been deleted)." : "GitHub needs you to sign in before " + label + " can push or pull.";
        m_doc.events().set_timeout(1.0, [this, why, label] {
            confirm("Sign in to GitHub", why + " Sign in now? Then run " + label + " again.", [this] { sign_in_to_github(); });
        });
    }
    if (m_hooks.files_changed) m_hooks.files_changed();
    std::function<void(bool)> done = std::move(m_action_done);
    m_action_done = nullptr;
    if (m_repo.empty() || m_repos.size() > 1 || ok) m_next_rescan = 0.0;
    reload_tasks();
    refresh();
    if (done) done(ok);
}

void GitPanel::sign_in_to_github() {
    if (!m_github) m_github = std::make_unique<GitHubSignIn>();
    if (m_github->busy()) return;
    const std::string client = github_client_id(m_hooks.setting ? m_hooks.setting("git", "github-client-id", "") : std::string());
    if (!client.empty()) {
        m_github->start_device(client);
        set_state("Asking GitHub for a sign-in code\xE2\x80\xA6");
        return;
    }
    confirm("Sign in to GitHub", "Signing in through the browser needs Canopy's GitHub app, and this copy of Canopy doesn't have one set (Settings > Explorer, Terminal and Git > GitHub app client ID).\nYou can paste a token from GitHub instead. Make it expire, and Canopy keeps it in " + (secure_store_name().empty() ? std::string("memory until it closes") : secure_store_name()) + ".", [this] { paste_github_token(); });
}

void GitPanel::paste_github_token() {
    open_in_browser(github_token_page());
    grove::PopupOptions options;
    options.kind = "question";
    options.title = "Paste a GitHub token";
    options.message = "GitHub opened in your browser on a page that makes a token for Canopy, with repo and workflow already ticked. Pick an expiration, press Generate token, copy it and paste it here.";
    options.input = "string";
    grove::Node* popup = grove::create_popup(m_doc, nullptr, options);
    if (!popup) return;
    if (grove::Node* field = grove::query(*popup, ".popup-input")) field->set_attribute("masked", "true");
    m_doc.events().add_listener(popup, "ok", [this, popup](grove::Event&) {
        const std::string token = popup->attribute_or("value", "");
        m_doc.events().set_timeout(1.0, [this, token] {
            if (m_github->start_token(token)) set_state("Checking the token with GitHub\xE2\x80\xA6");
        });
    });
}

void GitPanel::sign_out_of_github() {
    const std::optional<GitHubLogin> login = load_github_login();
    if (!login) return;
    confirm("Sign out of GitHub", "Forget " + login->user + "'s GitHub sign-in in Canopy? Canopy asks again the next time Git needs it. A copy Git's own credential helper saved stays there.", [this, user = login->user] {
        forget_github_login();
        set_state("Signed out of GitHub (" + user + ")");
    });
}

void GitPanel::close_sign_in_popup() {
    if (!m_github_popup) return;
    m_github_closing = true;
    grove::answer_popup(m_doc, *m_github_popup, true);
    m_github_closing = false;
    m_github_popup = nullptr;
}

void GitPanel::poll_github_sign_in() {
    if (!m_github) return;
    std::optional<GitHubSignIn::Update> u = m_github->take();
    if (!u) return;
    if (u->stage == GitHubSignIn::Stage::Code) {
        grove::set_clipboard_text(m_doc, u->code.user_code);
        open_in_browser(u->code.verification_uri);
        grove::PopupOptions options;
        options.kind = "info";
        options.title = "Sign in to GitHub";
        options.message = "Your code is " + u->code.user_code + " (it's copied).\nGitHub opened in your browser at " + u->code.verification_uri + ": paste the code there and press Authorize.\nThis closes by itself once you're signed in. Close it to cancel.";
        m_github_popup = grove::create_popup(m_doc, nullptr, options);
        if (m_github_popup) {
            if (grove::Node* button = grove::query(*m_github_popup, ".popup-ok")) button->set_inner_text("Cancel sign-in");
            m_doc.events().add_listener(m_github_popup, "ok", [this](grove::Event&) {
                if (m_github_closing) return;
                m_github_popup = nullptr;
                m_github->cancel();
                set_state("GitHub sign-in cancelled");
            });
        }
        set_state("Waiting for you to enter " + u->code.user_code + " on GitHub\xE2\x80\xA6");
        return;
    }
    if (u->stage == GitHubSignIn::Stage::Failed) {
        close_sign_in_popup();
        set_state("GitHub sign-in failed");
        if (m_hooks.output) m_hooks.output("\nGitHub sign-in failed: " + u->result.error + "\n");
        if (m_hooks.show_output) m_hooks.show_output();
        return;
    }
    if (u->stage == GitHubSignIn::Stage::Done) {
        close_sign_in_popup();
        GitHubLogin login{ u->result.user, u->result.token };
        u->result.token.clear();
        if (!save_github_login(login)) {
            set_state("Signed in, but Canopy couldn't save it");
            if (m_hooks.output) m_hooks.output("\nCanopy couldn't write " + github_login_path() + "\n");
            return;
        }
        std::string helper;
        const bool shared = github_share_with_git(login, helper);
        const std::string storage = github_login_storage();
        std::string note = "\nSigned in to GitHub as " + login.user + ". Canopy's pushes and pulls use it from now on. It's kept in " + storage + ".";
        if (storage.rfind("memory", 0) == 0) note += " To stay signed in after Canopy closes, install secret-tool (the libsecret-tools package) and sign in again.";
        if (shared) note += " Git's credential helper (" + helper + ") has it too, so git in a terminal works without asking.";
        if (!u->result.scopes.empty() && u->result.scopes.find("workflow") == std::string::npos) note += " This sign-in can't change files in .github/workflows; make a token with workflow ticked if you need to push those.";
        if (m_hooks.output) m_hooks.output(note + "\n");
        set_state("Signed in to GitHub as " + login.user);
        return;
    }
}

void GitPanel::set_state(const std::string& text) {
    m_state = text;
    set_text(m_doc, "git-state", text);
}

void GitPanel::poll(double now) {
    m_now = now;
    poll_github_sign_in();
    std::vector<Request> finished;
    for (auto it = m_requests.begin(); it != m_requests.end();) {
        if (it->batch->running()) {
            ++it;
            continue;
        }
        finished.push_back(std::move(*it));
        it = m_requests.erase(it);
    }
    for (Request& r : finished) {
        std::optional<std::vector<CommandOutput>> results = r.batch->take_results();
        if (!results) continue;
        if (r.done) r.done(*results);
    }
    const std::string text = m_action.take_output();
    if (!text.empty() && m_hooks.output) m_hooks.output(text);
    if (m_action_running && !m_action.running()) {
        std::optional<std::vector<CommandOutput>> results = m_action.take_results();
        if (results) {
            const std::string rest = m_action.take_output();
            if (!rest.empty() && m_hooks.output) m_hooks.output(rest);
            finish_action(*results);
        }
    }
    if (!m_action_running && now >= m_next_rescan) {
        m_next_rescan = now + 20.0;
        if (!m_root.empty()) rescan_repos();
    }
    if (!m_refreshing && !m_action_running && now >= m_next_refresh) {
        double every = m_hooks.refresh_seconds ? m_hooks.refresh_seconds() : 4.0;
        if (every <= 0.0) every = 4.0;
        if (!m_git_visible) every *= 3.0;
        m_next_refresh = now + every;
        refresh();
    }
}

void GitPanel::refresh() {
    if (m_refreshing) {
        m_refresh_again = true;
        return;
    }
    if (m_repo.empty()) {
        render();
        return;
    }
    std::vector<CommandBatch::Step> steps;
    steps.push_back(step(git({ "status", "--porcelain=v2", "--branch", "-z", "--untracked-files=all", "--ignored=matching" }), true, "status"));
    steps.push_back(step(git({ "log", "-n", "150", "--format=" + git_log_format() }), true, "log"));
    steps.push_back(step(git({ "for-each-ref", "--format=" + git_branch_format(), "refs/heads", "refs/remotes" }), true, "branches"));
    steps.push_back(step(git({ "for-each-ref", "--sort=-creatordate", "--format=" + git_tag_format(), "refs/tags" }), true, "tags"));
    steps.push_back(step(git({ "remote" }), true, "remotes"));
    steps.push_back(step(git({ "stash", "list", "--format=%gd" }), true, "stash"));
    m_refreshing = true;
    m_refresh_again = false;
    if (!request(std::move(steps), [this](std::vector<CommandOutput>& r) { finish_refresh(r); })) m_refreshing = false;
}

void GitPanel::finish_refresh(std::vector<CommandOutput>& r) {
    m_refreshing = false;
    if (r.size() >= 6) {
        if (r[0].code == 0) {
            m_status = parse_git_status(r[0].text);
            if (!m_status.repo) m_status.repo = true;
        } else {
            m_status = GitStatus{};
            m_next_rescan = 0.0;
        }
        m_log = r[1].code == 0 ? parse_git_log(r[1].text) : std::vector<GitCommit>{};
        m_branches = r[2].code == 0 ? parse_git_branches(r[2].text) : std::vector<GitBranch>{};
        m_tag_list = r[3].code == 0 ? parse_git_tags(r[3].text) : std::vector<GitTag>{};
        m_tags.clear();
        for (const GitTag& t : m_tag_list) m_tags.push_back(t.name);
        m_remotes = r[4].code == 0 ? parse_lines(r[4].text) : std::vector<std::string>{};
        m_stashes = r[5].code == 0 ? parse_lines(r[5].text).size() : 0;
    }
    rebuild_decorations();
    refresh_others();
    render();
    std::vector<std::pair<std::string, bool>> rediff;
    rediff.swap(m_rediff);
    for (const auto& [rel, staged] : rediff) diff_file(rel, staged);
    if (m_refresh_again) refresh();
}

void GitPanel::file_saved(const std::string& path) {
    m_next_refresh = std::min(m_next_refresh, m_now + 0.4);
    if (!tasks_root().empty() && (fs::path(path).lexically_normal() == fs::path(tasks_path(tasks_root())).lexically_normal() || fs::path(path).lexically_normal() == fs::path(local_tasks_path(tasks_root())).lexically_normal())) {
        reload_tasks();
        render_tasks();
    }
    if (canonical(path) == m_history_path) {
        m_history_stale = true;
        if (m_history_visible) load_versions();
    }
}

void GitPanel::active_changed(const std::string& path) {
    if (canonical(path) == m_history_path) return;
    show_history(path);
}

void GitPanel::set_visible(bool git, bool history) {
    const bool git_appeared = git && !m_git_visible;
    const bool history_appeared = history && !m_history_visible;
    m_git_visible = git;
    m_history_visible = history;
    if (git_appeared) panel_shown("git");
    if (history_appeared) panel_shown("history");
}

void GitPanel::panel_shown(const std::string& name) {
    m_panel = name;
    if (name == "git") m_git_visible = true;
    if (name == "history") m_history_visible = true;
    if (name == "git") {
        reload_tasks();
        render_tasks();
        refresh();
    }
    if (name == "history") {
        const std::string active = m_hooks.active_file ? m_hooks.active_file() : std::string();
        if (canonical(active) != m_history_path) show_history(active);
        else if (m_history_stale) load_versions();
    }
}

void GitPanel::show_history(const std::string& path) {
    m_history_path = canonical(path);
    m_versions.clear();
    m_history_stale = true;
    ++m_history_serial;
    if (m_history_visible) load_versions();
    else render_history();
}

std::string GitPanel::tasks_root() const {
    std::error_code ec;
    if (!m_repo.empty() && fs::exists(tasks_path(m_repo), ec)) return m_repo;
    if (!m_root.empty() && fs::exists(tasks_path(m_root), ec)) return m_root;
    return m_repo.empty() ? m_root : m_repo;
}

void GitPanel::reload_tasks() {
    m_tasks = load_tasks(tasks_root());
}

std::string GitPanel::absolute(const std::string& rel) const {
    const std::string base = m_repo.empty() ? m_root : m_repo;
    return (fs::path(base) / fs::path(rel)).lexically_normal().make_preferred().string();
}

std::string GitPanel::relative(const std::string& path) const {
    if (m_repo.empty()) return path;
    const std::string rel = relative_to(path, m_repo);
    if (fs::path(rel).is_absolute()) return std::string();
    return rel;
}

std::string GitPanel::current_text(const std::string& path) const {
    if (m_hooks.file_text) {
        if (std::optional<std::string> open = m_hooks.file_text(path)) return *open;
    }
    return read_text_file(path).value_or(std::string());
}

std::string GitPanel::message_text() const {
    const grove::Node* box = m_doc.find_by_id("git-message");
    return box ? trim(grove::input_string(*box).value_or("")) : std::string();
}

void GitPanel::set_message(const std::string& text) {
    if (grove::Node* box = m_doc.find_by_id("git-message")) grove::set_input_string(m_doc, *box, text);
}

std::string GitPanel::remote_name() const {
    if (std::find(m_remotes.begin(), m_remotes.end(), "origin") != m_remotes.end()) return "origin";
    return m_remotes.empty() ? std::string() : m_remotes.front();
}

std::vector<CommandBatch::Step> GitPanel::push_steps(bool& ok) {
    ok = false;
    std::vector<CommandBatch::Step> steps;
    if (m_status.branch.empty()) return steps;
    if (!m_status.upstream.empty()) {
        steps.push_back(step(git({ "push" })));
        ok = true;
        return steps;
    }
    const std::string remote = remote_name();
    if (remote.empty()) return steps;
    steps.push_back(step(git({ "push", "-u", remote, m_status.branch })));
    ok = true;
    return steps;
}

void GitPanel::commit(bool push_too) {
    if (m_repo.empty()) {
        set_state("This folder isn't a Git repository");
        return;
    }
    if (m_hooks.save_all) m_hooks.save_all();
    const std::string message = message_text();
    bool staged = false;
    for (const GitFile& f : m_status.files) staged = staged || f.staged();
    if (m_status.files.empty()) {
        if (push_too) push();
        else set_state("Nothing to commit");
        return;
    }
    if (message.empty()) {
        set_state("Type a commit message first");
        if (grove::Node* box = m_doc.find_by_id("git-message"); box && m_doc.host().focus) m_doc.host().focus(box);
        return;
    }
    std::vector<CommandBatch::Step> steps;
    if (!staged) steps.push_back(step(git({ "add", "-A" })));
    steps.push_back(step(git({ "commit", "-m", message })));
    std::string label = staged ? "Commit staged changes" : "Commit";
    if (push_too) {
        bool ok = false;
        std::vector<CommandBatch::Step> more = push_steps(ok);
        if (ok) {
            steps.insert(steps.end(), more.begin(), more.end());
            label += " and push";
        } else {
            set_state(m_status.branch.empty() ? "Committing only: not on a branch, so there's nothing to push" : "Committing only: add a remote to push");
        }
    }
    const bool started = action(std::move(steps), label, [this, message](bool ok) {
        if (!ok && message_text().empty()) set_message(message);
    });
    if (started) set_message("");
}

void GitPanel::push() {
    if (m_repo.empty()) {
        set_state("This folder isn't a Git repository");
        return;
    }
    bool ok = false;
    std::vector<CommandBatch::Step> steps = push_steps(ok);
    if (ok) {
        action(std::move(steps), "Push " + m_status.branch);
        return;
    }
    if (m_status.branch.empty()) {
        set_state("Check out a branch to push");
        return;
    }
    const std::string branch = m_status.branch;
    ask_text("Add a remote", "This repository has no remote yet. Paste its URL (like https://github.com/you/project.git) to add it as origin and push " + branch + " there.", "", [this, branch](const std::string& url_in) {
        const std::string url = trim(url_in);
        if (url.empty()) return;
        action({ step(git({ "remote", "add", "origin", url })), step(git({ "push", "-u", "origin", branch })) }, "Publish " + branch);
    });
}

void GitPanel::pull() {
    if (m_repo.empty()) {
        set_state("This folder isn't a Git repository");
        return;
    }
    if (m_status.upstream.empty()) {
        set_state(m_status.branch.empty() ? "Check out a branch to pull" : m_status.branch + " has no upstream yet; push it first");
        return;
    }
    action({ step(git({ "pull" })) }, "Pull " + m_status.branch);
}

void GitPanel::fetch() {
    if (m_repo.empty()) {
        set_state("This folder isn't a Git repository");
        return;
    }
    if (m_remotes.empty()) {
        set_state("There are no remotes to fetch from");
        return;
    }
    action({ step(git({ "fetch", "--all", "--prune" })) }, "Fetch");
}

void GitPanel::branch_picked(const std::string& value) {
    if (value.empty() || value == m_status.branch) return;
    if (value.rfind("remote:", 0) == 0) {
        const std::string name = value.substr(7);
        action({ step(git({ "checkout", "--track", name })) }, "Check out " + name);
        return;
    }
    action({ step(git({ "checkout", value })) }, "Check out " + value);
}

void GitPanel::stage(const std::string& rel, bool on, std::function<void(bool)> done) {
    if (on) {
        action({ step(git({ "add", "--", rel })) }, "Stage " + file_name(rel), std::move(done));
        return;
    }
    if (m_status.initial) action({ step(git({ "rm", "--cached", "-q", "--", rel })) }, "Unstage " + file_name(rel), std::move(done));
    else action({ step(git({ "restore", "--staged", "--", rel })) }, "Unstage " + file_name(rel), std::move(done));
}

void GitPanel::discard(const std::string& rel, std::function<void(bool)> done) {
    const auto it = std::find_if(m_status.files.begin(), m_status.files.end(), [&](const GitFile& f) { return f.path == rel; });
    if (it == m_status.files.end()) return;
    const GitFile file = *it;
    const std::string path = absolute(rel);
    const std::string what = file.untracked ? "Delete " + rel + "? It isn't tracked by Git, so this removes the file." : "Discard your changes to " + rel + " and go back to the last commit?";
    confirm("Discard changes", what + " The current text is kept in local history.", [this, file, path, rel, done] {
        std::error_code ec;
        if (fs::is_regular_file(path, ec)) m_history.record(path, current_text(path), "Before discard");
        if (file.untracked) {
            CommandBatch::Step s;
            s.label = "delete " + rel;
            s.internal = [path](std::string& out) {
                std::error_code e;
                const bool ok = fs::remove(path, e);
                if (!ok) out += e ? e.message() : std::string("not found");
                return ok;
            };
            action({ s }, "Discard " + file_name(rel), done);
        } else if (file.index == 'A') {
            action({ step(git({ "rm", "-f", "-q", "--", rel })) }, "Discard " + file_name(rel), done);
        } else {
            action({ step(git({ "restore", "--staged", "--worktree", "--source=HEAD", "--", rel })) }, "Discard " + file_name(rel), done);
        }
        if (canonical(path) == m_history_path) m_history_stale = true;
    });
}

void GitPanel::discard_all() {
    std::size_t tracked = 0, untracked = 0;
    for (const GitFile& f : m_status.files) {
        if (!f.changed()) continue;
        (f.untracked ? untracked : tracked) += 1;
    }
    if (tracked + untracked == 0) {
        set_state("There are no unstaged changes");
        return;
    }
    std::string what = "Discard ";
    if (tracked) what += std::to_string(tracked) + (tracked == 1 ? " changed file" : " changed files");
    if (tracked && untracked) what += " and ";
    if (untracked) what += "delete " + std::to_string(untracked) + (untracked == 1 ? " new file" : " new files");
    confirm("Discard all changes", what + "? Staged changes are kept. The current text of each file is kept in local history.", [this] {
        std::vector<std::string> doomed;
        for (const GitFile& f : m_status.files) {
            if (!f.changed()) continue;
            const std::string path = absolute(f.path);
            std::error_code ec;
            if (fs::is_regular_file(path, ec)) m_history.record(path, current_text(path), "Before discard");
            if (f.untracked) doomed.push_back(path);
        }
        std::vector<CommandBatch::Step> steps;
        bool tracked_changes = std::any_of(m_status.files.begin(), m_status.files.end(), [](const GitFile& f) { return f.changed() && !f.untracked; });
        if (tracked_changes) steps.push_back(step(git({ "restore", "--worktree", "--", "." }), true));
        if (!doomed.empty()) {
            CommandBatch::Step s;
            s.label = "delete " + std::to_string(doomed.size()) + (doomed.size() == 1 ? " new file" : " new files");
            s.internal = [doomed](std::string& out) {
                bool ok = true;
                for (const std::string& p : doomed) {
                    std::error_code e;
                    if (!fs::remove(p, e)) {
                        out += p + ": " + (e ? e.message() : std::string("not found")) + "\n";
                        ok = false;
                    }
                }
                return ok;
            };
            steps.push_back(s);
        }
        action(std::move(steps), "Discard all changes");
    });
}

void GitPanel::diff_file(const std::string& rel, bool staged) {
    const auto it = std::find_if(m_status.files.begin(), m_status.files.end(), [&](const GitFile& f) { return f.path == rel; });
    const std::string old_path = it != m_status.files.end() && !it->old_path.empty() ? it->old_path : rel;
    const bool untracked = it != m_status.files.end() && it->untracked;
    const bool conflicted = it != m_status.files.end() && it->conflicted;
    const bool index_deleted = it != m_status.files.end() && it->index == 'D';
    const std::string path = absolute(rel);
    const std::string key = (staged ? "index:" : "work:") + path_key(path);
    std::vector<CommandBatch::Step> steps;
    steps.push_back(step(git({ "show", "HEAD:" + old_path }), true, "head"));
    steps.push_back(step(git({ "show", ":" + rel }), true, "index"));
    request(std::move(steps), [this, rel, staged, path, key, untracked, conflicted, index_deleted](std::vector<CommandOutput>& r) {
        const bool have_head = r.size() > 0 && r[0].code == 0;
        const bool have_index = r.size() > 1 && r[1].code == 0;
        const std::string head = have_head ? r[0].text : std::string();
        const std::string index = have_index ? r[1].text : std::string();
        DiffContext c;
        c.rel = rel;
        c.path = path;
        c.staged = staged;
        if (staged) {
            c.raw_before = head;
            c.raw_after = index_deleted ? std::string() : index;
            c.mode = "staged";
        } else {
            c.raw_before = untracked ? std::string() : have_index ? index : head;
            c.raw_after = current_text(path);
            c.mode = untracked ? "untracked" : conflicted ? "conflict" : "unstaged";
        }
        if (has_crlf(c.raw_before) != has_crlf(c.raw_after)) {
            c.before = unix_newlines(c.raw_before);
            c.after = unix_newlines(c.raw_after);
        } else {
            c.before = c.raw_before;
            c.after = c.raw_after;
        }
        c.lines = compare(c.raw_before, c.raw_after);
        if (binary_text(c.raw_before) || binary_text(c.raw_after)) c.mode = staged ? "staged-binary" : untracked ? "untracked" : "binary";
        if (c.lines.empty()) {
            m_diffs.erase(key);
            if (m_hooks.close_diff) m_hooks.close_diff(key);
            set_state("No " + std::string(staged ? "staged " : "") + "changes in " + file_name(rel));
            return;
        }
        const std::string title = file_name(rel) + (staged ? " (Staged)" : untracked ? " (New)" : " (Changes)");
        const std::string subtitle = staged ? "HEAD \xE2\x86\x94 staged" : untracked ? "new file" : "staged \xE2\x86\x94 working tree";
        const std::vector<DiffLine> lines = c.lines;
        const std::string mode = c.mode;
        m_diffs[key] = std::move(c);
        if (m_hooks.open_change_diff) m_hooks.open_change_diff(key, title, subtitle, path, lines, mode);
        else if (m_hooks.open_diff) m_hooks.open_diff(key, title, subtitle, path, lines);
    });
}

void GitPanel::diff_action(const std::string& key, std::size_t hunk, const std::string& act) {
    const auto it = m_diffs.find(key);
    if (it == m_diffs.end()) {
        set_state("That diff is out of date; open it again");
        return;
    }
    const DiffContext c = it->second;
    auto again = [this, c](bool) {
        m_rediff.emplace_back(c.rel, c.staged);
        refresh();
    };
    if (act == "stage-file") {
        stage(c.rel, true, again);
    } else if (act == "unstage-file") {
        stage(c.rel, false, again);
    } else if (act == "discard-file") {
        discard(c.rel, again);
    } else if (act == "stage-hunk" || act == "unstage-hunk") {
        const std::string patch = hunk_patch(c.rel, c.lines, hunk, c.before, c.after);
        if (patch.empty()) return;
        std::error_code ec;
        const fs::path file = fs::temp_directory_path(ec) / ("canopy-hunk-" + std::to_string(++m_patch_serial) + "-" + std::to_string(static_cast<long long>(std::time(nullptr))) + ".patch");
        if (!write_text_file(file.string(), patch)) {
            set_state("Couldn't write the patch file");
            return;
        }
        std::vector<std::string> args{ "apply", "--cached", "--recount", "--whitespace=nowarn" };
        if (act == "unstage-hunk") args.push_back("-R");
        args.push_back(file.string());
        const std::string path = file.string();
        action({ step(git(args)) }, act == "stage-hunk" ? "Stage hunk" : "Unstage hunk", [path, again](bool ok) {
            std::error_code e;
            fs::remove(path, e);
            again(ok);
        });
    } else if (act == "revert-hunk") {
        const std::optional<std::string> text = revert_hunk(c.lines, hunk, c.raw_before, c.raw_after);
        if (!text) {
            set_state("Couldn't revert that change; open the diff again");
            return;
        }
        m_history.record(c.path, current_text(c.path), "Before revert");
        const bool ok = m_hooks.restore_file && m_hooks.restore_file(c.path, *text);
        set_state(ok ? "Reverted a change in " + file_name(c.rel) : "Couldn't write " + file_name(c.rel));
        m_next_refresh = std::min(m_next_refresh, m_now + 0.2);
        if (ok) {
            m_rediff.emplace_back(c.rel, false);
            refresh();
        }
    }
}

void GitPanel::show_commit(const std::string& hash) {
    if (hash.empty()) return;
    std::vector<CommandBatch::Step> steps;
    steps.push_back(step(git({ "show", "-m", "--first-parent", "--stat", "--patch", "--date=format:%Y-%m-%d %H:%M", "--format=commit %H%nAuthor: %an <%ae>%nDate:   %ad%n%n%w(0,4,4)%B", hash }), true, "show"));
    request(std::move(steps), [this, hash](std::vector<CommandOutput>& r) {
        if (r.empty() || r[0].code != 0) {
            set_state("Couldn't read commit " + hash.substr(0, 7));
            return;
        }
        std::string subject;
        std::string meta;
        for (const GitCommit& c : m_log) {
            if (c.hash == hash) {
                subject = c.subject;
                meta = c.author + ", " + format_date(c.time);
            }
        }
        for (const Version& v : m_versions) {
            if (v.git && v.commit.hash == hash) {
                subject = v.commit.subject;
                meta = v.commit.author + ", " + format_date(v.commit.time);
            }
        }
        if (m_hooks.open_diff) m_hooks.open_diff("commit:" + hash, hash.substr(0, 7) + (subject.empty() ? std::string() : " " + subject), meta, std::string(), parse_unified_diff(r[0].text));
    });
}

void GitPanel::ask_text(const std::string& title, const std::string& message, const std::string& value, std::function<void(const std::string&)> then, std::function<void()> cancel) {
    grove::PopupOptions options;
    options.kind = "question";
    options.title = title;
    options.message = message;
    options.input = "string";
    options.value = value;
    grove::Node* popup = grove::create_popup(m_doc, nullptr, options);
    if (!popup) {
        if (cancel) cancel();
        return;
    }
    m_doc.events().add_listener(popup, "ok", [this, popup, then](grove::Event&) {
        const std::string value = popup->attribute_or("value", "");
        m_doc.events().set_timeout(1.0, [then, value] { then(value); });
    });
    if (cancel) m_doc.events().add_listener(popup, "cancel", [this, cancel](grove::Event&) { m_doc.events().set_timeout(1.0, cancel); });
}

void GitPanel::ask_choice(const std::string& title, const std::string& message, const std::vector<std::string>& choices, std::function<void(const std::string&)> then) {
    if (choices.empty()) return;
    grove::PopupOptions options;
    options.kind = "question";
    options.title = title;
    options.message = message;
    options.input = "choice";
    options.options = choices;
    options.value = choices.front();
    grove::Node* popup = grove::create_popup(m_doc, nullptr, options);
    if (!popup) return;
    m_doc.events().add_listener(popup, "ok", [this, popup, then](grove::Event&) {
        const std::string value = popup->attribute_or("value", "");
        m_doc.events().set_timeout(1.0, [then, value] { then(value); });
    });
}

void GitPanel::confirm(const std::string& title, const std::string& message, std::function<void()> then, std::function<void()> cancel) {
    grove::PopupOptions options;
    options.kind = "question";
    options.title = title;
    options.message = message;
    grove::Node* popup = grove::create_popup(m_doc, nullptr, options);
    if (!popup) {
        then();
        return;
    }
    m_doc.events().add_listener(popup, "ok", [this, then](grove::Event&) { m_doc.events().set_timeout(1.0, then); });
    if (cancel) m_doc.events().add_listener(popup, "cancel", [this, cancel](grove::Event&) { m_doc.events().set_timeout(1.0, cancel); });
}

bool GitPanel::run_task(const std::string& id, std::function<void(bool)> done) {
    reload_tasks();
    const Task* task = m_tasks.find(id);
    if (!task) {
        set_state("There's no task named " + id);
        if (done) done(false);
        return false;
    }
    if (m_action_running) {
        set_state("Wait for " + m_action_label + " to finish");
        if (done) done(false);
        return false;
    }
    auto run = std::make_shared<TaskRun>();
    run->task = *task;
    run->context.root = tasks_root();
    run->folder = task_folder(run->task, run->context.root);
    run->context.branch = m_status.branch;
    run->context.file = m_hooks.active_file ? m_hooks.active_file() : std::string();
    if (run->context.file.rfind("canopy:", 0) == 0) run->context.file.clear();
    run->done = std::move(done);
    ask_task_value(run, 0);
    return true;
}

void GitPanel::pick_task() {
    reload_tasks();
    if (m_tasks.tasks.empty()) {
        if (m_hooks.show_panel) m_hooks.show_panel("git");
        set_state(m_tasks.exists ? "tasks.bark has no tasks" : "There's no tasks file yet; press Create tasks.bark");
        return;
    }
    std::vector<std::string> titles;
    for (const Task& t : m_tasks.tasks) titles.push_back(t.title());
    const std::vector<Task> tasks = m_tasks.tasks;
    ask_choice("Run task", "Which task?", titles, [this, tasks](const std::string& title) {
        for (const Task& t : tasks) {
            if (t.title() == title) {
                run_task(t.id);
                return;
            }
        }
    });
}

void GitPanel::ask_task_value(std::shared_ptr<TaskRun> run, std::size_t index) {
    if (index >= run->task.asks.size()) {
        if (run->task.confirm.empty()) {
            start_task(run);
            return;
        }
        const std::string text = substitute(run->task.confirm, task_values(run->task, run->context));
        confirm(run->task.title(), text, [this, run] { start_task(run); }, [run] {
            if (run->done) run->done(false);
        });
        return;
    }
    const TaskAsk& ask = run->task.asks[index];
    const std::map<std::string, std::string> values = task_values(run->task, run->context);
    std::string value = substitute(ask.value, values);
    if (ask.name == "message") {
        run->used_message = true;
        if (value.empty()) value = message_text();
    }
    const std::string name = ask.name;
    ask_text(run->task.title(), ask.prompt, value, [this, run, index, name](const std::string& answer_in) {
        const std::string answer = trim(answer_in);
        if (answer.empty() && name == "message") {
            set_state("A commit message is needed");
            ask_task_value(run, index);
            return;
        }
        run->context.values[name] = answer;
        ask_task_value(run, index + 1);
    }, [run] {
        if (run->done) run->done(false);
    });
}

void GitPanel::start_task(std::shared_ptr<TaskRun> run) {
    if (m_hooks.save_all) m_hooks.save_all();
    std::vector<CommandBatch::Step> steps = task_steps(run->task, run->context);
    if (steps.empty()) {
        set_state(run->task.title() + " has no steps");
        if (run->done) run->done(true);
        return;
    }
    const bool started = action(std::move(steps), run->task.title(), [this, run](bool ok) {
        if (ok && run->used_message) set_message("");
        if (run->done) run->done(ok);
    }, run->folder);
    if (!started && run->done) run->done(false);
}

void GitPanel::create_tasks_file() {
    create_tasks_file(false);
}

void GitPanel::create_tasks_file(bool personal) {
    if (m_root.empty()) {
        set_state("Open a folder first");
        return;
    }
    const std::string base = m_repo.empty() ? m_root : m_repo;
    const std::string path = personal ? local_tasks_path(base) : tasks_path(base);
    std::error_code ec;
    if (personal && !keep_local_tasks_out_of_git(base)) set_state("Couldn't add tasks.local.bark to .canopy/.gitignore");
    if (!fs::exists(path, ec)) {
        fs::create_directories(fs::path(path).parent_path(), ec);
        if (!write_text_file(path, tasks_template())) {
            set_state("Couldn't write " + path);
            return;
        }
    }
    reload_tasks();
    m_sig_tasks.clear();
    render_tasks();
    if (m_hooks.open_file) m_hooks.open_file(path);
}

void GitPanel::open_local_tasks_file() {
    if (m_root.empty()) return;
    std::error_code ec;
    if (!fs::exists(local_tasks_path(tasks_root()), ec)) {
        create_tasks_file(true);
        return;
    }
    if (m_hooks.open_file) m_hooks.open_file(local_tasks_path(tasks_root()));
}

void GitPanel::open_tasks_file() {
    if (m_root.empty()) return;
    std::error_code ec;
    if (!fs::exists(tasks_path(tasks_root()), ec)) {
        if (fs::exists(local_tasks_path(tasks_root()), ec)) {
            open_local_tasks_file();
            return;
        }
        create_tasks_file();
        return;
    }
    if (m_hooks.open_file) m_hooks.open_file(tasks_path(tasks_root()));
}

void GitPanel::render() {
    render_bar();
    render_changes();
    render_tasks();
    render_side();
    if (m_versions.empty()) render_history();
}

void GitPanel::render_bar() {
    if (grove::Node* pick = m_doc.find_by_id("git-repo")) {
        std::vector<grove::SelectOption> repos;
        const std::vector<std::string> roots = m_hooks.roots ? m_hooks.roots() : std::vector<std::string>{ m_root };
        for (const std::string& r : m_repos) {
            std::string label = file_name(r);
            for (const std::string& root : roots) {
                const std::string rel = relative_to(r, root);
                if (!fs::path(rel).is_absolute() && rel != "." && !root.empty() && path_key(r) != path_key(root)) {
                    label = roots.size() > 1 ? file_name(root) + "/" + rel : rel;
                    break;
                }
            }
            repos.push_back(grove::SelectOption{ r, label, false });
        }
        if (repos.empty()) repos.push_back(grove::SelectOption{ "", m_root.empty() ? "No folder" : "No repository", false });
        if (!m_git_missing) repos.push_back(grove::SelectOption{ "__clone__", "Clone Repository\xE2\x80\xA6", false });
        std::string sig;
        for (const grove::SelectOption& o : repos) sig += o.value + "\x1f" + o.label + "\x1e";
        if (sig != m_sig_repos) {
            m_sig_repos = sig;
            grove::set_select_options(m_doc, *pick, repos);
        }
        if (pick->attribute_or("value", "") != m_repo) {
            pick->set_attribute("value", m_repo);
            m_doc.touch();
        }
        pick->set_attribute("title", m_repo.empty() ? std::string("The repository the Git tab shows") : m_repo);
    }
    std::vector<grove::SelectOption> options;
    std::string current;
    if (m_root.empty()) {
        options.push_back(grove::SelectOption{ "", "No folder", false });
    } else if (m_repo.empty()) {
        options.push_back(grove::SelectOption{ "", m_git_missing ? "Git not found" : "No repository", false });
    } else {
        current = m_status.branch;
        if (m_status.detached) options.push_back(grove::SelectOption{ "", "(detached at " + m_status.head.substr(0, 7) + ")", false });
        bool have_current = false;
        for (const GitBranch& b : m_branches) {
            if (b.remote) continue;
            options.push_back(grove::SelectOption{ b.name, b.name, false });
            have_current = have_current || b.name == current;
        }
        if (!current.empty() && !have_current) options.push_back(grove::SelectOption{ current, current, false });
        for (const GitBranch& b : m_branches) {
            if (!b.remote) continue;
            const std::size_t slash = b.name.find('/');
            const std::string local = slash == std::string::npos ? b.name : b.name.substr(slash + 1);
            const bool tracked = std::any_of(m_branches.begin(), m_branches.end(), [&](const GitBranch& o) { return !o.remote && o.name == local; });
            if (!tracked) options.push_back(grove::SelectOption{ "remote:" + b.name, b.name + " (remote)", false });
        }
    }
    std::string sig;
    for (const grove::SelectOption& o : options) sig += o.value + "\x1f" + o.label + "\x1e";
    if (grove::Node* pick = m_doc.find_by_id("git-branch")) {
        if (sig != m_sig_bar) {
            m_sig_bar = sig;
            grove::set_select_options(m_doc, *pick, options);
        }
        if (pick->attribute_or("value", "") != current) {
            pick->set_attribute("value", current);
            m_doc.touch();
        }
    }
    std::string sync;
    std::string compact;
    if (!m_repo.empty()) {
        if (m_status.detached) sync = "detached HEAD";
        else if (m_status.initial) sync = "no commits yet";
        else if (m_status.upstream.empty()) sync = m_remotes.empty() ? "no remote" : "not pushed yet";
        else if (m_status.ahead == 0 && m_status.behind == 0) sync = "up to date with " + m_status.upstream;
        else {
            if (m_status.ahead) compact += "\xE2\x86\x91" + std::to_string(m_status.ahead);
            if (m_status.behind) compact += (compact.empty() ? "" : " ") + std::string("\xE2\x86\x93") + std::to_string(m_status.behind);
            sync = compact + " " + m_status.upstream;
        }
    }
    set_text(m_doc, "git-sync", sync);
    set_text(m_doc, "git-state", m_state);
    std::string status;
    if (!m_repo.empty()) {
        status = m_status.branch.empty() ? m_status.head.substr(0, 7) : m_status.branch;
        if (!m_status.files.empty()) status += "*";
        if (!compact.empty()) status += " " + compact;
    }
    set_text(m_doc, "status-branch", status);
    if (grove::Node* item = m_doc.find_by_id("status-git")) {
        const bool hidden = item->has_class("hidden");
        if (status.empty() != hidden) {
            if (status.empty()) grove::add_class(m_doc, *item, "hidden");
            else grove::remove_class(m_doc, *item, "hidden");
        }
    }
    const std::size_t n = m_status.files.size();
    set_text(m_doc, "tab-git", n == 0 ? std::string("GIT") : "GIT " + std::to_string(n));
}

void GitPanel::render_changes() {
    grove::Node* list = m_doc.find_by_id("git-changes");
    if (!list) return;
    std::string sig = m_root + "\x1e" + m_repo + "\x1e" + (m_git_missing ? "!" : "");
    for (const GitFile& f : m_status.files) sig += f.path + "\x1f" + f.index + f.worktree + (f.untracked ? "?" : "") + (f.conflicted ? "!" : "") + "\x1e";
    if (sig == m_sig_changes) return;
    m_sig_changes = sig;
    clear(m_doc, *list);
    if (m_root.empty()) {
        add(m_doc, *list, "p", "Open a folder to use Git.", "muted");
        return;
    }
    if (m_git_missing) {
        add(m_doc, *list, "p", "Git isn't installed (or isn't on your PATH). Install it from git-scm.com and reopen Canopy.", "muted");
        return;
    }
    if (m_repo.empty()) {
        add(m_doc, *list, "p", "This folder isn't a Git repository yet.", "muted");
        grove::Node* buttons = add(m_doc, *list, "row", "", "git-start");
        grove::Node* init = add(m_doc, *buttons, "text", "Initialize Repository", "view-button git-init");
        init->set_attribute("data-git-init", "1");
        init->set_attribute("title", "Run git init in " + m_root);
        grove::Node* clone_button = add(m_doc, *buttons, "text", "Clone Repository\xE2\x80\xA6", "view-button git-init");
        clone_button->set_attribute("data-git-clone", "1");
        clone_button->set_attribute("title", "Download a repository from a URL into a folder");
        return;
    }
    std::vector<const GitFile*> staged;
    std::vector<const GitFile*> changed;
    for (const GitFile& f : m_status.files) {
        if (f.staged()) staged.push_back(&f);
        if (f.changed()) changed.push_back(&f);
    }
    if (staged.empty() && changed.empty()) {
        add(m_doc, *list, "p", "No changes. Everything is committed.", "muted");
        return;
    }
    auto section = [&](const std::string& title, const std::vector<const GitFile*>& files, bool is_staged) {
        if (files.empty()) return;
        grove::Node* head = add(m_doc, *list, "row", "", "git-section");
        add(m_doc, *head, "text", title + "  " + std::to_string(files.size()), "git-title");
        add(m_doc, *head, "div", "", "viewer-spacer");
        if (!is_staged) {
            grove::Node* discard_button = add(m_doc, *head, "text", "Discard All", "view-button danger");
            discard_button->set_attribute("data-git-all", "discard");
            discard_button->set_attribute("title", "Throw away every unstaged change (kept in local history)");
        }
        grove::Node* all = add(m_doc, *head, "text", is_staged ? "Unstage All" : "Stage All", "view-button");
        all->set_attribute("data-git-all", is_staged ? "unstage" : "stage");
        std::size_t shown = 0;
        for (const GitFile* f : files) {
            if (++shown > 400) {
                add(m_doc, *list, "p", std::to_string(files.size() - 400) + " more files aren't listed.", "muted");
                break;
            }
            grove::Node* row = add(m_doc, *list, "row", "", "git-file");
            row->set_attribute("data-git-path", f->path);
            row->set_attribute("data-git-staged", is_staged ? "1" : "0");
            row->set_attribute("title", f->old_path.empty() ? f->path : f->old_path + " \xE2\x86\x92 " + f->path);
            char letter = git_status_letter(*f);
            if (is_staged) letter = f->index == 'T' ? 'M' : f->index;
            std::string word = git_status_word(*f);
            if (is_staged) {
                GitFile copy = *f;
                copy.worktree = '.';
                word = git_status_word(copy);
            }
            add(m_doc, *row, "text", std::string(1, letter), "git-letter git-" + word);
            const fs::path p(f->path);
            add(m_doc, *row, "text", p.filename().string(), "git-name git-" + word);
            add(m_doc, *row, "text", p.parent_path().generic_string(), "git-dir");
            add(m_doc, *row, "div", "", "viewer-spacer");
            if (word != "deleted") add(m_doc, *row, "text", "Open", "view-button minor")->set_attribute("data-git-action", "open");
            add(m_doc, *row, "text", "History", "view-button minor")->set_attribute("data-git-action", "history");
            if (is_staged) {
                add(m_doc, *row, "text", "Unstage", "view-button")->set_attribute("data-git-action", "unstage");
            } else {
                add(m_doc, *row, "text", "Stage", "view-button")->set_attribute("data-git-action", "stage");
                add(m_doc, *row, "text", "Discard", "view-button danger")->set_attribute("data-git-action", "discard");
            }
        }
    };
    section("STAGED CHANGES", staged, true);
    section("CHANGES", changed, false);
}

void GitPanel::render_tasks() {
    grove::Node* list = m_doc.find_by_id("git-tasks");
    if (!list) return;
    std::string sig = m_root + "\x1e" + (m_tasks.exists ? "1" : "0") + (m_tasks.shared_exists ? "1" : "0") + (m_tasks.local_exists ? "1" : "0") + "\x1e";
    for (const Task& t : m_tasks.tasks) sig += t.id + "\x1f" + t.title() + "\x1f" + task_summary(t) + "\x1f" + task_hook_name(t.on_build) + task_hook_name(t.on_run) + (t.personal ? "p" : "s") + "\x1e";
    for (const std::string& p : m_tasks.problems) sig += p + "\x1e";
    if (sig == m_sig_tasks) return;
    m_sig_tasks = sig;
    clear(m_doc, *list);
    if (m_root.empty()) {
        add(m_doc, *list, "p", "Open a folder to use tasks.", "muted");
        return;
    }
    if (!m_tasks.exists) {
        add(m_doc, *list, "p", "A tasks file runs a list of Git, CMake and shell steps with one click, like commit, merge into main, tag and push.", "muted");
        grove::Node* buttons = add(m_doc, *list, "row", "", "git-task-buttons");
        grove::Node* make = add(m_doc, *buttons, "text", "Create tasks.bark", "view-button");
        make->set_attribute("data-tasks-create", "shared");
        make->set_attribute("title", "Write .canopy/tasks.bark with a release, commit and CMake task to start from. It's committed with the project, so everyone who opens it gets the same tasks");
        grove::Node* mine = add(m_doc, *buttons, "text", "Create my tasks", "view-button");
        mine->set_attribute("data-tasks-create", "local");
        mine->set_attribute("title", "Write .canopy/tasks.local.bark: tasks just for you on this computer. Canopy adds it to .canopy/.gitignore so it's never committed");
        return;
    }
    for (std::size_t i = 0; i < m_tasks.problems.size() && i < 3; ++i) add(m_doc, *list, "p", m_tasks.problems[i], "git-problem");
    if (m_tasks.tasks.empty()) add(m_doc, *list, "p", "tasks.bark has no tasks yet.", "muted");
    for (const Task& t : m_tasks.tasks) {
        grove::Node* row = add(m_doc, *list, "row", "", "git-task");
        grove::Node* text = add(m_doc, *row, "column", "", "git-task-text");
        text->set_attribute("data-task-line", std::to_string(t.line));
        text->set_attribute("data-task-file", t.file);
        add(m_doc, *text, "div", t.title() + (t.personal ? "  \xC2\xB7 yours" : ""), "git-task-name");
        std::string summary = task_summary(t);
        if (t.on_build != TaskHook::None) summary = "on build: " + task_hook_name(t.on_build) + (summary.empty() ? std::string() : " \xC2\xB7 " + summary);
        if (t.on_run != TaskHook::None) summary = "on run: " + task_hook_name(t.on_run) + (summary.empty() ? std::string() : " \xC2\xB7 " + summary);
        add(m_doc, *text, "div", summary, "git-task-summary");
        text->set_attribute("title", summary);
        grove::Node* run = add(m_doc, *row, "text", "Run", "view-button git-run");
        run->set_attribute("data-task", t.id);
        run->set_attribute("title", "Run " + t.title());
    }
    if (!m_tasks.local_exists) {
        grove::Node* mine = add(m_doc, *list, "text", "Add tasks just for you", "view-button git-task-more");
        mine->set_attribute("data-tasks-create", "local");
        mine->set_attribute("title", "Write .canopy/tasks.local.bark: tasks only on this computer, never committed. A task there with the same name as a shared one replaces it for you");
    } else if (!m_tasks.shared_exists) {
        grove::Node* shared = add(m_doc, *list, "text", "Add shared tasks.bark", "view-button git-task-more");
        shared->set_attribute("data-tasks-create", "shared");
        shared->set_attribute("title", "Write .canopy/tasks.bark: tasks committed with the project for everyone");
    }
}

void GitPanel::render_log() {
    grove::Node* list = m_doc.find_by_id("git-log");
    if (!list) return;
    std::string sig = m_repo + "\x1e";
    for (const GitCommit& c : m_log) sig += c.hash + c.refs + "\x1e";
    const long long now = unix_now();
    const std::string minute = std::to_string(now / 60);
    if (sig + minute == m_sig_log) return;
    m_sig_log = sig + minute;
    clear(m_doc, *list);
    if (m_repo.empty()) return;
    if (m_log.empty()) {
        add(m_doc, *list, "p", "No commits yet.", "muted");
        return;
    }
    for (const GitCommit& c : m_log) {
        grove::Node* row = add(m_doc, *list, "row", "", "git-commit");
        row->set_attribute("data-hash", c.hash);
        row->set_attribute("title", c.subject + "\n" + c.author + " <" + c.email + ">, " + format_date(c.time) + "\n" + c.hash);
        add(m_doc, *row, "text", c.short_hash, "git-hash");
        const std::string refs = short_ref_names(c.refs);
        if (!refs.empty()) add(m_doc, *row, "text", refs, "git-refs");
        add(m_doc, *row, "text", c.subject, "git-subject");
        add(m_doc, *row, "div", "", "viewer-spacer");
        add(m_doc, *row, "text", relative_time(c.time, now), "git-meta");
    }
}

void GitPanel::render_history() {
    grove::Node* list = m_doc.find_by_id("history-list");
    if (!list) return;
    clear(m_doc, *list);
    if (m_history_path.empty() || m_history_path.rfind("canopy:", 0) == 0) {
        set_text(m_doc, "history-file", "No file");
        set_text(m_doc, "history-note", "");
        add(m_doc, *list, "p", "Open a file to see its history: every version you saved in Canopy, and every Git commit that changed it.", "muted");
        return;
    }
    const std::string rel = relative(m_history_path);
    set_text(m_doc, "history-file", rel.empty() ? m_history_path : rel);
    std::size_t saves = 0, commits = 0;
    for (const Version& v : m_versions) (v.git ? commits : saves) += 1;
    set_text(m_doc, "history-note", std::to_string(saves) + (saves == 1 ? " save" : " saves") + (m_repo.empty() || rel.empty() ? std::string() : " \xC2\xB7 " + std::to_string(commits) + (commits == 1 ? " commit" : " commits")));
    if (m_versions.empty()) {
        add(m_doc, *list, "p", "No history yet. Each time you save, Canopy keeps a copy here" + std::string(m_repo.empty() ? "." : ", next to the Git commits that changed this file."), "muted");
        return;
    }
    const long long now = unix_now();
    for (std::size_t i = 0; i < m_versions.size(); ++i) {
        const Version& v = m_versions[i];
        grove::Node* row = add(m_doc, *list, "row", "", "hist-row");
        row->set_attribute("data-version", std::to_string(i));
        if (v.git) {
            add(m_doc, *row, "text", v.commit.short_hash, "hist-kind git");
            add(m_doc, *row, "text", v.commit.subject, "hist-label");
            row->set_attribute("title", v.commit.subject + "\n" + v.commit.author + ", " + format_date(v.time) + "\nClick to compare with the current text; right-click for more.");
        } else {
            add(m_doc, *row, "text", "SAVED", "hist-kind local");
            add(m_doc, *row, "text", v.entry.label, "hist-label");
            row->set_attribute("title", "Saved " + format_date(v.time) + "\nClick to compare with the current text; right-click for more.");
        }
        add(m_doc, *row, "div", "", "viewer-spacer");
        add(m_doc, *row, "text", relative_time(v.time, now) + "  \xC2\xB7  " + format_date(v.time), "git-meta");
        add(m_doc, *row, "text", v.git ? v.commit.author : size_text(v.entry.size), "git-meta");
        add(m_doc, *row, "text", "Compare", "view-button")->set_attribute("data-hist-action", "compare");
        add(m_doc, *row, "text", "Restore", "view-button")->set_attribute("data-hist-action", "restore");
    }
}

void GitPanel::load_versions() {
    m_history_stale = false;
    m_versions.clear();
    const std::string path = m_history_path;
    if (path.empty() || path.rfind("canopy:", 0) == 0) {
        render_history();
        return;
    }
    for (const HistoryEntry& e : m_history.entries(path)) {
        Version v;
        v.time = e.time_ms / 1000;
        v.entry = e;
        m_versions.push_back(std::move(v));
    }
    render_history();
    const std::string rel = relative(path);
    if (m_repo.empty() || rel.empty()) return;
    const std::size_t serial = ++m_history_serial;
    request({ step(git({ "log", "--follow", "-n", "200", "--format=" + git_log_format(), "--name-status", "--", rel }), true, "history") }, [this, serial, path](std::vector<CommandOutput>& r) {
        if (serial != m_history_serial || path != m_history_path) return;
        if (r.empty() || r[0].code != 0) return;
        for (GitCommit& c : parse_git_log(r[0].text)) {
            Version v;
            v.git = true;
            v.time = c.time;
            v.commit = std::move(c);
            m_versions.push_back(std::move(v));
        }
        std::stable_sort(m_versions.begin(), m_versions.end(), [](const Version& a, const Version& b) { return a.time > b.time; });
        render_history();
    });
}

std::string GitPanel::version_label(const Version& v) const {
    if (v.git) return v.commit.short_hash + " " + v.commit.subject;
    return "saved " + format_date(v.time);
}

void GitPanel::version_text(const Version& v, std::function<void(std::optional<std::string>)> then) {
    if (!v.git) {
        then(m_history.read(v.entry));
        return;
    }
    if (v.commit.status == "D") {
        then(std::string());
        return;
    }
    const std::string rel = v.commit.path.empty() ? relative(m_history_path) : v.commit.path;
    request({ step(git({ "show", v.commit.hash + ":" + rel }), true, "show") }, [then](std::vector<CommandOutput>& r) {
        if (r.empty() || r[0].code != 0) then(std::nullopt);
        else then(r[0].text);
    });
}

void GitPanel::compare_versions(std::size_t index, bool previous) {
    if (index >= m_versions.size()) return;
    const std::string path = m_history_path;
    const Version newer = m_versions[index];
    const std::string name = file_name(path);
    if (!previous) {
        version_text(newer, [this, newer, path, name](std::optional<std::string> text) {
            if (!text) {
                set_state("Couldn't read that version");
                return;
            }
            const std::vector<DiffLine> lines = compare(*text, current_text(path));
            if (lines.empty()) {
                set_state("That version is the same as the current text");
                return;
            }
            if (m_hooks.open_diff) m_hooks.open_diff("hist:" + path + ":" + std::to_string(newer.time) + (newer.git ? newer.commit.hash : std::string()), name + " (" + (newer.git ? newer.commit.short_hash : format_date(newer.time)) + " \xE2\x86\x94 now)", version_label(newer) + " \xE2\x86\x94 current text", path, lines);
        });
        return;
    }
    if (index + 1 >= m_versions.size()) {
        set_state("That's the oldest version");
        return;
    }
    const Version older = m_versions[index + 1];
    version_text(older, [this, older, newer, path, name](std::optional<std::string> before) {
        if (!before) {
            set_state("Couldn't read the older version");
            return;
        }
        version_text(newer, [this, older, newer, path, name, before](std::optional<std::string> after) {
            if (!after) {
                set_state("Couldn't read that version");
                return;
            }
            const std::vector<DiffLine> lines = compare(*before, *after);
            if (lines.empty()) {
                set_state("Those two versions are the same");
                return;
            }
            if (m_hooks.open_diff) m_hooks.open_diff("hist2:" + path + ":" + std::to_string(newer.time), name + " (" + (older.git ? older.commit.short_hash : format_date(older.time)) + " \xE2\x86\x94 " + (newer.git ? newer.commit.short_hash : format_date(newer.time)) + ")", version_label(older) + " \xE2\x86\x94 " + version_label(newer), path, lines);
        });
    });
}

void GitPanel::changes_click(grove::Event& e) {
    if (with_attribute(e.target, "data-git-init")) {
        action({ step(git({ "init" })) }, "Initialize repository", {}, m_root);
        return;
    }
    if (with_attribute(e.target, "data-git-clone")) {
        clone();
        return;
    }
    if (const grove::Node* all = with_attribute(e.target, "data-git-all")) {
        const std::string which = all->attribute_or("data-git-all", "");
        if (which == "discard") discard_all();
        else if (which == "stage") action({ step(git({ "add", "-A" })) }, "Stage all");
        else if (m_status.initial) action({ step(git({ "rm", "-r", "--cached", "-q", "." })) }, "Unstage all");
        else action({ step(git({ "restore", "--staged", "." })) }, "Unstage all");
        return;
    }
    const grove::Node* row = with_attribute(e.target, "data-git-path");
    if (!row) return;
    const std::string rel = row->attribute_or("data-git-path", "");
    const bool staged = row->attribute_or("data-git-staged", "0") == "1";
    const grove::Node* button = with_attribute(e.target, "data-git-action");
    const std::string act = button ? button->attribute_or("data-git-action", "") : std::string();
    if (act == "open") {
        if (m_hooks.open_file) m_hooks.open_file(absolute(rel));
    } else if (act == "stage") {
        stage(rel, true);
    } else if (act == "unstage") {
        stage(rel, false);
    } else if (act == "discard") {
        discard(rel);
    } else if (act == "history") {
        show_history(absolute(rel));
        if (m_hooks.show_panel) m_hooks.show_panel("history");
    } else {
        diff_file(rel, staged);
    }
}

void GitPanel::side_click(grove::Event& e) {
    if (const grove::Node* tab = with_attribute(e.target, "data-git-view")) {
        m_side = tab->attribute_or("data-git-view", "commits");
        render_side();
        return;
    }
    if (const grove::Node* add_button = with_attribute(e.target, "data-git-add")) {
        const std::string what = add_button->attribute_or("data-git-add", "");
        if (what == "branch") new_branch();
        else if (what == "tag") new_tag();
        return;
    }
    if (const grove::Node* row = with_attribute(e.target, "data-branch")) {
        const grove::Node* button = with_attribute(e.target, "data-branch-action");
        const std::string act = button ? button->attribute_or("data-branch-action", "") : std::string();
        const bool remote = row->attribute_or("data-remote", "0") == "1";
        const std::string name = row->attribute_or("data-branch", "");
        if (!act.empty()) branch_action(name, remote, act);
        else if (!remote && name != m_status.branch) branch_action(name, false, "switch");
        return;
    }
    if (const grove::Node* row = with_attribute(e.target, "data-tag")) {
        const grove::Node* button = with_attribute(e.target, "data-tag-action");
        const std::string act = button ? button->attribute_or("data-tag-action", "") : std::string("show");
        tag_action(row->attribute_or("data-tag", ""), act);
        return;
    }
    log_click(e);
}

bool GitPanel::protected_branch(const std::string& name_in) const {
    std::string name = name_in;
    const std::size_t slash = name.find('/');
    if (slash != std::string::npos && std::find(m_remotes.begin(), m_remotes.end(), name.substr(0, slash)) != m_remotes.end()) name = name.substr(slash + 1);
    const std::string list = m_hooks.setting ? m_hooks.setting("git", "protected-branches", "main master") : std::string("main master");
    for (const std::string& p : tokenize_command(list)) {
        if (p == name) return true;
    }
    return false;
}

void GitPanel::confirm_typed(const std::string& title, const std::string& message, const std::string& word, std::function<void()> then) {
    ask_text(title, message + "\n\nType " + word + " to confirm.", "", [this, word, then](const std::string& typed) {
        if (trim(typed) == word) then();
        else set_state("That didn't match " + word + ", so nothing was changed");
    });
}

std::string GitPanel::next_tag() const {
    for (const std::string& t : m_tags) {
        const std::string v = version_tag(t);
        if (!v.empty()) return std::string(t[0] == 'v' || t[0] == 'V' ? "v" : "") + bump_version(v, "patch");
    }
    Task probe;
    const std::optional<std::string> version = current_version(probe, m_repo.empty() ? m_root : m_repo);
    return "v" + version.value_or("0.1.0");
}

void GitPanel::after_tag(const std::string& name) {
    const std::string remote = remote_name();
    if (remote.empty()) return;
    confirm("Push tag", "Push " + name + " to " + remote + " too?", [this, name, remote] { action({ step(git({ "push", remote, name })) }, "Push tag " + name); });
}

void GitPanel::new_branch(const std::string& start) {
    if (m_repo.empty()) {
        set_state("Open or clone a repository first");
        return;
    }
    const std::string from = start.empty() ? (m_status.branch.empty() ? std::string("HEAD") : m_status.branch) : start.substr(0, 7);
    ask_text("New branch", "Name of the new branch. It starts at " + from + " and Canopy switches to it.", "", [this, start](const std::string& name_in) {
        std::string name = trim(name_in);
        for (char& c : name) {
            if (c == ' ') c = '-';
        }
        if (name.empty()) return;
        std::vector<std::string> args{ "checkout", "-b", name };
        if (!start.empty()) args.push_back(start);
        action({ step(git(args)) }, "Create branch " + name);
    });
}

void GitPanel::new_tag(const std::string& commit) {
    if (m_repo.empty() || m_status.initial) {
        set_state("Make a commit before tagging");
        return;
    }
    const std::string where = commit.empty() ? std::string("the current commit") : commit.substr(0, 7);
    ask_text("New tag", "Name of the tag for " + where + ". Adding a message makes it an annotated tag: write it after a | (v1.2.0 | First stable release).", next_tag(), [this, commit](const std::string& text_in) {
        const std::string text = trim(text_in);
        if (text.empty()) return;
        const std::size_t bar = text.find('|');
        const std::string name = trim(text.substr(0, bar));
        const std::string message = bar == std::string::npos ? std::string() : trim(text.substr(bar + 1));
        if (name.empty()) return;
        std::vector<std::string> args{ "tag" };
        if (!message.empty()) {
            args.push_back("-a");
            args.push_back(name);
            args.push_back("-m");
            args.push_back(message);
        } else {
            args.push_back(name);
        }
        if (!commit.empty()) args.push_back(commit);
        action({ step(git(args)) }, "Tag " + name, [this, name](bool ok) {
            if (ok) after_tag(name);
        });
    });
}

void GitPanel::branch_action(const std::string& name, bool remote, const std::string& act) {
    if (name.empty()) return;
    const std::string remote_of = remote ? name.substr(0, name.find('/')) : remote_name();
    const std::string short_name = remote && name.find('/') != std::string::npos ? name.substr(name.find('/') + 1) : name;
    if (act == "switch") {
        action({ step(git({ "checkout", name })) }, "Check out " + name);
    } else if (act == "checkout") {
        const bool local = std::any_of(m_branches.begin(), m_branches.end(), [&](const GitBranch& b) { return !b.remote && b.name == short_name; });
        if (local) action({ step(git({ "checkout", short_name })) }, "Check out " + short_name);
        else action({ step(git({ "checkout", "--track", name })) }, "Check out " + name);
    } else if (act == "merge") {
        const std::string into = m_status.branch.empty() ? std::string("HEAD") : m_status.branch;
        auto run = [this, name, into] { action({ step(git({ "merge", "--no-edit", name })) }, "Merge " + name + " into " + into); };
        if (protected_branch(into)) confirm("Merge into " + into, "Merge " + name + " into " + into + "? " + into + " is a protected branch.", run);
        else run();
    } else if (act == "push") {
        if (remote_of.empty()) {
            set_state("Add a remote first (Push on the bar asks for one)");
            return;
        }
        const auto it = std::find_if(m_branches.begin(), m_branches.end(), [&](const GitBranch& b) { return !b.remote && b.name == name; });
        if (it != m_branches.end() && !it->upstream.empty()) action({ step(git({ "push", remote_of, name })) }, "Push " + name);
        else action({ step(git({ "push", "-u", remote_of, name })) }, "Publish " + name);
    } else if (act == "rename") {
        ask_text("Rename branch", "New name for " + name + ".", name, [this, name](const std::string& to_in) {
            const std::string to = trim(to_in);
            if (to.empty() || to == name) return;
            action({ step(git({ "branch", "-m", name, to })) }, "Rename " + name + " to " + to);
        });
    } else if (act == "delete") {
        if (name == m_status.branch) {
            set_state("Switch to another branch before deleting " + name);
            return;
        }
        auto run = [this, name](bool force) { action({ step(git({ "branch", force ? "-D" : "-d", name })) }, "Delete branch " + name); };
        if (protected_branch(name)) {
            confirm_typed("Delete protected branch", name + " is a protected branch. Delete it from this computer? Commits that aren't merged anywhere else are lost.", name, [run] { run(true); });
        } else {
            confirm("Delete branch", "Delete " + name + " from this computer? Git refuses if it has commits that aren't merged.", [run] { run(false); });
        }
    } else if (act == "delete-remote") {
        if (remote_of.empty()) return;
        auto run = [this, remote_of, short_name, name] { action({ step(git({ "push", remote_of, "--delete", short_name })) }, "Delete " + name); };
        if (protected_branch(short_name)) confirm_typed("Delete protected branch", "Delete " + short_name + " from " + remote_of + "? Everyone using it loses it. It's a protected branch.", short_name, run);
        else confirm_typed("Delete remote branch", "Delete " + short_name + " from " + remote_of + " for everyone?", short_name, run);
    }
}

void GitPanel::tag_action(const std::string& name, const std::string& act) {
    if (name.empty()) return;
    const std::string remote = remote_name();
    if (act == "show") {
        const auto it = std::find_if(m_tag_list.begin(), m_tag_list.end(), [&](const GitTag& t) { return t.name == name; });
        show_commit(name);
        (void)it;
    } else if (act == "push") {
        if (remote.empty()) {
            set_state("Add a remote first");
            return;
        }
        action({ step(git({ "push", remote, name })) }, "Push tag " + name);
    } else if (act == "delete") {
        confirm("Delete tag", "Delete " + name + " from this computer? It stays on " + (remote.empty() ? std::string("the remote") : remote) + " if it was pushed.", [this, name] { action({ step(git({ "tag", "-d", name })) }, "Delete tag " + name); });
    } else if (act == "delete-remote") {
        if (remote.empty()) return;
        confirm_typed("Delete tag everywhere", "Delete " + name + " here and on " + remote + "? Anyone (or any release) that uses the tag loses it.", name, [this, name, remote] {
            action({ step(git({ "push", remote, "--delete", "refs/tags/" + name }), true), step(git({ "tag", "-d", name })) }, "Delete tag " + name + " everywhere");
        });
    } else if (act == "checkout") {
        confirm("Check out tag", "Check out " + name + "? You'll be on a detached HEAD until you switch back to a branch.", [this, name] { action({ step(git({ "checkout", name })) }, "Check out " + name); });
    }
}

void GitPanel::clone() {
    detect_git();
    if (m_git_missing) {
        set_state("Git isn't installed");
        return;
    }
    ask_text("Clone repository", "URL of the repository to clone (https://github.com/you/project.git or git@github.com:you/project.git).", "", [this](const std::string& url_in) {
        const std::string url = trim(url_in);
        if (url.empty()) return;
        const std::string base = m_root.empty() ? home_folder() : m_root;
        const std::string dest_default = (fs::path(base) / repo_name_from_url(url)).make_preferred().string();
        ask_text("Clone repository", "Folder to clone into. It must not exist yet, or be empty.", dest_default, [this, url](const std::string& dest_in) {
            const std::string dest = trim(dest_in);
            if (dest.empty()) return;
            const fs::path target = fs::absolute(fs::path(dest)).lexically_normal();
            std::error_code ec;
            if (fs::exists(target, ec) && !fs::is_empty(target, ec)) {
                set_state(target.string() + " already has files in it");
                return;
            }
            fs::create_directories(target.parent_path(), ec);
            const std::string where = target.string();
            action({ step(git({ "clone", url, where })) }, "Clone " + repo_name_from_url(url), [this, where](bool ok) {
                if (!ok) return;
                const std::vector<std::string> roots = m_hooks.roots ? m_hooks.roots() : std::vector<std::string>{ m_root };
                bool inside = false;
                for (const std::string& r : roots) {
                    if (r.empty()) continue;
                    const std::string rel = relative_to(where, r);
                    inside = inside || !fs::path(rel).is_absolute();
                }
                if (inside) {
                    rescan_repos();
                    const std::string norm = fs::path(where).lexically_normal().make_preferred().string();
                    if (std::find(m_repos.begin(), m_repos.end(), norm) != m_repos.end()) select_repo(norm);
                    if (m_hooks.files_changed) m_hooks.files_changed();
                    return;
                }
                confirm("Cloned", "Open " + where + " now?", [this, where] {
                    if (m_hooks.open_folder) m_hooks.open_folder(where);
                });
            }, fs::path(where).parent_path().string());
        });
    });
}

void GitPanel::render_side() {
    for (const char* view : { "commits", "branches", "tags" }) {
        const bool on = m_side == view;
        if (grove::Node* tab = m_doc.find_by_id(std::string("git-view-") + view)) {
            if (on != tab->has_class("active")) on ? (void)grove::add_class(m_doc, *tab, "active") : (void)grove::remove_class(m_doc, *tab, "active");
        }
        const std::string list_id = std::string(view) == "commits" ? std::string("git-log") : std::string("git-") + view;
        if (grove::Node* list = m_doc.find_by_id(list_id)) {
            if (on == list->has_class("hidden")) on ? (void)grove::remove_class(m_doc, *list, "hidden") : (void)grove::add_class(m_doc, *list, "hidden");
        }
    }
    if (grove::Node* add_button = m_doc.find_by_id("git-side-add")) {
        const bool show = !m_repo.empty() && m_side != "commits";
        if (show == add_button->has_class("hidden")) show ? (void)grove::remove_class(m_doc, *add_button, "hidden") : (void)grove::add_class(m_doc, *add_button, "hidden");
        add_button->set_attribute("data-git-add", m_side == "tags" ? "tag" : "branch");
        const std::string label = m_side == "tags" ? "+ New Tag" : "+ New Branch";
        if (add_button->inner_text() != label) add_button->set_inner_text(label);
    }
    set_text(m_doc, "git-log-note", m_repo.empty() ? std::string() : m_side == "commits" ? (m_status.branch.empty() ? std::string("HEAD") : m_status.branch) : std::string());
    render_log();
    render_branches();
    render_tags();
    m_doc.touch();
}

void GitPanel::render_branches() {
    grove::Node* list = m_doc.find_by_id("git-branches");
    if (!list) return;
    std::string sig = m_repo + "\x1e" + m_status.branch + "\x1e";
    for (const GitBranch& b : m_branches) sig += b.name + "\x1f" + b.upstream + "\x1f" + b.track + (b.current ? "*" : "") + "\x1e";
    if (sig == m_sig_branches) return;
    m_sig_branches = sig;
    clear(m_doc, *list);
    if (m_repo.empty()) return;
    auto row_for = [&](const GitBranch& b) {
        grove::Node* row = add(m_doc, *list, "row", "", b.current ? "git-branch-row current" : "git-branch-row");
        row->set_attribute("data-branch", b.name);
        row->set_attribute("data-remote", b.remote ? "1" : "0");
        add(m_doc, *row, "text", b.current ? "\xE2\x97\x8F" : " ", "git-branch-dot");
        add(m_doc, *row, "text", b.name, protected_branch(b.name) ? "git-branch-name protected" : "git-branch-name");
        row->set_attribute("title", b.remote ? b.name : b.name + (b.upstream.empty() ? std::string() : " \xE2\x86\x92 " + b.upstream) + "\nRight-click to rename, merge, push or delete");
        std::string meta;
        if (!b.remote) meta = b.upstream.empty() ? std::string("not pushed") : b.upstream;
        if (!b.track.empty()) meta += " " + b.track;
        if (!meta.empty()) add(m_doc, *row, "text", meta, "git-meta");
        add(m_doc, *row, "div", "", "viewer-spacer");
        auto button = [&](const std::string& label, const std::string& act, const std::string& extra = {}) {
            grove::Node* bnode = add(m_doc, *row, "text", label, extra.empty() ? std::string("view-button") : "view-button " + extra);
            bnode->set_attribute("data-branch-action", act);
        };
        if (b.remote) {
            button("Check Out", "checkout");
            button("Delete", "delete-remote", "danger");
            return;
        }
        if (!b.current) button("Switch", "switch");
        button(b.upstream.empty() ? "Publish" : "Push", "push");
        if (!b.current) button("Delete", "delete", "danger");
    };
    add(m_doc, *list, "p", "LOCAL", "git-subtitle");
    for (const GitBranch& b : m_branches) {
        if (!b.remote) row_for(b);
    }
    bool any_remote = false;
    for (const GitBranch& b : m_branches) any_remote = any_remote || b.remote;
    if (any_remote) {
        add(m_doc, *list, "p", "REMOTE", "git-subtitle");
        for (const GitBranch& b : m_branches) {
            if (b.remote) row_for(b);
        }
    }
}

void GitPanel::render_tags() {
    grove::Node* list = m_doc.find_by_id("git-tags");
    if (!list) return;
    std::string sig = m_repo + "\x1e";
    for (const GitTag& t : m_tag_list) sig += t.name + "\x1f" + t.hash + "\x1e";
    const long long now = unix_now();
    sig += std::to_string(now / 60);
    if (sig == m_sig_tags) return;
    m_sig_tags = sig;
    clear(m_doc, *list);
    if (m_repo.empty()) return;
    if (m_tag_list.empty()) {
        add(m_doc, *list, "p", "No tags yet. + New Tag marks the current commit, like v0.1.0.", "muted");
        return;
    }
    for (const GitTag& t : m_tag_list) {
        grove::Node* row = add(m_doc, *list, "row", "", "git-tag-row");
        row->set_attribute("data-tag", t.name);
        add(m_doc, *row, "text", t.name, "git-tag-name");
        add(m_doc, *row, "text", t.hash, "git-hash");
        add(m_doc, *row, "div", "", "viewer-spacer");
        add(m_doc, *row, "text", relative_time(t.time, now), "git-meta");
        row->set_attribute("title", (t.subject.empty() ? t.name : t.name + "\n" + t.subject) + "\nClick for its changes; right-click to check it out or delete it everywhere");
        for (const auto& [label, act] : std::vector<std::pair<std::string, std::string>>{ { "Push", "push" }, { "Delete", "delete" } }) {
            grove::Node* b = add(m_doc, *row, "text", label, act.rfind("delete", 0) == 0 ? "view-button danger" : "view-button");
            b->set_attribute("data-tag-action", act);
        }
    }
}

void GitPanel::log_click(grove::Event& e) {
    if (const grove::Node* row = with_attribute(e.target, "data-hash")) show_commit(row->attribute_or("data-hash", ""));
}

void GitPanel::log_menu(grove::Event& e) {
    const grove::Node* row = with_attribute(e.target, "data-hash");
    grove::Node* owner = m_doc.find_by_id("git-log");
    if (!row || !owner) return;
    m_menu_hash = row->attribute_or("data-hash", "");
    std::vector<grove::MenuItem> items{
        grove::menu_item("show", "Show Changes"),
        grove::menu_item("copy", "Copy Hash"),
        grove::menu_separator(),
        grove::menu_item("branch", "Create Branch Here\xE2\x80\xA6"),
        grove::menu_item("tag", "Create Tag Here\xE2\x80\xA6"),
        grove::menu_separator(),
        grove::menu_item("revert", "Revert Commit\xE2\x80\xA6"),
        grove::menu_item("checkout", "Check Out This Commit\xE2\x80\xA6"),
    };
    grove::open_menu(m_doc, *owner, items, e.x, e.y);
    e.default_prevented = true;
}

void GitPanel::log_menu_action(const std::string& act) {
    const std::string hash = m_menu_hash;
    if (hash.empty()) return;
    const std::string short_hash = hash.substr(0, 7);
    if (act == "show") {
        show_commit(hash);
    } else if (act == "copy") {
        grove::set_clipboard_text(m_doc, hash);
        set_state("Copied " + short_hash);
    } else if (act == "branch") {
        new_branch(hash);
    } else if (act == "tag") {
        new_tag(hash);
    } else if (act == "revert") {
        confirm("Revert commit", "Make a new commit that undoes " + short_hash + "?", [this, hash, short_hash] { action({ step(git({ "revert", "--no-edit", hash })) }, "Revert " + short_hash); });
    } else if (act == "checkout") {
        confirm("Check out commit", "Check out " + short_hash + "? You'll be on a detached HEAD until you switch back to a branch.", [this, hash, short_hash] { action({ step(git({ "checkout", hash })) }, "Check out " + short_hash); });
    }
}

void GitPanel::history_click(grove::Event& e) {
    const grove::Node* row = with_attribute(e.target, "data-version");
    if (!row) return;
    const std::size_t index = static_cast<std::size_t>(grove::parse_integer(row->attribute_or("data-version", "0")).value_or(0));
    const grove::Node* button = with_attribute(e.target, "data-hist-action");
    m_menu_version = index;
    if (button && button->attribute_or("data-hist-action", "") == "restore") history_menu_action("restore");
    else compare_versions(index, false);
}

void GitPanel::history_menu(grove::Event& e) {
    const grove::Node* row = with_attribute(e.target, "data-version");
    grove::Node* owner = m_doc.find_by_id("history-list");
    if (!row || !owner) return;
    m_menu_version = static_cast<std::size_t>(grove::parse_integer(row->attribute_or("data-version", "0")).value_or(0));
    if (m_menu_version >= m_versions.size()) return;
    const bool git_version = m_versions[m_menu_version].git;
    std::vector<grove::MenuItem> items{
        grove::menu_item("compare", "Compare with Current"),
        grove::menu_item("previous", "Compare with Previous", {}, m_menu_version + 1 >= m_versions.size()),
        grove::menu_item("open", "Open This Version"),
        grove::menu_separator(),
        grove::menu_item("restore", "Restore This Version\xE2\x80\xA6"),
    };
    if (git_version) {
        items.push_back(grove::menu_separator());
        items.push_back(grove::menu_item("commit", "Show Whole Commit"));
        items.push_back(grove::menu_item("copy", "Copy Hash"));
    } else {
        items.push_back(grove::menu_separator());
        items.push_back(grove::menu_item("delete", "Delete This Snapshot"));
    }
    grove::open_menu(m_doc, *owner, items, e.x, e.y);
    e.default_prevented = true;
}

void GitPanel::history_menu_action(const std::string& act) {
    if (m_menu_version >= m_versions.size()) return;
    const Version v = m_versions[m_menu_version];
    const std::string path = m_history_path;
    if (act == "compare") {
        compare_versions(m_menu_version, false);
    } else if (act == "previous") {
        compare_versions(m_menu_version, true);
    } else if (act == "open") {
        version_text(v, [this, v, path](std::optional<std::string> text) {
            if (!text) {
                set_state("Couldn't read that version");
                return;
            }
            const std::string tag = v.git ? v.commit.short_hash : format_date(v.time);
            if (m_hooks.open_version) m_hooks.open_version("version:" + path + ":" + std::to_string(v.time) + (v.git ? v.commit.hash : std::string()), file_name(path) + " (" + tag + ")", path, *text);
        });
    } else if (act == "restore") {
        confirm("Restore version", "Replace " + file_name(path) + " with the version from " + version_label(v) + "? The current text is kept in local history, so you can come back to it.", [this, v, path] {
            version_text(v, [this, path](std::optional<std::string> text) {
                if (!text) {
                    set_state("Couldn't read that version");
                    return;
                }
                m_history.record(path, current_text(path), "Before restore");
                const bool ok = m_hooks.restore_file && m_hooks.restore_file(path, *text);
                set_state(ok ? "Restored " + file_name(path) : "Couldn't restore " + file_name(path));
                load_versions();
                m_next_refresh = std::min(m_next_refresh, m_now + 0.3);
            });
        });
    } else if (act == "commit" && v.git) {
        show_commit(v.commit.hash);
    } else if (act == "copy" && v.git) {
        grove::set_clipboard_text(m_doc, v.commit.hash);
        set_state("Copied " + v.commit.short_hash);
    } else if (act == "delete" && !v.git) {
        m_history.remove(path, v.entry.time_ms);
        load_versions();
    }
}

void GitPanel::more_menu(grove::Event& e) {
    grove::Node* owner = m_doc.find_by_id("git-more");
    if (!owner) return;
    const bool repo = !m_repo.empty();
    const bool dirty = !m_status.files.empty();
    const bool commits = repo && !m_status.initial;
    std::vector<grove::MenuItem> items;
    if (!repo) {
        items.push_back(grove::menu_item("init", "Initialize Repository", {}, m_root.empty() || m_git_missing));
        items.push_back(grove::menu_item("clone", "Clone Repository\xE2\x80\xA6", {}, m_git_missing));
    } else {
        items.push_back(grove::menu_item("branch", "New Branch\xE2\x80\xA6"));
        items.push_back(grove::menu_item("branches", "Show Branches"));
        items.push_back(grove::menu_item("merge", "Merge Branch into " + (m_status.branch.empty() ? std::string("HEAD") : m_status.branch) + "\xE2\x80\xA6", {}, m_branches.size() < 2));
        items.push_back(grove::menu_item("delete-branch", "Delete Branch\xE2\x80\xA6", {}, m_branches.size() < 2));
        items.push_back(grove::menu_separator());
        items.push_back(grove::menu_item("tag", "Create Tag\xE2\x80\xA6", {}, !commits));
        items.push_back(grove::menu_item("push-tags", "Push Tags", {}, m_remotes.empty()));
        items.push_back(grove::menu_item("tags", "Show Tags"));
        items.push_back(grove::menu_separator());
        items.push_back(grove::menu_item("stash", "Stash Changes", {}, !dirty));
        items.push_back(grove::menu_item("pop", "Pop Stash" + (m_stashes ? " (" + std::to_string(m_stashes) + ")" : std::string()), {}, m_stashes == 0));
        items.push_back(grove::menu_separator());
        items.push_back(grove::menu_item("amend", "Amend Last Commit", {}, !commits));
        items.push_back(grove::menu_item("undo", "Undo Last Commit\xE2\x80\xA6", {}, !commits || m_log.size() < 2));
        items.push_back(grove::menu_item("discard-all", "Discard All Changes\xE2\x80\xA6", {}, !dirty));
    }
    items.push_back(grove::menu_separator());
    if (repo) items.push_back(grove::menu_item("clone", "Clone Repository\xE2\x80\xA6"));
    items.push_back(grove::menu_item("tasks", m_tasks.shared_exists ? "Edit Shared Tasks (tasks.bark)" : "Create Shared Tasks (tasks.bark)", {}, m_root.empty()));
    items.push_back(grove::menu_item("my-tasks", m_tasks.local_exists ? "Edit My Tasks (not shared)" : "Create My Tasks (not shared)", {}, m_root.empty()));
    if (const std::optional<GitHubLogin> login = load_github_login()) items.push_back(grove::menu_item("github-sign-out", "Sign Out of GitHub (" + login->user + ")"));
    else items.push_back(grove::menu_item("github-sign-in", "Sign in to GitHub\xE2\x80\xA6", {}, m_github && m_github->busy()));
    items.push_back(grove::menu_item("refresh", "Refresh"));
    grove::open_menu(m_doc, *owner, items, e.x, e.y);
    e.default_prevented = true;
}

void GitPanel::more_action(const std::string& act) {
    std::vector<std::string> locals;
    for (const GitBranch& b : m_branches) {
        if (!b.remote && b.name != m_status.branch) locals.push_back(b.name);
    }
    if (act == "init") {
        action({ step(git({ "init" })) }, "Initialize repository", {}, m_root);
    } else if (act == "github-sign-in") {
        sign_in_to_github();
    } else if (act == "github-sign-out") {
        sign_out_of_github();
    } else if (act == "branch") {
        new_branch();
    } else if (act == "clone") {
        clone();
    } else if (act == "branches" || act == "tags") {
        m_side = act;
        render_side();
    } else if (act == "merge") {
        ask_choice("Merge", "Merge which branch into " + m_status.branch + "?", locals, [this](const std::string& name) {
            if (!name.empty()) action({ step(git({ "merge", "--no-edit", name })) }, "Merge " + name);
        });
    } else if (act == "delete-branch") {
        ask_choice("Delete branch", "Delete which branch? Git refuses if it has commits that aren't merged anywhere.", locals, [this](const std::string& name) {
            if (!name.empty()) branch_action(name, false, "delete");
        });
    } else if (act == "tag") {
        new_tag();
    } else if (act == "push-tags") {
        action({ step(git({ "push", remote_name(), "--tags" })) }, "Push tags");
    } else if (act == "stash") {
        action({ step(git({ "stash", "push", "--include-untracked", "-m", "Stashed from Canopy" })) }, "Stash changes");
    } else if (act == "pop") {
        action({ step(git({ "stash", "pop" })) }, "Pop stash");
    } else if (act == "amend") {
        const std::string message = message_text();
        std::vector<CommandBatch::Step> steps{ step(git({ "add", "-A" })) };
        if (message.empty()) steps.push_back(step(git({ "commit", "--amend", "--no-edit" })));
        else steps.push_back(step(git({ "commit", "--amend", "-m", message })));
        action(std::move(steps), "Amend last commit", [this](bool ok) {
            if (ok) set_message("");
        });
    } else if (act == "undo") {
        const std::string subject = m_log.empty() ? std::string() : m_log.front().subject;
        auto run = [this] { action({ step(git({ "reset", "--soft", "HEAD~1" })) }, "Undo last commit"); };
        const std::string message = "Undo \"" + subject + "\"? Its changes stay in your files, staged, so you can commit them again.";
        if (protected_branch(m_status.branch) && !m_status.upstream.empty() && m_status.ahead == 0) confirm_typed("Undo a pushed commit", message + " It's already pushed to " + m_status.upstream + ", so the branches will disagree.", m_status.branch, run);
        else confirm("Undo last commit", message, run);
    } else if (act == "discard-all") {
        confirm("Discard all changes", "Throw away every uncommitted change, including new files Git doesn't track? This can't be undone from Git.", [this] {
            for (const GitFile& f : m_status.files) {
                const std::string path = absolute(f.path);
                std::error_code ec;
                if (fs::is_regular_file(path, ec)) m_history.record(path, current_text(path), "Before discard");
            }
            action({ step(git({ "reset", "--hard", "HEAD" })), step(git({ "clean", "-fd" })) }, "Discard all changes");
        });
    } else if (act == "tasks") {
        if (m_tasks.shared_exists) open_tasks_file();
        else create_tasks_file(false);
    } else if (act == "my-tasks") {
        open_local_tasks_file();
    } else if (act == "refresh") {
        rescan_repos();
        reload_tasks();
        m_sig_changes.clear();
        m_sig_tasks.clear();
        m_sig_log.clear();
        refresh();
    }
}

} // namespace canopy
