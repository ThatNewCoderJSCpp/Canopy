#ifndef CANOPY_LIVE_SERVER_HPP
#define CANOPY_LIVE_SERVER_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace canopy {

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
std::string folder_listing_html(const std::string& root, const std::string& folder, const std::string& url_path);

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

    static void pump();

private:
    struct State;
    std::shared_ptr<State> m_state;
};

} // namespace canopy

#endif // CANOPY_LIVE_SERVER_HPP
