#include <canopy/developer.hpp>

#include <grove/markup/events.hpp>
#include <grove/version.hpp>

#ifndef FIZMO
#define FIZMO
#endif
#ifndef FIZMO_GRAPHICS
#define FIZMO_GRAPHICS
#endif
#include <fizmo/includes.hpp>

#include <canopy/version.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/utsname.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/time.h>
#endif
#endif

namespace canopy {

namespace {

std::string fixed(double value, int digits) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", digits, value);
    return buf;
}

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

std::string quoted_value(const std::string& line) {
    const std::size_t eq = line.find('=');
    if (eq == std::string::npos) return {};
    std::string v = line.substr(eq + 1);
    if (v.size() >= 2 && (v.front() == '"' || v.front() == '\'') && v.back() == v.front()) v = v.substr(1, v.size() - 2);
    return v;
}

#if defined(_WIN32)
std::string registry_text(const char* value) {
    HKEY key = nullptr;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &key) != ERROR_SUCCESS) return {};
    char buf[256] = {};
    DWORD size = sizeof buf;
    DWORD type = 0;
    std::string out;
    if (RegQueryValueExA(key, value, nullptr, &type, reinterpret_cast<LPBYTE>(buf), &size) == ERROR_SUCCESS && type == REG_SZ) out = buf;
    RegCloseKey(key);
    return out;
}
#endif

std::string percent(double value) { return fixed(value, value < 10.0 ? 1 : 0) + "%"; }

} // namespace

std::string format_byte_size(std::uint64_t bytes) {
    static const char* const units[] = { "B", "KB", "MB", "GB", "TB" };
    double v = static_cast<double>(bytes);
    std::size_t u = 0;
    while (v >= 1024.0 && u + 1 < sizeof units / sizeof units[0]) {
        v /= 1024.0;
        ++u;
    }
    return fixed(v, u == 0 ? 0 : v < 10.0 ? 2 : 1) + " " + units[u];
}

std::string format_uptime(double seconds) {
    const long long s = static_cast<long long>(seconds);
    const long long days = s / 86400;
    const long long hours = (s % 86400) / 3600;
    const long long minutes = (s % 3600) / 60;
    std::string out;
    if (days > 0) out += std::to_string(days) + "d ";
    if (days > 0 || hours > 0) out += std::to_string(hours) + "h ";
    out += std::to_string(minutes) + "m";
    return out;
}

std::string size_with_share(double part, double whole, const std::string& unit) {
    std::string out = fixed(part, 0) + unit;
    if (whole > 0.0) out += " (" + fixed(part * 100.0 / whole, 0) + "%)";
    return out;
}

std::string compiler_description() {
#if defined(__clang__)
    return "Clang " + std::to_string(__clang_major__) + "." + std::to_string(__clang_minor__) + "." + std::to_string(__clang_patchlevel__);
#elif defined(__GNUC__)
    return "GCC " + std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__) + "." + std::to_string(__GNUC_PATCHLEVEL__);
#elif defined(_MSC_VER)
    return "MSVC " + std::to_string(_MSC_VER);
#else
    return "unknown compiler";
#endif
}

OsDetails os_details() {
    OsDetails d;
#if defined(_WIN32)
    d.name = registry_text("ProductName");
    if (d.name.empty()) d.name = "Windows";
    d.version = registry_text("DisplayVersion");
    const std::string build = registry_text("CurrentBuildNumber");
    if (!build.empty()) d.kernel = "build " + build;
    SYSTEM_INFO info{};
    GetNativeSystemInfo(&info);
    switch (info.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: d.architecture = "x86_64"; break;
        case PROCESSOR_ARCHITECTURE_ARM64: d.architecture = "arm64"; break;
        case PROCESSOR_ARCHITECTURE_INTEL: d.architecture = "x86"; break;
        default: d.architecture = "unknown"; break;
    }
    char host[256] = {};
    DWORD size = sizeof host;
    if (GetComputerNameA(host, &size)) d.host = host;
    d.desktop = "Windows shell";
    d.uptime_seconds = static_cast<double>(GetTickCount64()) / 1000.0;
#else
    struct utsname u {};
    if (uname(&u) == 0) {
        d.kernel = std::string(u.sysname) + " " + u.release;
        d.architecture = u.machine;
        d.host = u.nodename;
    }
#if defined(__APPLE__)
    char version[64] = {};
    std::size_t size = sizeof version;
    d.name = "macOS";
    if (sysctlbyname("kern.osproductversion", version, &size, nullptr, 0) == 0) d.version = version;
    d.desktop = "Aqua";
    struct timeval boot {};
    size = sizeof boot;
    if (sysctlbyname("kern.boottime", &boot, &size, nullptr, 0) == 0) d.uptime_seconds = static_cast<double>(std::time(nullptr) - boot.tv_sec);
#else
    std::ifstream release("/etc/os-release");
    std::string line;
    while (std::getline(release, line)) {
        if (line.rfind("PRETTY_NAME=", 0) == 0) d.name = quoted_value(line);
        else if (line.rfind("VERSION_ID=", 0) == 0) d.version = quoted_value(line);
    }
    if (d.name.empty()) d.name = "Linux";
    std::ifstream uptime("/proc/uptime");
    uptime >> d.uptime_seconds;
    d.desktop = env("XDG_CURRENT_DESKTOP");
    d.session = env("XDG_SESSION_TYPE");
    if (d.session.empty()) d.session = !env("WAYLAND_DISPLAY").empty() ? "wayland" : !env("DISPLAY").empty() ? "x11" : std::string();
#endif
#endif
    return d;
}

struct DeveloperPage::Trackers {
    fizmo::system::CPUTracking cpu{ std::chrono::milliseconds(1000), 8 };
    fizmo::system::RAMTracking ram{ std::chrono::milliseconds(1000), 8 };
    fizmo::system::GPUTracking gpu{ std::chrono::milliseconds(1000), 8 };
};

DeveloperPage::DeveloperPage(grove::Document& doc) : m_doc(doc) {}

DeveloperPage::~DeveloperPage() { stop(); }

void DeveloperPage::card(grove::Node& parent, const std::string& title, const std::vector<std::pair<std::string, std::string>>& rows) {
    grove::Node* box = m_doc.create_element("column");
    box->set_attribute("class", "dev-card");
    m_doc.append_child(parent, *box);
    grove::Node* head = m_doc.create_element("div");
    head->set_attribute("class", "dev-card-title");
    head->set_inner_text(title);
    m_doc.append_child(*box, *head);
    for (const auto& [key, label] : rows) {
        grove::Node* row = m_doc.create_element("row");
        row->set_attribute("class", "dev-row");
        m_doc.append_child(*box, *row);
        grove::Node* name = m_doc.create_element("text");
        name->set_attribute("class", "dev-label");
        name->set_inner_text(label);
        m_doc.append_child(*row, *name);
        grove::Node* value = m_doc.create_element("column");
        value->set_attribute("class", "dev-value-box");
        m_doc.append_child(*row, *value);
        grove::Node* text = m_doc.create_element("text");
        text->set_attribute("class", "dev-value");
        text->set_inner_text("\xE2\x80\xA6");
        m_doc.append_child(*value, *text);
        m_values[key] = text;
        if (key.size() > 4 && key.compare(key.size() - 4, 4, "-use") == 0) {
            grove::Node* track = m_doc.create_element("div");
            track->set_attribute("class", "dev-bar");
            m_doc.append_child(*value, *track);
            grove::Node* fill = m_doc.create_element("div");
            fill->set_attribute("class", "dev-bar-fill");
            fill->set_attribute("style", "width: 0%");
            m_doc.append_child(*track, *fill);
            m_bars[key] = fill;
        }
    }
}

void DeveloperPage::build(grove::Node& view) {
    stop();
    m_view = &view;
    m_os = os_details();
    m_opened = std::chrono::steady_clock::now();
    grove::Node* body = m_doc.create_element("column");
    body->set_attribute("class", "dev-body");
    m_doc.append_child(view, *body);
    grove::Node* heading = m_doc.create_element("h2");
    heading->set_attribute("class", "settings-heading");
    heading->set_inner_text("Developer");
    m_doc.append_child(*body, *heading);
    grove::Node* note = m_doc.create_element("p");
    note->set_attribute("class", "settings-note");
    note->set_inner_text("Live numbers for this window and this computer, refreshed every second while this tab is open. Canopy only draws a frame when something changes, so frames per second is how often it actually painted.");
    m_doc.append_child(*body, *note);
    grove::Node* grid = m_doc.create_element("row");
    grid->set_attribute("class", "dev-grid");
    m_doc.append_child(*body, *grid);
    grove::Node* left = m_doc.create_element("column");
    left->set_attribute("class", "dev-column");
    m_doc.append_child(*grid, *left);
    grove::Node* right = m_doc.create_element("column");
    right->set_attribute("class", "dev-column");
    m_doc.append_child(*grid, *right);

    card(*left, "Window", { { "win-size", "Size" }, { "win-position", "Position" }, { "win-monitor", "Monitor" }, { "win-scale", "Scale" } });
    card(*left, "Frames", { { "frame-ms", "Last frame" }, { "frame-split", "Layout / draw / present" }, { "frame-fps", "Frames painted" }, { "frame-range", "Between frames" }, { "frame-ops", "Draw operations" }, { "frame-renderer", "Renderer" } });
    card(*left, "System", { { "os-name", "OS" }, { "os-kernel", "Kernel" }, { "os-arch", "Architecture" }, { "os-desktop", "Desktop" }, { "os-host", "Computer name" }, { "os-uptime", "Up for" } });
    card(*left, "Canopy", { { "app-version", "Canopy" }, { "app-grove", "Grove" }, { "app-compiler", "Built with" }, { "app-build", "Build" } });
    card(*right, "CPU", { { "cpu-model", "Processor" }, { "cpu-cores", "Cores" }, { "cpu-use", "Whole computer" }, { "cpu-proc-use", "Canopy" }, { "cpu-threads", "Canopy threads" }, { "cpu-clock", "Clock" }, { "cpu-temp", "Temperature" } });
    card(*right, "Memory", { { "ram-use", "Whole computer" }, { "ram-proc", "Canopy" }, { "ram-peak", "Canopy at most" }, { "ram-swap", "Swap" }, { "ram-modules", "Modules" } });
    card(*right, "GPU", { { "gpu-name", "Graphics card" }, { "gpu-driver", "Driver" }, { "gpu-use", "Usage" }, { "gpu-proc", "Canopy's share" }, { "gpu-mem-use", "Video memory" }, { "gpu-temp", "Temperature" }, { "gpu-clock", "Clock" }, { "gpu-power", "Power" } });

    set("app-version", CANOPY_VERSION_STRING);
    set("app-grove", GROVE_VERSION_STRING);
    set("app-compiler", compiler_description());
#if defined(NDEBUG)
    set("app-build", "release");
#else
    set("app-build", "debug");
#endif
    std::string os = m_os.name;
    if (!m_os.version.empty() && os.find(m_os.version) == std::string::npos) os += " " + m_os.version;
    set("os-name", os);
    set("os-kernel", m_os.kernel.empty() ? "unknown" : m_os.kernel);
    set("os-arch", m_os.architecture.empty() ? "unknown" : m_os.architecture);
    std::string desktop = m_os.desktop;
    if (!m_os.session.empty()) desktop += (desktop.empty() ? "" : ", ") + m_os.session;
    set("os-desktop", desktop.empty() ? "unknown" : desktop);
    set("os-host", m_os.host.empty() ? "unknown" : m_os.host);
    start_tracking();
    refresh();
}

void DeveloperPage::start_tracking() {
    m_trackers = std::make_unique<Trackers>();
    grove::DocumentHost& host = m_doc.host();
    if (host.native_window && host.main_window) {
        if (void* r = host.native_window(*host.main_window, "renderer")) {
            auto& renderer = *static_cast<fizmo::graphics::Renderer*>(r);
            m_trackers->cpu.attach(renderer);
            m_trackers->gpu.attach(renderer);
        }
    }
    m_trackers->cpu.start();
    m_trackers->ram.start();
    m_trackers->gpu.start();
    const fizmo::system::CPUInfo& cpu = m_trackers->cpu.info();
    set("cpu-model", cpu.model.empty() ? "unknown" : cpu.model);
    std::string cores = std::to_string(cpu.logical_cores) + " threads";
    if (cpu.physical_cores > 0) cores = std::to_string(cpu.physical_cores) + " cores, " + cores;
    set("cpu-cores", cores);
    const fizmo::system::RAMInfo& ram = m_trackers->ram.info();
    std::string modules;
    for (const auto& m : ram.modules) {
        if (m.size_bytes == 0) continue;
        if (!modules.empty()) modules += ", ";
        modules += format_byte_size(m.size_bytes);
        if (!m.type.empty()) modules += " " + m.type;
        if (m.configured_speed_mts || m.speed_mts) modules += " " + std::to_string(m.configured_speed_mts ? m.configured_speed_mts : m.speed_mts) + " MT/s";
    }
    set("ram-modules", modules.empty() ? "not reported" : modules);
    const auto& adapters = m_trackers->gpu.adapters();
    if (!adapters.empty()) {
        std::string driver = adapters.front().driver;
        set("gpu-driver", driver.empty() ? "not reported" : driver);
    } else {
        set("gpu-driver", "not reported");
    }
}

void DeveloperPage::stop() {
    if (m_trackers) {
        m_trackers->cpu.detach();
        m_trackers->gpu.detach();
        m_trackers->cpu.stop();
        m_trackers->ram.stop();
        m_trackers->gpu.stop();
        m_trackers.reset();
    }
    m_view = nullptr;
    m_values.clear();
    m_bars.clear();
    m_shown.clear();
}

void DeveloperPage::set(const std::string& key, const std::string& value) {
    const auto it = m_values.find(key);
    if (it == m_values.end()) return;
    std::string& shown = m_shown[key];
    if (shown == value) return;
    shown = value;
    it->second->set_inner_text(value);
    m_doc.touch();
}

void DeveloperPage::bar(const std::string& key, double value) {
    const auto it = m_bars.find(key);
    if (it == m_bars.end()) return;
    const std::string style = "width: " + fixed(std::clamp(value, 0.0, 100.0), 1) + "%";
    if (it->second->attribute_or("style", "") == style) return;
    it->second->set_attribute("style", style);
    m_doc.touch();
}

void DeveloperPage::refresh() {
    if (!m_view || !m_doc.is_attached(*m_view)) return;
    grove::DocumentHost& host = m_doc.host();
    grove::WindowInfo w;
    if (host.window_info && host.main_window && host.window_info(*host.main_window, w)) {
        std::string size = fixed(w.width, 0) + " \xC3\x97 " + fixed(w.height, 0) + " px";
        if (w.monitor_width > 0 && w.monitor_height > 0) size += " (" + fixed(w.width * 100.0 / w.monitor_width, 0) + "% \xC3\x97 " + fixed(w.height * 100.0 / w.monitor_height, 0) + "% of the monitor)";
        set("win-size", size);
        set("win-position", std::to_string(w.x) + ", " + std::to_string(w.y));
        std::string monitor = w.monitor.empty() ? std::string("monitor") : w.monitor;
        monitor += ", " + fixed(w.monitor_width, 0) + " \xC3\x97 " + fixed(w.monitor_height, 0) + " px";
        if (w.refresh_rate > 0.0) monitor += " at " + fixed(w.refresh_rate, w.refresh_rate == std::floor(w.refresh_rate) ? 0 : 2) + " Hz";
        set("win-monitor", monitor);
        set("win-scale", fixed(w.dpi_scale * 100.0, 0) + "%");
        set("frame-ms", fixed(w.frame_ms, 2) + " ms");
        set("frame-split", fixed(w.layout_ms, 2) + " / " + fixed(w.draw_ms, 2) + " / " + fixed(w.present_ms, 2) + " ms");
        set("frame-fps", fixed(w.frames_per_second, 0) + " per second (" + std::to_string(w.frames) + " since it opened)");
        set("frame-ops", std::to_string(w.draw_ops));
        set("frame-renderer", w.renderer.empty() ? "unknown" : w.renderer);
    }
    if (!m_trackers) return;
    if (m_trackers->cpu.has_sample()) {
        const fizmo::system::CPUSample c = m_trackers->cpu.latest();
        set("cpu-use", percent(c.usage_percent));
        bar("cpu-use", c.usage_percent);
        set("cpu-proc-use", percent(c.process_percent) + " of the whole CPU (" + percent(c.process_core_percent) + " of one core)");
        bar("cpu-proc-use", c.process_percent);
        set("cpu-threads", std::to_string(c.process_threads));
        std::string clock = c.frequency_mhz ? fixed(*c.frequency_mhz / 1000.0, 2) + " GHz" : std::string("not reported");
        if (c.max_core_frequency_mhz) clock += " (fastest core " + fixed(*c.max_core_frequency_mhz / 1000.0, 2) + " GHz)";
        set("cpu-clock", clock);
        set("cpu-temp", c.temperature_c ? fixed(*c.temperature_c, 0) + " \xC2\xB0" "C" : std::string("not reported"));
        if (c.frames.frames > 0) set("frame-range", fixed(c.frames.frame_ms_min, 1) + " to " + fixed(c.frames.frame_ms_max, 1) + " ms (each frame took " + fixed(c.frames.cpu_ms, 2) + " ms of CPU time)");
        else set("frame-range", "no frames (nothing changed)");
    }
    if (m_trackers->ram.has_sample()) {
        const fizmo::system::RAMSample r = m_trackers->ram.latest();
        set("ram-use", format_byte_size(r.used) + " of " + format_byte_size(r.total) + " (" + percent(r.usage_percent) + ")");
        bar("ram-use", r.usage_percent);
        set("ram-proc", format_byte_size(r.process_resident) + " (" + percent(r.process_percent) + " of the computer's memory)");
        set("ram-peak", format_byte_size(r.process_peak_resident));
        set("ram-swap", r.swap_total ? format_byte_size(r.swap_used) + " of " + format_byte_size(r.swap_total) : std::string("none"));
    }
    if (m_trackers->gpu.has_sample()) {
        const fizmo::system::GPUSample g = m_trackers->gpu.latest();
        const fizmo::system::GPUAdapterSample* a = g.active();
        if (!a) a = g.primary();
        std::string name = !g.device_name.empty() ? g.device_name : a ? a->name : std::string();
        if (a && !a->vendor.empty() && name.find(a->vendor) == std::string::npos) name = a->vendor + " " + name;
        if (!g.backend.empty()) name += " (" + g.backend + ")";
        set("gpu-name", name.empty() ? "not found" : name);
        if (a && a->utilization_percent) {
            set("gpu-use", percent(*a->utilization_percent));
            bar("gpu-use", *a->utilization_percent);
        } else {
            set("gpu-use", "not reported by this driver");
        }
        set("gpu-proc", a && a->process_percent ? percent(*a->process_percent) : std::string("not reported"));
        if (a && a->memory_used && a->memory_total && *a->memory_total > 0) {
            const double share = static_cast<double>(*a->memory_used) * 100.0 / static_cast<double>(*a->memory_total);
            set("gpu-mem-use", format_byte_size(*a->memory_used) + " of " + format_byte_size(*a->memory_total) + (a->process_memory ? ", Canopy " + format_byte_size(*a->process_memory) : std::string()));
            bar("gpu-mem-use", share);
        } else if (a && a->memory_total) {
            set("gpu-mem-use", format_byte_size(*a->memory_total) + " total");
        } else {
            set("gpu-mem-use", "not reported");
        }
        set("gpu-temp", a && a->temperature_c ? fixed(*a->temperature_c, 0) + " \xC2\xB0" "C" : std::string("not reported"));
        std::string clock = a && a->clock_mhz ? fixed(*a->clock_mhz, 0) + " MHz" : std::string("not reported");
        if (a && a->max_clock_mhz && a->clock_mhz) clock += " of " + fixed(*a->max_clock_mhz, 0) + " MHz";
        set("gpu-clock", clock);
        set("gpu-power", a && a->power_watts ? fixed(*a->power_watts, 1) + " W" + (a->power_limit_watts ? " of " + fixed(*a->power_limit_watts, 0) + " W" : std::string()) : std::string("not reported"));
    }
    set("os-uptime", format_uptime(m_os.uptime_seconds + std::chrono::duration<double>(std::chrono::steady_clock::now() - m_opened).count()));
}

} // namespace canopy
