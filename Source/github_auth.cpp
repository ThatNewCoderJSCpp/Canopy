#include <canopy/github_auth.hpp>
#include <canopy/live_server.hpp>
#include <canopy/secure_store.hpp>

#include <fizmo/../../Source/Networking/Node/http.hpp>
#include <fizmo/../../Source/Networking/Node/loop.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <mutex>
#include <thread>

namespace fs = std::filesystem;
namespace fnet = fizmo::networking;

#ifndef CANOPY_GITHUB_CLIENT_ID
#define CANOPY_GITHUB_CLIENT_ID ""
#endif

namespace canopy {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    return s.substr(i);
}

std::string url_escape(const std::string& text) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (const unsigned char c : text) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

std::string url_unescape(const std::string& text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '+') {
            out += ' ';
        } else if (text[i] == '%' && i + 2 < text.size() && std::isxdigit(static_cast<unsigned char>(text[i + 1])) && std::isxdigit(static_cast<unsigned char>(text[i + 2]))) {
            out += static_cast<char>(std::strtol(text.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        } else {
            out += text[i];
        }
    }
    return out;
}

std::string env_or(const char* name, const std::string& fallback) {
    const char* v = std::getenv(name);
    return v && *v ? std::string(v) : fallback;
}

struct Reply {
    bool        ok = false;
    int         status = 0;
    std::string body;
    std::string scopes;
    std::string error;
};

Reply fetch_blocking(const std::string& url, const std::string& method, const std::string& body, const fnet::http::Headers& headers) {
    Reply out;
    network_loop_acquire();
    auto promise = std::make_shared<std::promise<Reply>>();
    std::future<Reply> future = promise->get_future();
    fnet::http::FetchOptions options;
    options.method = method;
    options.headers = headers;
    options.headers.set("User-Agent", "Canopy");
    options.body = body;
    options.timeout = std::chrono::milliseconds(30000);
    fnet::set_immediate([url, options, promise] {
        fnet::http::fetch(url, options, [promise](const std::error_code& ec, fnet::http::FetchResponse response) {
            Reply r;
            if (ec) {
                r.error = ec.message();
            } else {
                r.ok = true;
                r.status = response.status;
                r.body = response.body;
                r.scopes = response.headers.get("x-oauth-scopes");
            }
            try {
                promise->set_value(r);
            } catch (...) {
            }
        });
    });
    if (future.wait_for(std::chrono::seconds(40)) == std::future_status::ready) out = future.get();
    else out.error = "GitHub didn't answer";
    network_loop_release();
    return out;
}

std::string connect_error(const Reply& r) {
    return "Canopy couldn't reach GitHub (" + (r.error.empty() ? std::string("no answer") : r.error) + "). Check your internet connection and try again.";
}

void sleep_for(int seconds, const std::atomic<bool>& cancel) {
    for (int i = 0; i < seconds * 10 && !cancel; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

} // namespace

std::string github_login_path() {
    return (fs::path(user_config_dir()) / "github-login").string();
}

namespace {

const char* k_secure_service = "canopy-github";
const char* k_session_variable = "CANOPY_GITHUB_SESSION";

struct LoginFile {
    std::string user;
    std::string store;
    std::string token;
};

LoginFile read_login_file() {
    LoginFile file;
    std::ifstream in(github_login_path(), std::ios::binary);
    std::string line;
    while (in && std::getline(in, line)) {
        line = trim(line);
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        if (key == "user") file.user = value;
        else if (key == "store") file.store = value;
        else if (key == "token") file.token = value;
    }
    return file;
}

bool write_login_file(const std::string& user, const std::string& store) {
    const fs::path path = github_login_path();
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    const fs::path temp = path.string() + ".part";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        fs::permissions(temp, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);
        out << "user=" << user << "\nstore=" << store << "\n";
        if (!out) return false;
    }
    fs::rename(temp, path, ec);
    if (ec) {
        fs::remove(temp, ec);
        return false;
    }
    return true;
}

void set_session(const std::string& value) {
#if defined(_WIN32)
    _putenv_s(k_session_variable, value.c_str());
#else
    if (value.empty()) unsetenv(k_session_variable);
    else setenv(k_session_variable, value.c_str(), 1);
#endif
}

std::optional<GitHubLogin> session_login() {
    const char* v = std::getenv(k_session_variable);
    if (!v || !*v) return std::nullopt;
    const std::string text = v;
    const std::size_t colon = text.find(':');
    if (colon == std::string::npos) return std::nullopt;
    GitHubLogin login{ text.substr(0, colon), text.substr(colon + 1) };
    if (login.user.empty() || login.token.empty()) return std::nullopt;
    return login;
}

} // namespace

std::optional<GitHubLogin> load_github_login() {
    const LoginFile file = read_login_file();
    if (file.user.empty()) return session_login();
    if (!file.token.empty()) {
        GitHubLogin legacy{ file.user, file.token };
        save_github_login(legacy);
        return legacy;
    }
    if (file.store == "keyring") {
        if (const std::optional<std::string> token = secure_store_load(k_secure_service, file.user); token && !token->empty()) return GitHubLogin{ file.user, *token };
        return std::nullopt;
    }
    std::optional<GitHubLogin> session = session_login();
    if (session && session->user == file.user) return session;
    return std::nullopt;
}

bool save_github_login(const GitHubLogin& login) {
    if (login.user.empty() || login.token.empty()) return false;
    if (secure_store_save(k_secure_service, login.user, login.token)) {
        set_session(std::string());
        return write_login_file(login.user, "keyring");
    }
    set_session(login.user + ":" + login.token);
    return write_login_file(login.user, "session");
}

bool forget_github_login() {
    const LoginFile file = read_login_file();
    if (!file.user.empty()) secure_store_delete(k_secure_service, file.user);
    set_session(std::string());
    std::error_code ec;
    return fs::remove(github_login_path(), ec);
}

std::string github_login_storage() {
    const LoginFile file = read_login_file();
    if (file.store == "keyring") return secure_store_name();
    if (file.user.empty()) return std::string();
    return "memory, until Canopy closes";
}

std::string github_client_id(const Settings& settings) {
    return github_client_id(settings.get_or("git", "github-client-id", ""));
}

std::string github_client_id(const std::string& configured) {
    const std::string id = trim(configured);
    if (!id.empty()) return id;
    return env_or("CANOPY_GITHUB_CLIENT_ID", CANOPY_GITHUB_CLIENT_ID);
}

std::string github_web_base() {
    return env_or("CANOPY_GITHUB_WEB", "https://github.com");
}

std::string github_api_base() {
    return env_or("CANOPY_GITHUB_API", "https://api.github.com");
}

std::string github_token_page() {
    return github_web_base() + "/settings/tokens/new?scopes=repo,workflow&description=Canopy";
}

std::string github_askpass_answer(const std::string& prompt) {
    const std::string p = lower(prompt);
    if (p.find("github.com") == std::string::npos) return std::string();
    const std::optional<GitHubLogin> login = load_github_login();
    if (!login) return std::string();
    if (p.find("username") != std::string::npos) return login->user;
    if (p.find("password") != std::string::npos || p.find("token") != std::string::npos) return login->token;
    return std::string();
}

bool github_auth_failure(const std::string& output) {
    const std::string p = lower(output);
    return p.find("authentication failed") != std::string::npos || p.find("invalid username or token") != std::string::npos || p.find("password authentication is not supported") != std::string::npos || p.find("could not read username") != std::string::npos || p.find("could not read password") != std::string::npos;
}

std::map<std::string, std::string> parse_form(const std::string& text) {
    std::map<std::string, std::string> out;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('&', start);
        if (end == std::string::npos) end = text.size();
        const std::string pair = text.substr(start, end - start);
        const std::size_t eq = pair.find('=');
        if (!pair.empty()) out[url_unescape(pair.substr(0, eq))] = eq == std::string::npos ? std::string() : url_unescape(trim(pair.substr(eq + 1)));
        start = end + 1;
    }
    return out;
}

std::string json_string_field(const std::string& json, const std::string& name) {
    const std::string key = "\"" + name + "\"";
    std::size_t at = json.find(key);
    if (at == std::string::npos) return std::string();
    at = json.find(':', at + key.size());
    if (at == std::string::npos) return std::string();
    at = json.find_first_not_of(" \t\r\n", at + 1);
    if (at == std::string::npos || json[at] != '"') return std::string();
    std::string out;
    for (std::size_t i = at + 1; i < json.size(); ++i) {
        if (json[i] == '\\' && i + 1 < json.size()) {
            out += json[++i];
        } else if (json[i] == '"') {
            return out;
        } else {
            out += json[i];
        }
    }
    return std::string();
}

GitHubResult github_check_token(const std::string& token_in) {
    GitHubResult result;
    const std::string token = trim(token_in);
    if (token.empty()) {
        result.error = "Paste a token first.";
        return result;
    }
    fnet::http::Headers headers;
    headers.set("Authorization", "Bearer " + token);
    headers.set("Accept", "application/vnd.github+json");
    const Reply reply = fetch_blocking(github_api_base() + "/user", "GET", std::string(), headers);
    if (!reply.ok) {
        result.error = connect_error(reply);
        return result;
    }
    if (reply.status == 401) {
        result.error = "GitHub didn't accept that token. Check that you copied all of it and that it hasn't expired.";
        return result;
    }
    if (reply.status != 200) {
        result.error = "GitHub answered " + std::to_string(reply.status) + " when Canopy checked the token.";
        return result;
    }
    result.user = json_string_field(reply.body, "login");
    if (result.user.empty()) {
        result.error = "GitHub accepted the token but didn't say whose it is.";
        return result;
    }
    result.scopes = reply.scopes;
    const std::string scopes = "," + lower(reply.scopes) + ",";
    std::string compact;
    for (char c : scopes) if (c != ' ') compact += c;
    if (!reply.scopes.empty() && compact.find(",repo,") == std::string::npos) {
        result.error = "This token can't push: it needs the repo permission. Make a new one with repo and workflow ticked.";
        return result;
    }
    result.token = token;
    result.ok = true;
    return result;
}

GitHubDeviceCode github_device_code(const std::string& client_id) {
    GitHubDeviceCode code;
    fnet::http::Headers headers;
    headers.set("Content-Type", "application/x-www-form-urlencoded");
    const Reply reply = fetch_blocking(github_web_base() + "/login/device/code", "POST", "client_id=" + url_escape(client_id) + "&scope=" + url_escape("repo workflow"), headers);
    if (!reply.ok) {
        code.error = connect_error(reply);
        return code;
    }
    const std::map<std::string, std::string> form = parse_form(reply.body);
    auto field = [&](const char* name) {
        const auto it = form.find(name);
        return it == form.end() ? std::string() : it->second;
    };
    if (reply.status != 200 || field("device_code").empty()) {
        const std::string why = field("error_description").empty() ? field("error") : field("error_description");
        code.error = "GitHub wouldn't start the sign-in" + (why.empty() ? std::string(" (") + std::to_string(reply.status) + ")" : ": " + why) + ". Check git.github-client-id, and that Device Flow is turned on for that OAuth app.";
        return code;
    }
    code.device_code = field("device_code");
    code.user_code = field("user_code");
    code.verification_uri = field("verification_uri").empty() ? github_web_base() + "/login/device" : field("verification_uri");
    code.interval = std::max(1, std::atoi(field("interval").c_str()));
    if (field("interval").empty()) code.interval = 5;
    code.expires_in = field("expires_in").empty() ? 900 : std::atoi(field("expires_in").c_str());
    code.ok = true;
    return code;
}

GitHubResult github_device_wait(const std::string& client_id, const GitHubDeviceCode& code, const std::atomic<bool>& cancel) {
    GitHubResult result;
    int interval = code.interval;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(code.expires_in);
    fnet::http::Headers headers;
    headers.set("Content-Type", "application/x-www-form-urlencoded");
    const std::string body = "client_id=" + url_escape(client_id) + "&device_code=" + url_escape(code.device_code) + "&grant_type=" + url_escape("urn:ietf:params:oauth:grant-type:device_code");
    while (!cancel) {
        sleep_for(interval, cancel);
        if (cancel) break;
        if (std::chrono::steady_clock::now() > deadline) {
            result.error = "The code expired before it was entered on GitHub. Sign in again for a new one.";
            return result;
        }
        const Reply reply = fetch_blocking(github_web_base() + "/login/oauth/access_token", "POST", body, headers);
        if (!reply.ok) continue;
        const std::map<std::string, std::string> form = parse_form(reply.body);
        const auto token = form.find("access_token");
        if (token != form.end() && !token->second.empty()) return github_check_token(token->second);
        const auto error = form.find("error");
        const std::string e = error == form.end() ? std::string() : error->second;
        if (e == "authorization_pending") continue;
        if (e == "slow_down") {
            interval += 5;
            continue;
        }
        if (e == "access_denied") result.error = "The sign-in was cancelled on GitHub.";
        else if (e == "expired_token") result.error = "The code expired before it was entered on GitHub. Sign in again for a new one.";
        else result.error = "GitHub stopped the sign-in" + (e.empty() ? std::string() : " (" + e + ")") + ".";
        return result;
    }
    result.error = "Sign-in cancelled.";
    return result;
}

bool github_share_with_git(const GitHubLogin& login, std::string& helper) {
    helper.clear();
    {
#if defined(_WIN32)
        FILE* pipe = _popen("git config --get credential.helper 2>NUL", "r");
#else
        FILE* pipe = popen("git config --get credential.helper 2>/dev/null", "r");
#endif
        if (!pipe) return false;
        char buffer[512];
        std::size_t n = 0;
        while ((n = std::fread(buffer, 1, sizeof(buffer), pipe)) > 0) helper.append(buffer, n);
#if defined(_WIN32)
        _pclose(pipe);
#else
        pclose(pipe);
#endif
    }
    helper = trim(helper);
    if (helper.empty()) return false;
    const std::string kind = lower(helper);
    const bool secure = kind.find("manager") != std::string::npos || kind.find("osxkeychain") != std::string::npos || kind.find("libsecret") != std::string::npos || kind.find("wincred") != std::string::npos || kind.find("gnome-keyring") != std::string::npos;
    if (!secure) return false;
#if defined(_WIN32)
    FILE* pipe = _popen("git credential approve >NUL 2>NUL", "w");
#else
    FILE* pipe = popen("git credential approve >/dev/null 2>&1", "w");
#endif
    if (!pipe) return false;
    const std::string input = "protocol=https\nhost=github.com\nusername=" + login.user + "\npassword=" + login.token + "\n\n";
    std::fwrite(input.data(), 1, input.size(), pipe);
#if defined(_WIN32)
    return _pclose(pipe) == 0;
#else
    return pclose(pipe) == 0;
#endif
}

struct GitHubSignIn::State {
    std::mutex                 m;
    std::optional<Update>      update;
    std::atomic<bool>          cancel{ false };
    std::atomic<bool>          busy{ false };
    std::thread                worker;

    void post(Update u) {
        std::lock_guard<std::mutex> lk(m);
        update = std::move(u);
    }
};

GitHubSignIn::GitHubSignIn() : m_state(std::make_shared<State>()) {}

GitHubSignIn::~GitHubSignIn() {
    m_state->cancel = true;
    if (m_state->worker.joinable()) m_state->worker.detach();
}

bool GitHubSignIn::start_device(const std::string& client_id) {
    if (m_state->busy) return false;
    if (m_state->worker.joinable()) m_state->worker.join();
    m_state->cancel = false;
    m_state->busy = true;
    std::shared_ptr<State> state = m_state;
    state->worker = std::thread([state, client_id] {
        Update waiting;
        waiting.stage = Stage::Waiting;
        state->post(waiting);
        Update u;
        u.code = github_device_code(client_id);
        if (!u.code.ok) {
            u.stage = Stage::Failed;
            u.result.error = u.code.error;
            state->post(u);
            state->busy = false;
            return;
        }
        u.stage = Stage::Code;
        state->post(u);
        u.result = github_device_wait(client_id, u.code, state->cancel);
        u.stage = u.result.ok ? Stage::Done : Stage::Failed;
        if (!state->cancel) state->post(u);
        state->busy = false;
    });
    return true;
}

bool GitHubSignIn::start_token(const std::string& token) {
    if (m_state->busy) return false;
    if (m_state->worker.joinable()) m_state->worker.join();
    m_state->cancel = false;
    m_state->busy = true;
    std::shared_ptr<State> state = m_state;
    state->worker = std::thread([state, token] {
        Update u;
        u.result = github_check_token(token);
        u.stage = u.result.ok ? Stage::Done : Stage::Failed;
        state->post(u);
        state->busy = false;
    });
    return true;
}

void GitHubSignIn::cancel() {
    m_state->cancel = true;
}

bool GitHubSignIn::busy() const {
    return m_state->busy;
}

std::optional<GitHubSignIn::Update> GitHubSignIn::take() {
    std::lock_guard<std::mutex> lk(m_state->m);
    std::optional<Update> u = std::move(m_state->update);
    m_state->update.reset();
    return u;
}

} // namespace canopy
