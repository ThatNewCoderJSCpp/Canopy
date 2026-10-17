# Canopy

Canopy is a code editor built with [Grove](https://github.com/ThatNewCoderJSCpp/Grove), on top of [Fizmo](https://github.com/ThatNewCoderJSCpp/Fizmo) and [Walnut](https://github.com/ThatNewCoderJSCpp/Walnut).

A grove is where the trees are, and the canopy is the part that covers all of them.

Out of the box it builds **Grove**, **C**, **C++** and **Walnut**, with no setup:

- **Compilers are found for you.** Walnut and Grove ship next to Canopy. For C and C++, Canopy looks on your `PATH`, then in the usual install folders (MSYS2's `ucrt64`/`mingw64`/`clang64` on Windows, Homebrew on macOS). `canopy doctor` says exactly what it found, where, and what to install if something is missing.
- **Settings are one small file, in Bark.** No JSON, no 400-entry settings page. You only write the lines you want to change, `canopy settings` lists every value in effect with the file and line it came from, and a misspelled setting is a warning with a "did you mean" instead of being silently ignored.

```
// ~/.config/canopy/settings.bark   (Windows: %APPDATA%\Canopy\settings.bark)
editor {
    font-size: 15px;
}

language.cpp {
    standard: "c++20";
}

toolchain {
    cpp: "C:/msys64/ucrt64/bin/clang++.exe";
}
```

A project can override both with `.canopy/settings.bark` in its root.

## Commands

| Command | |
|---|---|
| `canopy [folder]` | open the editor |
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
| `CANOPY_GROVE_TAG` | `v0.1.2` | Grove tag to fetch |
| `CANOPY_WITH_WALNUT` | `ON` | build and ship Walnut |
| `CANOPY_BUILD_TESTS` | `ON` | build the tests |

## Layout

| Folder | |
|---|---|
| `Include/canopy`, `Source` | `canopy_core` (settings, languages, toolchain detection, builds, workspaces) and the shell that fills in the UI |
| `Tools/canopy.cpp` | the `canopy` command |
| `UI` | the editor itself, as a Grove document and Bark stylesheet |
| `Defaults/settings.bark` | the built-in settings, compiled into Canopy |
| `Tests` | unit tests and the hello-world files the build tests use |

## Branches

- `main` is what's released. Every release is a `v*` tag on `main`.
- `dev` is where work happens. Push to `dev` (or open pull requests into it), and merge `dev` into `main` when it's ready to release.

CI builds and tests both branches on Linux and Windows.
