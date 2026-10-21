#ifndef CANOPY_GITHUB_AUTH_HPP
#define CANOPY_GITHUB_AUTH_HPP

#include "settings.hpp"

#include <atomic>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace canopy {

struct GitHubLogin {
    std::string user;
    std::string token;
};

struct GitHubResult {
    bool        ok = false;
    std::string user;
    std::string token;
    std::string scopes;
    std::string error;
};

struct GitHubRepo {
    std::string full_name;
    std::string description;
    std::string clone_url;
    std::string ssh_url;
    bool        is_private = false;
    bool        fork = false;
    bool        archived = false;
};

struct GitHubRepoList {
    bool                    ok = false;
    std::vector<GitHubRepo> repos;
    std::string             error;
};

struct GitHubKeyResult {
    bool        ok = false;
    bool        needs_permission = false;
    bool        already_there = false;
    std::string error;
};

struct GitHubDeviceCode {
    bool        ok = false;
    std::string device_code;
    std::string user_code;
    std::string verification_uri;
    int         interval = 5;
    int         expires_in = 900;
    std::string error;
};

std::string github_login_path();
std::optional<GitHubLogin> load_github_login();
bool save_github_login(const GitHubLogin& login);
bool forget_github_login();
std::string github_login_storage();

std::string github_client_id(const Settings& settings);
std::string github_client_id(const std::string& configured);
std::string github_token_page();
std::string github_web_base();
std::string github_api_base();
std::string github_askpass_answer(const std::string& prompt);
bool github_auth_failure(const std::string& output);
std::map<std::string, std::string> parse_form(const std::string& text);
std::string json_string_field(const std::string& json, const std::string& name);

GitHubResult github_check_token(const std::string& token);
GitHubDeviceCode github_device_code(const std::string& client_id);
GitHubResult github_device_wait(const std::string& client_id, const GitHubDeviceCode& code, const std::atomic<bool>& cancel);
bool github_share_with_git(const GitHubLogin& login, std::string& helper);
std::vector<GitHubRepo> parse_github_repos(const std::string& json);
GitHubRepoList github_list_repos(const std::string& token);
GitHubKeyResult github_add_ssh_key(const std::string& token, const std::string& title, const std::string& public_key);
std::string github_ssh_keys_page();

class GitHubSignIn {
public:
    enum class Stage {
        Idle,
        Waiting,
        Code,
        Done,
        Failed
    };

    struct Update {
        Stage            stage = Stage::Idle;
        GitHubDeviceCode code;
        GitHubResult     result;
    };

    GitHubSignIn();
    ~GitHubSignIn();
    GitHubSignIn(const GitHubSignIn&) = delete;
    GitHubSignIn& operator=(const GitHubSignIn&) = delete;

    bool start_device(const std::string& client_id);
    bool start_token(const std::string& token);
    void cancel();
    bool busy() const;
    std::optional<Update> take();

private:
    struct State;
    std::shared_ptr<State> m_state;
};

} // namespace canopy

#endif // CANOPY_GITHUB_AUTH_HPP
