#ifndef CANOPY_DEVELOPER_HPP
#define CANOPY_DEVELOPER_HPP

#include <grove/markup/node.hpp>

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace canopy {

struct OsDetails {
    std::string name;
    std::string version;
    std::string kernel;
    std::string architecture;
    std::string host;
    std::string desktop;
    std::string session;
    double uptime_seconds = 0.0;
};

OsDetails os_details();
std::string format_byte_size(std::uint64_t bytes);
std::string format_uptime(double seconds);
std::string size_with_share(double part, double whole, const std::string& unit);
std::string compiler_description();

class DeveloperPage {
public:
    explicit DeveloperPage(grove::Document& doc);
    ~DeveloperPage();
    DeveloperPage(const DeveloperPage&) = delete;
    DeveloperPage& operator=(const DeveloperPage&) = delete;

    void build(grove::Node& view);
    void refresh();
    void stop();
    bool running() const noexcept { return m_view != nullptr; }

private:
    struct Trackers;

    void card(grove::Node& parent, const std::string& title, const std::vector<std::pair<std::string, std::string>>& rows);
    void set(const std::string& key, const std::string& value);
    void bar(const std::string& key, double percent);
    void start_tracking();

    grove::Document& m_doc;
    grove::Node* m_view = nullptr;
    std::map<std::string, grove::Node*> m_values;
    std::map<std::string, grove::Node*> m_bars;
    std::map<std::string, std::string> m_shown;
    std::unique_ptr<Trackers> m_trackers;
    OsDetails m_os;
    std::chrono::steady_clock::time_point m_opened;
};

} // namespace canopy

#endif // CANOPY_DEVELOPER_HPP
