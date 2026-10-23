#include <canopy/shell.hpp>
#include <canopy/workspace.hpp>

#include <grove/app.hpp>
#include <grove/bundle.hpp>
#include <grove/runtime/runtime.hpp>

#if defined(__ANDROID__)

#ifndef FIZMO
#define FIZMO
#endif
#ifndef FIZMO_GRAPHICS
#define FIZMO_GRAPHICS
#endif
#include <fizmo/includes.hpp>

#include <cstdlib>
#include <filesystem>
#include <string>

namespace {

namespace fs = std::filesystem;

int show_error(fizmo::android::App& app, const std::string& message) {
    fizmo::android::log(fizmo::android::LogLevel::Error, "canopy", message);
    fizmo::graphics::Renderer renderer(app.surface());
    renderer.bind();
    fizmo::android::Event e;
    while (app.running()) {
        bool redraw = false;
        while (app.poll(e, 250)) {
            if (e.type == fizmo::android::EventType::Back) app.finish();
            redraw = true;
        }
        if (!redraw || !renderer.is_bound()) continue;
        const float k = app.density();
        const fizmo::android::Insets in = app.content_insets();
        const int x = in.left + static_cast<int>(24 * k);
        const int y = in.top + static_cast<int>(32 * k);
        const unsigned w = app.surface().width() > static_cast<unsigned>(x * 2) ? app.surface().width() - static_cast<unsigned>(x * 2) : app.surface().width();
        renderer.begin_frame();
        renderer.clear(fizmo::colors::Color(30, 30, 36));
        renderer.draw_text(x, y, w, static_cast<unsigned>(60 * k), std::string("Canopy couldn't start"), fizmo::text::TextStyle(22.0 * k, fizmo::colors::Color(243, 139, 168)));
        renderer.draw_text(x, y + static_cast<int>(48 * k), w, app.surface().height(), message, fizmo::text::TextStyle(15.0 * k, fizmo::colors::Color(205, 214, 244)));
        renderer.present();
    }
    return 1;
}

void set_home(const std::string& data) {
    if (data.empty()) return;
    std::error_code ec;
    const fs::path config = fs::path(data) / "config";
    fs::create_directories(config, ec);
    setenv("HOME", data.c_str(), 1);
    setenv("XDG_CONFIG_HOME", config.string().c_str(), 1);
    setenv("XDG_DATA_HOME", (fs::path(data) / "share").string().c_str(), 1);
    setenv("XDG_CACHE_HOME", (fs::path(data) / "cache").string().c_str(), 1);
    setenv("TMPDIR", (fs::path(data) / "cache").string().c_str(), 1);
}

std::string first_folder(const std::string& data) {
    std::error_code ec;
    for (const std::string& folder : canopy::recent_folders()) {
        if (fs::is_directory(folder, ec) && (fs::status(folder, ec).permissions() & fs::perms::owner_read) != fs::perms::none) {
            fs::directory_iterator it(folder, ec);
            if (!ec) return folder;
        }
    }
    const fs::path projects = fs::path(data.empty() ? std::string("/data/local/tmp") : data) / "Projects";
    fs::create_directories(projects, ec);
    return projects.string();
}

} // namespace

int fizmo_android_main(fizmo::android::App& app) {
    fizmo::android::redirect_stdio("canopy");
    const std::string data = app.internal_data_path();
    set_home(data);

    const std::optional<std::string> bundle_text = app.assets().read("grove/app.bundle");
    if (!bundle_text) return show_error(app, "this APK has no grove/app.bundle asset; build it again with grove build");
    std::optional<grove::Bundle> bundle = grove::parse_bundle(*bundle_text);
    if (!bundle) return show_error(app, "grove/app.bundle is damaged; build Canopy again");

    grove::AppLoadOptions options;
    options.root = data.empty() ? std::string("/data/local/tmp") : data;
    options.cache_dir = app.cache_path();
    options.module_dir = app.native_library_dir();

    grove::LoadedApp loaded;
    std::string error;
    if (!grove::load_app(*bundle, loaded, error, options)) return show_error(app, error);

    const canopy::Workspace workspace = canopy::open_workspace(first_folder(data));
    return canopy::run_shell_android(app, loaded.project, workspace);
}

#endif // __ANDROID__
