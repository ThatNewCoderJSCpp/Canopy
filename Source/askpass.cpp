#include "canopy/askpass.hpp"
#include "canopy/github_auth.hpp"

#include <grove/input/input.hpp>
#include <grove/project.hpp>
#include <grove/runtime/runtime.hpp>

#include <algorithm>
#include <cctype>
#include <iostream>

namespace canopy {

namespace {

const char* k_askpass_source = R"(<!grove>
<grove>
    <head>
        <title>Git</title>
        <style>
            body {
                size: 480px 200px;
                background: #1e1e2e;
                color: #cdd6f4;
                accent-color: #89b4fa;
                font-size: 13px;
                padding: 18px 20px;
            }
            #askpass { gap: 10px; }
            #askpass-title { font-weight: bold; font-size: 15px; margin: 0 0 4px 0; }
            #askpass-prompt { color: #9399b2; margin: 0 0 6px 0; }
            #askpass-hint { color: #9399b2; font-size: 11px; margin: 6px 0 0 0; }
            #askpass-answer { width: 100%; margin: 0; padding: 5px 8px; }
            #askpass-buttons { gap: 8px; justify-content: end; }
            #askpass-buttons button { margin: 0; padding: 4px 16px; }
            #askpass-ok { background: #89b4fa; color: #11111b; }
        </style>
    </head>
    <body title="Git">
        <column id="askpass">
            <p id="askpass-title">Git</p>
            <p id="askpass-prompt"></p>
            <input_string id="askpass-answer" autofocus />
            <p id="askpass-hint">Canopy hands this to Git and doesn't keep it.</p>
            <row id="askpass-buttons">
                <button id="askpass-cancel">Cancel</button>
                <button id="askpass-ok">OK</button>
            </row>
        </column>
    </body>
</grove>
)";

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

bool askpass_secret(const std::string& prompt) {
    const std::string p = lower(prompt);
    return p.find("password") != std::string::npos || p.find("passphrase") != std::string::npos || p.find("token") != std::string::npos || p.find("pin:") != std::string::npos || p.find("pin for") != std::string::npos;
}

std::string askpass_title(const std::string& prompt) {
    const std::string p = lower(prompt);
    if (p.find("passphrase") != std::string::npos) return "Unlock your SSH key";
    if (p.find("username") != std::string::npos) return "Git needs your username";
    if (p.find("password") != std::string::npos || p.find("token") != std::string::npos) return "Git needs your password or token";
    if (p.find("yes/no") != std::string::npos || p.find("fingerprint") != std::string::npos) return "Trust this server?";
    return "Git is asking";
}

int run_askpass(const std::string& prompt_in) {
    std::string prompt = prompt_in;
    while (!prompt.empty() && (prompt.back() == ' ' || prompt.back() == '\n' || prompt.back() == '\r')) prompt.pop_back();
    if (prompt.empty()) prompt = "Password:";
    if (const std::string saved = github_askpass_answer(prompt); !saved.empty()) {
        std::cout << saved << '\n';
        std::cout.flush();
        return 0;
    }
    grove::Project project = grove::load_project_from_source(k_askpass_source, "askpass.grove", ".");
    if (!project.diagnostics.ok()) {
        project.diagnostics.print(std::cerr);
        return 1;
    }
    grove::Document& doc = project.document;
    grove::Node* answer = doc.find_by_id("askpass-answer");
    if (!answer) return 1;
    if (grove::Node* title = doc.find_by_id("askpass-title")) title->set_inner_text(askpass_title(prompt));
    if (grove::Node* text = doc.find_by_id("askpass-prompt")) text->set_inner_text(prompt);
    if (grove::Node* body = doc.first_by_tag("body")) body->set_attribute("title", askpass_title(prompt) + " \xE2\x80\x94 Canopy");
    if (askpass_secret(prompt)) answer->set_attribute("masked", "true");
    if (grove::Node* hint = doc.find_by_id("askpass-hint"); hint && lower(prompt).find("github.com") != std::string::npos && lower(prompt).find("password") != std::string::npos) {
        hint->set_inner_text("GitHub doesn't take account passwords here: paste a personal access token (github.com/settings/tokens), or cancel and use More > Sign in to GitHub in the Git tab so Canopy remembers you.");
    }
    bool answered = false;
    std::string value;
    auto finish = [&](bool ok) {
        if (ok) {
            value = grove::input_string(*answer).value_or("");
            answered = true;
        }
        doc.events().request_quit();
    };
    if (grove::Node* ok = doc.find_by_id("askpass-ok")) doc.events().add_listener(ok, "click", [&](grove::Event&) { finish(true); });
    if (grove::Node* cancel = doc.find_by_id("askpass-cancel")) doc.events().add_listener(cancel, "click", [&](grove::Event&) { finish(false); });
    doc.events().add_listener(answer, "key_down", [&](grove::Event& e) {
        if (e.key == "Enter") {
            e.default_prevented = true;
            value = grove::input_string(*answer).value_or("");
            answered = true;
            doc.events().request_quit();
        } else if (e.key == "Escape") {
            finish(false);
        }
    });
    if (grove::Node* body = doc.first_by_tag("body")) {
        doc.events().add_listener(body, "key_down", [&](grove::Event& e) {
            if (e.key == "Escape") finish(false);
        });
    }
    grove::runtime::RunOptions options;
    options.scripts = false;
    grove::runtime::run(project, options);
    if (!answered) return 1;
    std::cout << value << '\n';
    std::cout.flush();
    return 0;
}

} // namespace canopy
