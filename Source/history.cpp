#include "canopy/history.hpp"

#include "canopy/settings.hpp"
#include "canopy/workspace.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace canopy {

namespace {

std::string normal_path(const std::string& path) {
    std::error_code ec;
    fs::path p = fs::absolute(fs::path(path), ec);
    if (ec) p = fs::path(path);
    return p.lexically_normal().generic_string();
}

std::string entries_file(const std::string& folder) {
    return (fs::path(folder) / "entries.txt").string();
}

std::string clean_label(std::string label) {
    for (char& c : label) {
        if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    }
    return label;
}

} // namespace

long long now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string history_key(const std::string& path) {
    std::uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : normal_path(path)) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    char buffer[17];
    std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(h));
    return buffer;
}

LocalHistory::LocalHistory() : m_base((fs::path(user_config_dir()) / "history").string()) {}

LocalHistory::LocalHistory(std::string base) : m_base(std::move(base)) {}

std::string LocalHistory::folder_for(const std::string& path) const {
    return (fs::path(m_base) / history_key(path)).string();
}

std::vector<HistoryEntry> LocalHistory::entries(const std::string& path) const {
    std::vector<HistoryEntry> out;
    const std::string folder = folder_for(path);
    const std::optional<std::string> text = read_text_file(entries_file(folder));
    if (!text) return out;
    std::istringstream in(*text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const std::size_t a = line.find('\t');
        if (a == std::string::npos) continue;
        const std::size_t b = line.find('\t', a + 1);
        HistoryEntry e;
        try {
            e.time_ms = std::stoll(line.substr(0, a));
            e.size = static_cast<std::size_t>(std::stoull(line.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1)));
        } catch (...) {
            continue;
        }
        if (b != std::string::npos) e.label = line.substr(b + 1);
        e.snapshot = (fs::path(folder) / (std::to_string(e.time_ms) + ".txt")).string();
        std::error_code ec;
        if (!fs::exists(e.snapshot, ec)) continue;
        out.push_back(std::move(e));
    }
    std::sort(out.begin(), out.end(), [](const HistoryEntry& x, const HistoryEntry& y) { return x.time_ms > y.time_ms; });
    return out;
}

bool LocalHistory::write_entries(const std::string& folder, const std::vector<HistoryEntry>& entries) const {
    std::string text;
    std::vector<HistoryEntry> ordered = entries;
    std::sort(ordered.begin(), ordered.end(), [](const HistoryEntry& x, const HistoryEntry& y) { return x.time_ms < y.time_ms; });
    for (const HistoryEntry& e : ordered) text += std::to_string(e.time_ms) + "\t" + std::to_string(e.size) + "\t" + clean_label(e.label) + "\n";
    return write_text_file(entries_file(folder), text);
}

std::optional<std::string> LocalHistory::read(const HistoryEntry& entry) const {
    return read_text_file(entry.snapshot);
}

std::optional<std::string> LocalHistory::latest(const std::string& path) const {
    const std::vector<HistoryEntry> all = entries(path);
    if (all.empty()) return std::nullopt;
    return read(all.front());
}

bool LocalHistory::record(const std::string& path, const std::string& text, const std::string& label) {
    if (path.empty() || text.size() > m_max_bytes) return false;
    std::vector<HistoryEntry> all = entries(path);
    if (!all.empty()) {
        const std::optional<std::string> last = read(all.front());
        if (last && *last == text) return false;
    }
    const std::string folder = folder_for(path);
    std::error_code ec;
    fs::create_directories(folder, ec);
    if (ec) return false;
    write_text_file((fs::path(folder) / "path.txt").string(), normal_path(path) + "\n");

    long long stamp = now_ms();
    if (!all.empty() && stamp <= all.front().time_ms) stamp = all.front().time_ms + 1;
    HistoryEntry e;
    e.time_ms = stamp;
    e.size = text.size();
    e.label = label;
    e.snapshot = (fs::path(folder) / (std::to_string(stamp) + ".txt")).string();
    if (!write_text_file(e.snapshot, text)) return false;
    all.insert(all.begin(), e);
    while (all.size() > m_limit) {
        fs::remove(all.back().snapshot, ec);
        all.pop_back();
    }
    return write_entries(folder, all);
}

bool LocalHistory::remove(const std::string& path, long long time_ms) {
    std::vector<HistoryEntry> all = entries(path);
    const auto it = std::find_if(all.begin(), all.end(), [&](const HistoryEntry& e) { return e.time_ms == time_ms; });
    if (it == all.end()) return false;
    std::error_code ec;
    fs::remove(it->snapshot, ec);
    all.erase(it);
    return write_entries(folder_for(path), all);
}

bool LocalHistory::clear(const std::string& path) {
    std::error_code ec;
    fs::remove_all(folder_for(path), ec);
    return !ec;
}

} // namespace canopy
