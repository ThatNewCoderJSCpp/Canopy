#ifndef CANOPY_LIVE_SERVER_HPP
#define CANOPY_LIVE_SERVER_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace canopy {

struct ConsoleMessage {
    std::string level;
    std::string text;
    std::string source;
    std::string page;
    std::string page_id;
    int line = 0;
    int column = 0;
};

struct LivePage {
    std::string id;
    std::string path;
};

struct PageDom {
    std::string page_id;
    unsigned long long serial = 0;
    std::string tree;
};

struct ServedFile {
    int status = 200;
    std::string file;
    std::string redirect;
    bool listing = false;
};

std::string mime_type_for(const std::string& path);
std::string url_decode(const std::string& text);
std::string url_encode_path(const std::string& path);
ServedFile resolve_served(const std::string& root, const std::string& url);
std::string inject_live_reload(const std::string& html);
std::string inject_page_script(const std::string& html, bool live_reload, bool console);
std::string page_script(bool live_reload, bool console);
std::vector<ConsoleMessage> parse_console_messages(const std::string& body);
std::string_view bundled_jquery() noexcept;
const char* bundled_jquery_version() noexcept;
bool is_jquery_request(const std::string& url);
std::string folder_listing_html(const std::string& root, const std::string& folder, const std::string& url_path);

void network_loop_acquire();
void network_loop_release();

class LiveServer {
public:
    LiveServer();
    ~LiveServer();
    LiveServer(const LiveServer&) = delete;
    LiveServer& operator=(const LiveServer&) = delete;

    bool start(const std::string& folder, std::uint16_t port, const std::string& host, bool live_reload, std::string& error);
    void stop();
    bool running() const;

    const std::string& folder() const;
    const std::string& host() const;
    std::uint16_t port() const;
    bool live_reload() const;
    std::string url(const std::string& file = {}) const;
    std::size_t requests() const;
    bool contains(const std::string& path) const;
    void changed(const std::string& path);
    std::vector<std::string> take_log();
    void set_console(bool on);
    bool console() const;
    std::vector<ConsoleMessage> take_console();
    std::size_t evaluate(const std::string& code);
    std::size_t pages() const;
    std::vector<LivePage> live_pages() const;
    std::size_t evaluate_in(const std::string& page_id, const std::string& code);
    void page_command(const std::string& page_id, const std::string& code);
    void watch_dom(const std::string& page_id);
    void stop_watching_dom();
    bool page_dom(const std::string& page_id, unsigned long long after, PageDom& out) const;
    std::string file_for_url(const std::string& url) const;
    void set_proxy(std::uint16_t port);
    std::uint16_t proxy() const;

    static void pump();

private:
    struct State;
    std::shared_ptr<State> m_state;
};

} // namespace canopy

#endif // CANOPY_LIVE_SERVER_HPP
