#include <canopy/build.hpp>

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace canopy {

std::string format_duration_us(std::int64_t microseconds) {
    if (microseconds < 0) microseconds = 0;
    const std::int64_t hours = microseconds / 3600000000LL;
    microseconds %= 3600000000LL;
    const std::int64_t minutes = microseconds / 60000000LL;
    microseconds %= 60000000LL;
    const std::int64_t seconds = microseconds / 1000000LL;
    microseconds %= 1000000LL;
    const std::int64_t millis = microseconds / 1000LL;
    const std::int64_t micros = microseconds % 1000LL;
    std::string out;
    const auto part = [&](std::int64_t value, const char* unit) {
        if (value == 0) return;
        if (!out.empty()) out += ' ';
        out += std::to_string(value);
        out += ' ';
        out += unit;
    };
    part(hours, "h");
    part(minutes, "min");
    part(seconds, "s");
    part(millis, "ms");
    part(micros, "\xC2\xB5s");
    return out.empty() ? std::string("0 \xC2\xB5s") : out;
}

std::string format_bytes(std::uint64_t bytes) {
    if (bytes < 1024) return std::to_string(bytes) + (bytes == 1 ? " byte" : " bytes");
    const char* units[] = { "KB", "MB", "GB", "TB" };
    double value = static_cast<double>(bytes) / 1024.0;
    int unit = 0;
    while (value >= 1024.0 && unit < 3) {
        value /= 1024.0;
        ++unit;
    }
    char text[32];
    std::snprintf(text, sizeof(text), value >= 100.0 ? "%.0f %s" : "%.1f %s", value, units[unit]);
    return text;
}

std::string run_stats_summary(const RunStats& stats) {
    if (!stats.measured) return std::string();
    std::string out = "Ran for " + format_duration_us(stats.wall_us);
    out += " \xC2\xB7 CPU " + format_duration_us(stats.user_us + stats.system_us);
    if (stats.peak_bytes) out += std::string(" \xC2\xB7 ") + (stats.peak_is_commit ? "peak memory (committed) " : "peak memory ") + format_bytes(stats.peak_bytes);
    out += " \xC2\xB7 exit code " + std::to_string(stats.exit_code);
    return out;
}

namespace {

std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

#if defined(_WIN32)

std::wstring widen(const std::string& text) {
    if (text.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n);
    return out;
}

std::int64_t filetime_us(const FILETIME& t) {
    ULARGE_INTEGER v;
    v.LowPart = t.dwLowDateTime;
    v.HighPart = t.dwHighDateTime;
    return static_cast<std::int64_t>(v.QuadPart / 10ULL);
}

#endif

} // namespace

int run_command_measured(const std::vector<std::string>& command, const std::string& working_dir, std::string& output, RunStats& stats, const std::function<void(const std::string&)>& on_output) {
    stats = RunStats{};
    stats.command = command_line(command);
    if (command.empty()) return -1;
    const auto emit = [&](const char* data, std::size_t n) {
        std::string piece(data, n);
        output += piece;
        if (on_output) on_output(piece);
    };
    stats.started_ms = now_ms();
    const auto start = std::chrono::steady_clock::now();

#if defined(_WIN32)
    const std::string line = "\"" + (working_dir.empty() ? std::string() : "cd /d " + quote_argument(working_dir) + " && ") + command_line(command) + " 2>&1\"";
    std::wstring full = widen("cmd.exe /c " + line);

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE read_end = nullptr;
    HANDLE write_end = nullptr;
    if (!CreatePipe(&read_end, &write_end, &sa, 0)) return -1;
    SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = write_end;
    si.hStdError = write_end;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    const DWORD flags = CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | (GetConsoleWindow() ? 0 : CREATE_NO_WINDOW);
    if (!CreateProcessW(nullptr, full.data(), nullptr, nullptr, TRUE, flags, nullptr, nullptr, &si, &pi)) {
        CloseHandle(read_end);
        CloseHandle(write_end);
        return -1;
    }
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    const bool in_job = job && AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    CloseHandle(write_end);

    char buffer[4096];
    DWORD got = 0;
    while (ReadFile(read_end, buffer, sizeof(buffer), &got, nullptr) && got > 0) emit(buffer, got);
    CloseHandle(read_end);
    WaitForSingleObject(pi.hProcess, INFINITE);
    stats.wall_us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();

    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    if (in_job) {
        JOBOBJECT_BASIC_AND_IO_ACCOUNTING_INFORMATION acct{};
        if (QueryInformationJobObject(job, JobObjectBasicAndIoAccountingInformation, &acct, sizeof(acct), nullptr)) {
            stats.user_us = acct.BasicInfo.TotalUserTime.QuadPart / 10;
            stats.system_us = acct.BasicInfo.TotalKernelTime.QuadPart / 10;
            stats.minor_faults = acct.BasicInfo.TotalPageFaultCount;
            stats.read_bytes = acct.IoInfo.ReadTransferCount;
            stats.written_bytes = acct.IoInfo.WriteTransferCount;
        }
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        if (QueryInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits), nullptr)) {
            stats.peak_bytes = static_cast<std::uint64_t>(limits.PeakProcessMemoryUsed);
            stats.peak_is_commit = true;
        }
    } else {
        FILETIME created, exited, kernel, user;
        if (GetProcessTimes(pi.hProcess, &created, &exited, &kernel, &user)) {
            stats.user_us = filetime_us(user);
            stats.system_us = filetime_us(kernel);
        }
    }
    if (job) CloseHandle(job);
    CloseHandle(pi.hProcess);
    stats.exit_code = static_cast<int>(code);
    stats.measured = true;
    return stats.exit_code;
#else
    std::string line = "exec " + command_line(command) + " 2>&1";
    if (!working_dir.empty()) line = "cd " + quote_argument(working_dir) + " && " + line;
    int fds[2];
    if (pipe(fds) != 0) return -1;
    const pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return -1;
    }
    if (pid == 0) {
        close(fds[0]);
        dup2(fds[1], STDOUT_FILENO);
        dup2(fds[1], STDERR_FILENO);
        if (fds[1] != STDOUT_FILENO && fds[1] != STDERR_FILENO) close(fds[1]);
        execl("/bin/sh", "sh", "-c", line.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    close(fds[1]);
    char buffer[4096];
    for (;;) {
        const ssize_t n = read(fds[0], buffer, sizeof(buffer));
        if (n > 0) emit(buffer, static_cast<std::size_t>(n));
        else if (n < 0 && errno == EINTR) continue;
        else break;
    }
    close(fds[0]);
    int status = 0;
    struct rusage usage {};
    pid_t done = -1;
    do {
        done = wait4(pid, &status, 0, &usage);
    } while (done < 0 && errno == EINTR);
    stats.wall_us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    if (done < 0) return -1;
    stats.user_us = static_cast<std::int64_t>(usage.ru_utime.tv_sec) * 1000000LL + usage.ru_utime.tv_usec;
    stats.system_us = static_cast<std::int64_t>(usage.ru_stime.tv_sec) * 1000000LL + usage.ru_stime.tv_usec;
#if defined(__APPLE__)
    stats.peak_bytes = static_cast<std::uint64_t>(usage.ru_maxrss);
#else
    stats.peak_bytes = static_cast<std::uint64_t>(usage.ru_maxrss) * 1024ULL;
#endif
    stats.minor_faults = static_cast<std::uint64_t>(usage.ru_minflt);
    stats.major_faults = static_cast<std::uint64_t>(usage.ru_majflt);
    stats.voluntary_switches = static_cast<std::uint64_t>(usage.ru_nvcsw);
    stats.involuntary_switches = static_cast<std::uint64_t>(usage.ru_nivcsw);
    stats.read_bytes = static_cast<std::uint64_t>(usage.ru_inblock) * 512ULL;
    stats.written_bytes = static_cast<std::uint64_t>(usage.ru_oublock) * 512ULL;
    if (WIFEXITED(status)) stats.exit_code = WEXITSTATUS(status);
    else stats.exit_code = 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    stats.measured = true;
    return stats.exit_code;
#endif
}

} // namespace canopy
