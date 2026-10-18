# Canopy

Canopy is a code editor built with [Grove](https://github.com/ThatNewCoderJSCpp/Grove), on top of [Fizmo](https://github.com/ThatNewCoderJSCpp/Fizmo) and [Walnut](https://github.com/ThatNewCoderJSCpp/Walnut).

A grove is where the trees are, and the canopy is the part that covers all of them.

Out of the box it builds **Grove**, **C**, **C++** and **Walnut**, with no setup:

- **Compilers are found for you.** Walnut and Grove ship next to Canopy. For C and C++, Canopy looks on your `PATH`, then in the usual install folders (MSYS2's `ucrt64`/`mingw64`/`clang64` on Windows, Homebrew on macOS). `canopy doctor` says exactly what it found, where, and what to install if something is missing.
- **Settings are a page, and a few small Bark files.** The **Settings** tab (`Ctrl+,`) has a page each for the editor, highlighting, compilers and building, and saves as you change things. Underneath, your settings are Bark files that only hold the lines you changed, so they're easy to read, edit by hand or copy to another computer. `canopy settings` lists every value in effect with the file and line it came from, and a misspelled setting is a warning with a "did you mean" instead of being silently ignored.

```
// ~/.config/canopy/compilers.bark   (Windows: %APPDATA%\Canopy\compilers.bark)
language.cpp {
    standard: "c++20";
}

toolchain {
    cpp: "C:/msys64/ucrt64/bin/clang++.exe";
}
```

| File | |
|---|---|
| `editor.bark` | font, font size, tabs, line numbers and the theme |
| `compilers.bark` | where each compiler lives (`toolchain`) and each language's standard and flags |
| `build.bark` | debug or release, the output folder, saving before a build |
| `highlighting.bark` | your own colors: `highlight` for every language, `highlight.cpp` (or `c`, `walnut`, `grove`, `bark`, `cmake`) for one |
| `keys.bark` | keyboard and mouse shortcuts, like `build: "Ctrl+B";` or `go-to-definition: "Ctrl+Click";` (several with commas, or `"none"`) |
| `themes/*.bark` | your own themes, next to the built-in ones |

`canopy settings --dir` prints the folder. A project can override any of them with the same files in `.canopy/` in its root. An older single `settings.bark` there still works.

- **Themes and colors.** Canopy Dark, Dark Modern, Neon, Amber, Ember, Soft, Ocean, Forest, Mono and Light are built in. The Highlighting page shows each one's colors, and lists every kind of word (keywords, types, namespaces, strings, ...) with a color, **B**/*I*/U/S (bold, italic, underline, strikethrough) toggles and a reset. You can change them for every language or for one. Clicking a color's swatch opens a picker with a color wheel, brightness and alpha sliders, and the color as hex, RGB, RGBA, HSL and HSLA. Pointing at a row boxes everything it colors in a live preview, and clicking a word in the preview finds its row. Changes only show in the preview until you press **Apply**, so the colors you had are never lost while you experiment: **Discard changes** goes back to them, **Save as theme…** names the result and saves it in your `themes/` folder, and **Copy Bark** and **Copy JSON** put it on the clipboard. A theme is a short Bark file:

```
theme {
    name: "Mine";
    background: #101014;
    foreground: #e6e6e6;
}

highlight {
    keyword: #ff4fd8 bold;
    comment: #5c6370 italic;
}
```

## Installing

Download the package for your system from the [releases page](https://github.com/ThatNewCoderJSCpp/Canopy/releases). Every package has Canopy, Grove and Walnut in it; you only need a C/C++ compiler for C, C++ and Walnut (`canopy doctor` tells you which one to install if it can't find one).

| System | Download | |
|---|---|---|
| Windows (x64) | `Canopy-<version>-windows-ucrt64.exe` | installer: Start menu entry, an optional desktop shortcut, `canopy` on your PATH and "Open with Canopy" when you right-click a folder or file. `windows-mingw64` also runs on Windows 7 and 8, `windows-clangarm64` is for ARM PCs. `.zip` and `.7z` are the same files without the installer |
| Fedora, RHEL, openSUSE | `Canopy-<version>-linux-x86_64.rpm` | `sudo dnf install ./Canopy-*.rpm` |
| Ubuntu, Debian, Mint | `Canopy-<version>-linux-x86_64.deb` | `sudo apt install ./Canopy-*.deb` |
| Any Linux | `Canopy-<version>-linux-x86_64.tar.gz` (or `.tar.xz`, `.zip`) | unpack it anywhere, then run `bin/canopy install` |

`arm64` and `i686` packages are there for Linux too.

The installers put Canopy in your applications menu and the `canopy` command on your PATH, so afterwards you open a folder with `canopy .` (it returns straight away, like VS Code's `code .`), or click Canopy in the menu. The `.deb` and `.rpm` install into `/opt/canopy` with `canopy` in `/usr/bin`; removing the package removes the menu entry and command too.

With the `.zip` or a tarball, `canopy install` does the same for just your user:

| | Linux | Windows |
|---|---|---|
| `canopy` command | a link in `~/.local/bin` | Canopy's `bin` folder added to your PATH |
| menu entry | `~/.local/share/applications/canopy.desktop`, with icons | Start menu shortcut |
| `--desktop` | `canopy.desktop` on your desktop | `Canopy.lnk` on your desktop |
| right-click menu | | "Open with Canopy" on folders, folder backgrounds and files |
| file types | `.wal`, `.walh`/`.hwal`, `.grove` and `.bark` get their own icons and open in Canopy | the same, with "Open with" entries; Canopy only becomes the default if nothing else is |

`canopy uninstall` takes it all away again, and `canopy doctor` shows what's installed. Leave out a part with `--no-command`, `--no-menu`, `--no-context-menu` or `--no-file-types`.

## The editor

`canopy [folder]` opens the editor on a folder (the current one by default), and `canopy file.cpp` opens a file in the folder it belongs to. Either way the command returns straight away and the editor keeps running; add `--wait` to keep the terminal attached until it closes.

- **Explorer** on the left, with an icon for each kind of file: **Open Folder…** and **Open File…** use your system's file dialog, folders expand and collapse in place, and **Recent folders** reopens the last ten. **Settings** opens the Settings tab. Saving a settings file by hand applies it straight away too.
- **Editor** in the middle: one tab per open file, with several carets when you want them (Alt+click, Shift+click straight up or down, or Shift+Up/Down), line numbers and highlighting for C, C++, Walnut, Grove, Bark and CMake (`CMakeLists.txt` and `.cmake`). A dot on a tab means unsaved changes, and closing it asks first. In Walnut files, hovering shows what the compiler knows: signatures, initializers, an enum member's underlying type and value (`short int: Color::Green = 5`), and what each keyword and builtin type is ("Walnut keyword", "Walnut builtin type"). Ctrl+click goes to the overload or constructor actually used, opens imported and `#include`d files, and opens Walnut's own library files (read-only copies that Canopy asks `walnut library` for when the library isn't on disk).
- **Viewers** for files that aren't code. Images (PNG, JPEG, GIF, BMP, WebP, TIFF, TGA, QOI, PNM, ICO and SVG) open as pictures with their size and format, zoom in and out or fit the editor, and scroll sideways when they are wider than it. PDFs open as pages, drawn with Poppler's `pdftoppm`, MuPDF's `mutool`, `pdftocairo` or Ghostscript, whichever is installed. A `.tex` file opens with a live preview beside it: Canopy builds it with `latexmk`, `tectonic`, `pdflatex`, `xelatex` or `lualatex`, rebuilds when you save (or press **Build**), underlines LaTeX's errors in the source and lists them in Problems. A file that's part of a bigger document can say `% !TEX root = main.tex`. Programs, libraries and other files that aren't text (`.exe`, `.dll`, `.so`, ELF and Mach-O programs, archives, fonts, media, ...) open in a hex view with **Info** (what the file is: machine, subsystem, entry point, sections, the libraries it loads, archive contents), **Hex**, **Binary** (bits) and **Strings** tabs. Every viewer has a button to open the file as text or as bytes instead. The **Viewers** settings page picks the PDF and LaTeX programs.
- **Panel** at the bottom with **Terminal**, **Problems**, **Warnings**, **Output**, **Ports** and **Debug Console** tabs. Build and run output goes to Output. Errors from building (C, C++ and Walnut) and from Walnut's live checking go to Problems, warnings, toolchain problems and settings mistakes go to Warnings, and clicking one jumps to that line. In the editor, a line with an error turns the error color and the mistake gets a squiggle (red for errors, amber for warnings; both are colors you can change). Terminal, Ports and Debug Console are placeholders for now.
- **Status bar**: the folder, problem counts, a theme switcher, the caret's line and column, the file's language and the toolchain in use.
- **GUI size**: Settings > GUI scales the tabs, explorer, panel and settings. Sizes there and for the editor font take px, pt, or a part of the window (vmin, vmax, vdiag, vw and vh from 0 to 1, or % of the window's smaller side), picked from a dropdown next to the number. The editor font is picked from a dropdown too.

| Key | |
|---|---|
| `Ctrl+S` | save |
| `Ctrl+B` | build the current file |
| `F5` | build and run it |
| `Ctrl+W` | close the tab |
| `Ctrl+Shift+M`, `Ctrl+Shift+U` | show Problems, show Output |
| `Ctrl+,` | open Settings |
| `F12`, `Alt+Left`, `Alt+Right` | go to definition, go back, go forward |

Settings > Shortcuts lists every key and mouse shortcut (Canopy's, the code editor's and the mouse's) with what it does. Type new keys into a box or press **Record** and then the keys; changes go to your `keys.bark`. When two shortcuts share a key, or a key can't be read, the page and Problems say which two clash and where each one is set (a line of `keys.bark`, or Canopy's default), and clicking the problem jumps there.

The editor itself is `UI/canopy.grove` and `UI/canopy.bark`. Grove scripts in it can call into Canopy with `grove::call`: `canopy.open_folder`, `canopy.open_file` (path and an optional line), `canopy.save`, `canopy.build`, `canopy.run`, `canopy.show_panel`, `canopy.active_file`, `canopy.workspace`, `canopy.settings_path` and `canopy.version`.

## Commands

| Command | |
|---|---|
| `canopy [folder or files]` | open the editor and return; `--wait` stays until it closes |
| `canopy install [--desktop]` | put `canopy` on your PATH and Canopy in your applications menu (and on your desktop) |
| `canopy uninstall` | undo `canopy install` |
| `canopy doctor` | which compilers and tools will be used, and what's missing |
| `canopy settings` | every setting in effect and where it came from |
| `canopy settings --create` | create your settings file with every option listed, commented out |
| `canopy build <file>` | build one C, C++, Walnut or Grove file |
| `canopy run <file> [-- args]` | build it and run it |
| `canopy languages` | the languages Canopy knows |

## Building

You need CMake 3.18+, Ninja (or Make) and a C++17 compiler. CMake fetches Grove, which brings Fizmo and Walnut with it.

Fedora:

```
sudo dnf install gcc-c++ cmake ninja-build pkgconf-pkg-config libX11-devel libXft-devel libXrender-devel \
    fontconfig-devel freetype-devel openssl-devel pango-devel cairo-devel zlib-devel libjpeg-turbo-devel \
    vulkan-headers vulkan-loader-devel mesa-libGL-devel
```

Windows (MSYS2 UCRT64):

```
pacman -S git mingw-w64-ucrt-x86_64-cc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja \
    mingw-w64-ucrt-x86_64-zlib mingw-w64-ucrt-x86_64-libjpeg-turbo mingw-w64-ucrt-x86_64-vulkan-headers
```

Then:

```
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
./build/bin/canopy doctor
./build/bin/canopy .
```

`canopy`, `grove` and `walnut` all land in `build/bin`, so the bundled tools are found the same way they are in an installed copy.

To work on Canopy against your own Grove checkout instead of the pinned tag:

```
cmake -S . -B build -G Ninja -DFETCHCONTENT_SOURCE_DIR_GROVE=../Grove
```

| Option | Default | |
|---|---|---|
| `CANOPY_GROVE_TAG` | `v0.1.6` | Grove tag to fetch |
| `CANOPY_WITH_WALNUT` | `ON` | build and ship Walnut |
| `CANOPY_BUILD_TESTS` | `ON` | build the tests |
| `CANOPY_PACKAGE` | `OFF` | build a self-contained download and set up CPack |
| `CANOPY_BUNDLE_WALNUT` | `ON` | with `CANOPY_PACKAGE`: put Walnut's release package for this platform inside Canopy's instead of building Walnut |
| `CANOPY_WALNUT_VERSION` | `0.1.7` | the Walnut release to bundle |
| `CANOPY_WALNUT_ARCHIVE` | | a Walnut release archive on disk to bundle instead of downloading one |

## Making packages

```
cmake -S . -B build-pkg -G Ninja -DCMAKE_BUILD_TYPE=Release -DCANOPY_PACKAGE=ON
cmake --build build-pkg
cd build-pkg && cpack
```

On Linux that makes `.tar.gz`, `.tar.xz`, `.zip`, `.deb` and `.rpm` (the last needs `rpmbuild`); with MSYS2 on Windows it makes `.zip`, `.7z` and an NSIS installer (needs NSIS). Each one has `bin/canopy` (plus `bin/canopy-gui.exe` on Windows, which opens the editor without a console window), `bin/grove`, `bin/walnut` with Walnut's `runtime` and `fizmo` folders, Grove's headers in `include`, the editor UI and icons in `share/canopy`, and the docs. The programs are linked so they only need libraries every system has.

Pushing a tag `vX.Y.Z` that matches `project(canopy VERSION X.Y.Z)` runs `.github/workflows/release.yml`, which builds, tests and packages Canopy for Linux x86_64, arm64 and i686 and Windows MinGW64, UCRT64 and ARM64, installs the `.deb` to check the menu entry and command work, and attaches everything to the GitHub release, the same set of systems as Grove and Walnut.

## Layout

| Folder | |
|---|---|
| `Include/canopy`, `Source` | `canopy_core` (settings and themes, languages, toolchain detection, builds and background jobs, compiler message parsing, workspaces and recent folders) and the shell that runs the UI |
| `Tools/canopy.cpp`, `Tools/canopy_gui.cpp` | the `canopy` command, and `canopy-gui` (Windows' windowed launcher); both call `run_cli` in `Source/cli.cpp` |
| `Packaging` | the icon (SVG, PNGs, `.ico`), the Windows resource file, and the `.deb`/`.rpm` install scripts |
| `cmake` | `CanopyPackaging.cmake` (CPack, Walnut bundling, static linking) and per-format options |
| `UI` | the editor itself, as a Grove document and Bark stylesheet |
| `Config` | the built-in settings (`editor.bark`, `compilers.bark`, `build.bark`, `highlighting.bark`, `keys.bark`) and themes (`themes/`), compiled into Canopy |
| `Tests` | unit tests and the hello-world files the build tests use |

## Branches

- `main` is what's released. Every release is a `v*` tag on `main`.
- `dev` is where work happens. Push to `dev` (or open pull requests into it), and merge `dev` into `main` when it's ready to release.

CI builds and tests both branches on Linux and Windows.
