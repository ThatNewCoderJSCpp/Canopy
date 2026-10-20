#ifndef CANOPY_TERMINAL_HPP
#define CANOPY_TERMINAL_HPP

#include "settings.hpp"

#include <memory>
#include <optional>
#include <string>

namespace canopy {

struct TerminalOptions {
    std::string folder;
    std::string shell;
    unsigned columns = 120;
    unsigned rows = 30;
    bool canopy_prompt = true;
};

class TerminalProcess {
public:
    TerminalProcess();
    ~TerminalProcess();
    TerminalProcess(const TerminalProcess&) = delete;
    TerminalProcess& operator=(const TerminalProcess&) = delete;

    bool start(const TerminalOptions& options);
    bool running();
    std::optional<int> exit_code();
    std::string read();
    bool write(const std::string& text);
    bool send_line(const std::string& line);
    bool resize(unsigned columns, unsigned rows);
    bool pseudo_console() const noexcept { return m_pseudo; }
    void interrupt();
    void end_of_input();
    void stop();
    const std::string& error() const noexcept { return m_error; }
    const std::string& shell() const noexcept { return m_shell; }

private:
    struct Native;
    std::unique_ptr<Native> m_native;
    std::string m_error;
    std::string m_shell;
    bool m_pseudo = true;
};

struct TerminalScreen {
    std::string text;
    std::string pending;
};

void terminal_feed(TerminalScreen& screen, const std::string& data);
std::string terminal_last_line(const TerminalScreen& screen);
std::string terminal_finished_lines(const TerminalScreen& screen);
void terminal_trim(TerminalScreen& screen, std::size_t keep);

std::string terminal_shell(const Settings& settings);
std::string terminal_prompt_preview(const std::string& folder);

} // namespace canopy

#endif // CANOPY_TERMINAL_HPP
