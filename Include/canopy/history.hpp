#ifndef CANOPY_HISTORY_HPP
#define CANOPY_HISTORY_HPP

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace canopy {

struct HistoryEntry {
    long long time_ms = 0;
    std::size_t size = 0;
    std::string label;
    std::string snapshot;
};

class LocalHistory {
public:
    LocalHistory();
    explicit LocalHistory(std::string base);

    const std::string& base() const noexcept { return m_base; }
    std::size_t limit() const noexcept { return m_limit; }
    void set_limit(std::size_t limit) noexcept { m_limit = limit == 0 ? 1 : limit; }
    std::size_t max_bytes() const noexcept { return m_max_bytes; }
    void set_max_bytes(std::size_t bytes) noexcept { m_max_bytes = bytes; }

    std::string folder_for(const std::string& path) const;
    bool record(const std::string& path, const std::string& text, const std::string& label = "Saved");
    std::vector<HistoryEntry> entries(const std::string& path) const;
    std::optional<std::string> read(const HistoryEntry& entry) const;
    std::optional<std::string> latest(const std::string& path) const;
    bool remove(const std::string& path, long long time_ms);
    bool clear(const std::string& path);

private:
    bool write_entries(const std::string& folder, const std::vector<HistoryEntry>& entries) const;

    std::string m_base;
    std::size_t m_limit = 50;
    std::size_t m_max_bytes = 2 * 1024 * 1024;
};

std::string history_key(const std::string& path);
long long now_ms();

} // namespace canopy

#endif // CANOPY_HISTORY_HPP
