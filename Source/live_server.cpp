#include <canopy/live_server.hpp>

#include <fizmo/../../Source/Networking/Node/http.hpp>
#include <fizmo/../../Source/Networking/Node/loop.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;
namespace fnet = fizmo::networking;

namespace canopy {

namespace {

const char* const k_reload_path = "/__canopy/reload";

std::mutex g_loop_mutex;
int g_running_servers = 0;

void loop_started() {
    std::lock_guard<std::mutex> lk(g_loop_mutex);
    if (g_running_servers++ == 0 && !fnet::background_running()) fnet::start_background(1);
}

void loop_stopped() {
    std::lock_guard<std::mutex> lk(g_loop_mutex);
    if (g_running_servers > 0 && --g_running_servers == 0 && fnet::background_running()) fnet::stop_background();
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string html_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += c;
        }
    }
    return out;
}

bool inside(const fs::path& path, const fs::path& root) {
    std::string p = path.generic_string();
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    std::string r = root.generic_string();
    while (r.size() > 1 && r.back() == '/') r.pop_back();
    if (p == r) return true;
    if (!r.empty() && r.back() != '/') r += '/';
    return p.size() > r.size() && p.compare(0, r.size(), r) == 0;
}

fs::path clean_root(const std::string& folder) {
    std::error_code ec;
    fs::path root = fs::weakly_canonical(fs::absolute(folder, ec), ec);
    if (ec) root = fs::absolute(folder).lexically_normal();
    while (root.has_relative_path() && root.filename().empty()) root = root.parent_path();
    return root;
}

std::optional<std::string> read_all(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string reload_script() {
    return std::string("<script>(function(){var seen=null;function check(){fetch('") + k_reload_path +
           "',{cache:'no-store'}).then(function(r){return r.text();}).then(function(v){if(seen!==null&&v!==seen){location.reload();return;}seen=v;setTimeout(check,600);})"
           ".catch(function(){setTimeout(check,1500);});}check();})();</script>";
}

std::string error_page(int code, const std::string& message) {
    return "<!doctype html><html><head><meta charset=\"utf-8\"><title>" + std::to_string(code) + "</title></head><body style=\"font-family:sans-serif;padding:24px\"><h1>" +
           std::to_string(code) + " " + html_escape(fnet::http::status_text(code)) + "</h1><p>" + html_escape(message) + "</p><p style=\"color:#888\">Canopy local server</p></body></html>";
}

} // namespace

std::string mime_type_for(const std::string& path) {
    static const std::pair<const char*, const char*> types[] = {
        { ".html", "text/html; charset=utf-8" }, { ".htm", "text/html; charset=utf-8" }, { ".css", "text/css; charset=utf-8" },
        { ".js", "text/javascript; charset=utf-8" }, { ".mjs", "text/javascript; charset=utf-8" }, { ".json", "application/json; charset=utf-8" },
        { ".map", "application/json; charset=utf-8" }, { ".txt", "text/plain; charset=utf-8" }, { ".md", "text/markdown; charset=utf-8" },
        { ".csv", "text/csv; charset=utf-8" }, { ".xml", "application/xml; charset=utf-8" }, { ".svg", "image/svg+xml" },
        { ".png", "image/png" }, { ".jpg", "image/jpeg" }, { ".jpeg", "image/jpeg" }, { ".gif", "image/gif" }, { ".webp", "image/webp" },
        { ".avif", "image/avif" }, { ".bmp", "image/bmp" }, { ".ico", "image/x-icon" }, { ".wasm", "application/wasm" },
        { ".woff", "font/woff" }, { ".woff2", "font/woff2" }, { ".ttf", "font/ttf" }, { ".otf", "font/otf" },
        { ".mp3", "audio/mpeg" }, { ".wav", "audio/wav" }, { ".ogg", "audio/ogg" }, { ".flac", "audio/flac" },
        { ".mp4", "video/mp4" }, { ".webm", "video/webm" }, { ".pdf", "application/pdf" }, { ".zip", "application/zip" },
        { ".webmanifest", "application/manifest+json" },
    };
    const std::string ext = lower(fs::path(path).extension().string());
    for (const auto& [e, type] : types) {
        if (ext == e) return type;
    }
    return "application/octet-stream";
}

std::string url_decode(const std::string& text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '%' && i + 2 < text.size() && std::isxdigit(static_cast<unsigned char>(text[i + 1])) && std::isxdigit(static_cast<unsigned char>(text[i + 2]))) {
            out += static_cast<char>(std::stoi(text.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else {
            out += c;
        }
    }
    return out;
}

std::string url_encode_path(const std::string& path) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (const unsigned char c : path) {
        const bool plain = std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~' || c == '/';
        if (plain) out += static_cast<char>(c);
        else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

ServedFile resolve_served(const std::string& root_folder, const std::string& url) {
    ServedFile out;
    std::string path = url.substr(0, url.find_first_of("?#"));
    path = url_decode(path);
    if (path.empty() || path[0] != '/') path = "/" + path;
    if (path.find('\0') != std::string::npos) {
        out.status = 400;
        return out;
    }
    std::replace(path.begin(), path.end(), '\\', '/');
    const fs::path root = clean_root(root_folder);
    const fs::path target = (root / fs::path(path.substr(1))).lexically_normal();
    if (!inside(target, root)) {
        out.status = 403;
        return out;
    }
    std::error_code ec;
    if (fs::is_directory(target, ec)) {
        if (path.back() != '/') {
            out.status = 301;
            out.redirect = url_encode_path(path + "/");
            const std::size_t q = url.find('?');
            if (q != std::string::npos) out.redirect += url.substr(q);
            return out;
        }
        for (const char* index : { "index.html", "index.htm" }) {
            if (fs::is_regular_file(target / index, ec)) {
                out.file = (target / index).string();
                return out;
            }
        }
        out.listing = true;
        out.file = target.string();
        return out;
    }
    if (fs::is_regular_file(target, ec)) {
        out.file = target.string();
        return out;
    }
    if (!target.has_extension()) {
        fs::path html = target;
        html += ".html";
        if (fs::is_regular_file(html, ec)) {
            out.file = html.string();
            return out;
        }
    }
    out.status = 404;
    return out;
}

std::string inject_live_reload(const std::string& html) {
    const std::string script = reload_script();
    const std::string low = lower(html);
    const std::size_t body = low.rfind("</body>");
    if (body != std::string::npos) return html.substr(0, body) + script + html.substr(body);
    const std::size_t end = low.rfind("</html>");
    if (end != std::string::npos) return html.substr(0, end) + script + html.substr(end);
    return html + script;
}

std::string folder_listing_html(const std::string& root, const std::string& folder, const std::string& url_path) {
    std::vector<std::pair<std::string, bool>> entries;
    std::error_code ec;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.empty() || name[0] == '.') continue;
        std::error_code dir_ec;
        entries.emplace_back(name, it->is_directory(dir_ec));
    }
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second;
        return lower(a.first) < lower(b.first);
    });
    std::string title = url_path.empty() ? std::string("/") : url_path;
    std::string out = "<!doctype html><html><head><meta charset=\"utf-8\"><title>Index of " + html_escape(title) +
                      "</title><style>body{font-family:sans-serif;padding:20px 28px;}a{text-decoration:none;}li{padding:2px 0;}.d{font-weight:bold;}</style></head><body><h2>Index of " +
                      html_escape(title) + "</h2><ul>";
    if (clean_root(folder) != clean_root(root)) out += "<li><a href=\"../\">../</a></li>";
    for (const auto& [name, dir] : entries) {
        const std::string href = url_encode_path(name) + (dir ? "/" : "");
        out += "<li><a" + std::string(dir ? " class=\"d\"" : "") + " href=\"" + href + "\">" + html_escape(name) + (dir ? "/" : "") + "</a></li>";
    }
    out += "</ul></body></html>";
    return out;
}

struct LiveServer::State {
    std::mutex m;
    fnet::http::Server server;
    std::string folder;
    std::string host;
    std::uint16_t port = 0;
    bool live_reload = true;
    bool running = false;
    std::size_t requests = 0;
    unsigned long long version = 1;
    std::vector<std::string> log;

    void note(const std::string& line) {
        std::lock_guard<std::mutex> lk(m);
        log.push_back(line);
        if (log.size() > 500) log.erase(log.begin(), log.begin() + 250);
    }

    void handle(fnet::http::IncomingMessage req, fnet::http::ServerResponse res) {
        const std::string method = req.method();
        const std::string url = req.url();
        std::string root;
        bool reload = false;
        unsigned long long now = 0;
        {
            std::lock_guard<std::mutex> lk(m);
            ++requests;
            root = folder;
            reload = live_reload;
            now = version;
        }
        res.set_header("Cache-Control", "no-store");
        if (method != "GET" && method != "HEAD") {
            req.discard();
            res.set_header("Allow", "GET, HEAD");
            res.send(405, error_page(405, "Only GET and HEAD work here."), "text/html; charset=utf-8");
            note(method + " " + url + " 405");
            return;
        }
        if (url.substr(0, url.find('?')) == k_reload_path) {
            res.send(200, std::to_string(now), "text/plain; charset=utf-8");
            return;
        }
        const ServedFile served = resolve_served(root, url);
        if (!served.redirect.empty()) {
            res.redirect(served.redirect, served.status);
            note(method + " " + url + " " + std::to_string(served.status));
            return;
        }
        if (served.status != 200) {
            const std::string why = served.status == 404 ? "There's no file at " + url_decode(url.substr(0, url.find_first_of("?#"))) + " in this folder." : std::string("That path isn't allowed.");
            res.send(served.status, error_page(served.status, why), "text/html; charset=utf-8");
            note(method + " " + url + " " + std::to_string(served.status));
            return;
        }
        if (served.listing) {
            std::string page = folder_listing_html(root, served.file, url_decode(url.substr(0, url.find_first_of("?#"))));
            if (reload) page = inject_live_reload(page);
            res.send(200, page, "text/html; charset=utf-8");
            note(method + " " + url + " 200");
            return;
        }
        std::optional<std::string> body = read_all(served.file);
        if (!body) {
            res.send(500, error_page(500, "Couldn't read " + fs::path(served.file).filename().string() + "."), "text/html; charset=utf-8");
            note(method + " " + url + " 500");
            return;
        }
        const std::string type = mime_type_for(served.file);
        if (reload && type.rfind("text/html", 0) == 0) *body = inject_live_reload(*body);
        res.send(200, *body, type);
        note(method + " " + url + " 200");
    }
};

LiveServer::LiveServer() : m_state(std::make_shared<State>()) {}

LiveServer::~LiveServer() {
    stop();
}

bool LiveServer::start(const std::string& folder, std::uint16_t port, const std::string& host, bool live_reload, std::string& error) {
    stop();
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) {
        error = folder + " isn't a folder";
        return false;
    }
    const std::string bind = host.empty() ? std::string("127.0.0.1") : host;
    std::weak_ptr<State> weak = m_state;
    std::string last;
    const std::uint16_t first = port == 0 ? 5500 : port;
    for (unsigned attempt = 0; attempt < 25; ++attempt) {
        const unsigned want = static_cast<unsigned>(first) + attempt;
        if (want > 65535) break;
        fnet::http::Server server = fnet::http::create_server([weak](fnet::http::IncomingMessage req, fnet::http::ServerResponse res) {
            if (const std::shared_ptr<State> st = weak.lock()) st->handle(std::move(req), std::move(res));
            else res.send(503, "stopped");
        });
        server.on_error([weak](const std::error_code& e) {
            if (const std::shared_ptr<State> st = weak.lock()) st->note("error: " + e.message());
        });
        server.on_client_error([](const std::error_code&, fnet::net::Socket socket) { socket.destroy(); });
        server.listen(static_cast<std::uint16_t>(want), bind);
        if (server.listening()) {
            std::lock_guard<std::mutex> lk(m_state->m);
            m_state->server = server;
            m_state->folder = clean_root(folder).string();
            m_state->host = bind;
            m_state->port = server.port() != 0 ? server.port() : static_cast<std::uint16_t>(want);
            m_state->live_reload = live_reload;
            m_state->running = true;
            m_state->requests = 0;
            loop_started();
            return true;
        }
        last = "port " + std::to_string(want) + " is busy";
        server.close();
    }
    error = "Couldn't start a server: " + (last.empty() ? std::string("no free port") : last + " and so are the next ones");
    return false;
}

void LiveServer::stop() {
    fnet::http::Server server;
    {
        std::lock_guard<std::mutex> lk(m_state->m);
        if (!m_state->running) return;
        m_state->running = false;
        server = m_state->server;
        m_state->server = fnet::http::Server();
    }
    if (server) {
        server.close();
        server.close_all_connections();
    }
    loop_stopped();
}

bool LiveServer::running() const {
    std::lock_guard<std::mutex> lk(m_state->m);
    return m_state->running;
}

const std::string& LiveServer::folder() const {
    return m_state->folder;
}

const std::string& LiveServer::host() const {
    return m_state->host;
}

std::uint16_t LiveServer::port() const {
    return m_state->port;
}

bool LiveServer::live_reload() const {
    return m_state->live_reload;
}

std::string LiveServer::url(const std::string& file) const {
    const std::string shown = m_state->host == "0.0.0.0" || m_state->host == "::" || m_state->host == "127.0.0.1" ? std::string("localhost") : m_state->host;
    std::string out = "http://" + shown + ":" + std::to_string(m_state->port) + "/";
    if (file.empty()) return out;
    const fs::path root = clean_root(m_state->folder);
    std::error_code ec;
    fs::path target = fs::absolute(file, ec).lexically_normal();
    if (!inside(target, root)) return out;
    std::string rel = target.lexically_relative(root).generic_string();
    if (rel == ".") rel.clear();
    if (fs::is_directory(target, ec) && !rel.empty() && rel.back() != '/') rel += '/';
    return out + url_encode_path(rel);
}

std::size_t LiveServer::requests() const {
    std::lock_guard<std::mutex> lk(m_state->m);
    return m_state->requests;
}

bool LiveServer::contains(const std::string& path) const {
    std::error_code ec;
    return inside(fs::absolute(path, ec).lexically_normal(), clean_root(m_state->folder));
}

void LiveServer::changed(const std::string& path) {
    if (!running() || !contains(path)) return;
    std::lock_guard<std::mutex> lk(m_state->m);
    ++m_state->version;
}

std::vector<std::string> LiveServer::take_log() {
    std::lock_guard<std::mutex> lk(m_state->m);
    std::vector<std::string> out;
    out.swap(m_state->log);
    return out;
}

void LiveServer::pump() {
    if (!fnet::background_running()) fnet::poll();
}

} // namespace canopy
