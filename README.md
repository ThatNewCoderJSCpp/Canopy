# Canopy

Canopy is a code editor built with [Grove](https://github.com/ThatNewCoderJSCpp/Grove), on top of [Fizmo](https://github.com/ThatNewCoderJSCpp/Fizmo) and [Walnut](https://github.com/ThatNewCoderJSCpp/Walnut).

A grove is where the trees are, and the canopy is the part that covers all of them.

Out of the box it builds **Grove**, **C**, **C++** and **Walnut**, with no setup, and Rust, Zig, Python, Lua, Java, C#, assembly and JavaScript once their tools are installed:

- **Compilers are found for you.** Walnut and Grove ship next to Canopy. For C and C++, Canopy looks on your `PATH`, then in the usual install folders (MSYS2's `ucrt64`/`mingw64`/`clang64` on Windows, Homebrew on macOS). `canopy doctor` says exactly what it found, where, and what to install if something is missing.
- **Settings are a page, and a few small Bark files.** The **Settings** tab (`Ctrl+,`) has a page each for the editor, highlighting, compilers and building, and saves as you change things. Underneath, your settings are Bark files that only hold the lines you changed, so they're easy to read, edit by hand or copy to another computer. `canopy settings` lists every value in effect with the file and line it came from, and a misspelled setting is a warning with a "did you mean" instead of being silently ignored. Hovering a setting in any of those files shows what it does, its default, the value in use and where it came from, and (in a folder with the code that reads it, like Canopy's own) the lines that read it; Ctrl+click goes there. In a Grove app's stylesheets, your own properties work too: a custom property like `--glow: 4px;` is inherited, can be used in other values with `var(--glow)`, and is read from Walnut, C++ or C scripts with `computed_number("--glow")`, `computed_color`, `computed_bool` or `computed_style`. Hovering one shows the code that reads it, the `var()`s that use it and the other rules that set it (Ctrl+click goes to the first reader, or from a `var()` to where it's set); a name without `--` that Grove doesn't know still works but gets a "did you mean" in case it's a typo.

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
| `highlighting.bark` | your own colors: `highlight` for every language, `highlight.cpp` (or `c`, `walnut`, `grove`, `bark`, `cmake`, `javascript`, `zig`, `markdown`, `text`, ...) for one |
| `keys.bark` | keyboard and mouse shortcuts, like `build: "Ctrl+B";` or `go-to-definition: "Ctrl+Click";` (several with commas, or `"none"`) |
| `themes/*.bark` | your own themes, next to the built-in ones |

`canopy settings --dir` prints the folder. A project can override any of them with the same files in `.canopy/` in its root. An older single `settings.bark` there still works.

- **Themes and colors.** Canopy Dark, Dark Modern, Neon, Amber, Ember, Soft, Ocean, Forest, Mono and Light are built in. The Highlighting page shows each one's colors, and lists every kind of word (keywords, types, namespaces, strings, ...) with a color, **B**/*I*/U/S (bold, italic, underline, strikethrough) toggles and a reset. Each language (C, C++, Walnut, Grove, Bark, CMake, HTML, CSS, JavaScript, JSON, Rust, Zig, Python, Lua, Java, C#, assembly, MathText, Markdown and plain text) has its own fold on the page, opened from the list or from the sidebar, with its own **Apply**, **Discard changes** and **Copy Bark**. The Compilers page works the same way. Clicking a color's swatch opens a picker with a color wheel, brightness and alpha sliders, and the color as hex, RGB, RGBA, HSL and HSLA. Pointing at a row boxes everything it colors in a live preview, and clicking a word in the preview finds its row. Changes only show in the preview until you press **Apply**, so the colors you had are never lost while you experiment: **Discard changes** goes back to them, **Save as theme…** names the result and saves it in your `themes/` folder, and **Copy Bark** and **Copy JSON** put it on the clipboard. A theme is a short Bark file:

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

- **Explorer** on the left, with an icon for each kind of file: **Open Folder…** and **Open File…** use your system's file dialog, folders expand and collapse in place, and **Recent folders** reopens the last ten. **Add Folder…** puts more folders in the workspace; each shows as its own root, gets its own terminal, and is remembered the next time you open the first folder. Click to select, Ctrl+click to add to the selection, Shift+click for a range. Drag files and folders onto a folder to move them, or hold Ctrl while dropping to copy (Canopy asks first). Files and folders dragged in from your file manager or desktop are copied into the folder you drop them on; dropped anywhere else, files open in the editor and folders open (or join the workspace if one is open). While you drag inside the explorer, a little list beside the pointer shows each file's icon and name and where it will go, and Escape cancels. When something with the same name is already there, a move, copy, paste or rename asks whether to **Replace** it (what's there goes to the trash), **Keep both** or **Skip**; Settings > Explorer turns either question off. Right-click for New File, New Folder, Open in Terminal, Reveal in File Manager, Cut, Copy, Paste (on folders), Copy Path, Copy Relative Path, Rename (the extension stays), Change Extension, Delete (to the trash, after asking), Save, Revert Changes, Save All, and Add or Remove Folder from Workspace (removing the first folder makes the next one the workspace). With the explorer focused, Delete, F2, Ctrl+C, Ctrl+X, Ctrl+V, Shift+Alt+C, Enter, Up and Down work too. **Settings** opens the Settings tab. Saving a settings file by hand applies it straight away too.
- **Editor** in the middle: one tab per open file (clicking a file once opens a preview tab, in italics, that the next click replaces; double-click the file or its tab, or edit it, to keep it), with several carets when you want them (Alt+click, Shift+click straight up or down, or Shift+Up/Down) and box selection (Ctrl+Alt+drag selects the same columns on every line; drag straight down for a caret on each), Backspace at a tab stop deleting a whole level of indentation at once (between tab stops it deletes one space; Settings > Editor turns that off), Shift+Tab that also works on spaces after `//` or other text, Enter after a `{` adding the closing line for the language (`} end function main` in Walnut, `};` after a C++ class, `</column>` after a Grove tag; Settings > Editor turns that off), line numbers and highlighting for C, C++, Walnut, Grove, Bark and CMake (`CMakeLists.txt` and `.cmake`). A dot on a tab means unsaved changes, and closing it asks first. Hovering a string, character or number literal in C, C++ or Walnut shows its type, its length (characters, bytes and code units) and its content with escapes like `\t` and `\u00e9` decoded and named; a character's code in decimal, hex and U+, and its UTF-8 bytes; and a number's type from its suffix, with its value in decimal, hex, octal and binary (and, for floats, what the stored value really is). In Walnut files, hovering shows what the compiler knows: signatures, initializers, an enum member's underlying type and value (`short int: Color::Green = 5`), and what each keyword and builtin type is ("Walnut keyword", "Walnut builtin type"). Function signatures in hovers that don't fit on one line break between whole parameters, with `) -> type` on the last line; Settings > Editor sets how wide and tall the hover gets. Ctrl+click goes to the overload or constructor actually used, opens imported and `#include`d files, and opens Walnut's own library files (read-only copies that Canopy asks `walnut library` for when the library isn't on disk).
- **Suggestions** while you type, like VS Code's IntelliSense list: Tab puts the highlighted one in, Escape closes the list until you start another word, and Ctrl+Space opens it any time. C and C++ get the names declared in the file and the headers it includes, members after `.` and `->`, keywords, `#` directives and header names inside `#include "..."` and `<...>`. Walnut asks `walnut complete`, so after `counter.` or `std::io::` you get that type's fields and methods or that namespace's contents. Grove gets tags after `<`, the tag to close after `</`, and attributes inside a tag (with `=""` added). Bark gets properties (with `: ;` added), pseudo-classes after `:`, functions and `$variables` in values. CMake gets commands, `${...}` variables, targets and argument keywords. Every language also gets other words from the file. Names are kept in a trie (`fizmo::TrieMap`), so the list is ready as you type: exact prefixes come first, then names whose word starts match (`gv` finds `getValue`). Each suggestion shows a colored letter and what it is (function, struct, enum, keyword, ...); Settings > Editor turns suggestions off, makes Enter accept them too, turns off word suggestions, and hides the letters or the kinds. In Walnut, `import module.` suggests the library's modules (`io`, `collections`, ...) and the `.walh` modules and folders next to the file, in the open folder and in `-M` folders from the Walnut flags.
- **Rust, Zig, Python, Lua, Java and C#.** Each is highlighted and gets suggestions and hovers: keywords, built-in functions and types, the standard library's modules (`std::` in Rust and Zig, `import` in Python, `string.` in Lua, `java.util` and `System.` in Java and C#), and members after `.` (or `::` in Rust and `:` in Lua) for variables whose type Canopy can tell from the code, like a `Vec`, a Python list or a Java `ArrayList`. Python, Lua and Zig files get Problems while you type (from Python's parser, `luac -p` and `zig ast-check`), and building shows the compiler's errors in Problems with their lines for all of them. **Run** (F5) does what each language expects: `cargo run` in a folder with `Cargo.toml` or `rustc` for one file, `zig build` with a `build.zig` or `zig build-exe`, `python -u`, `lua`, `javac` then `java` (packages work, from the file's `package` line), and `dotnet run` with a `.csproj` (or a single `.cs` file with .NET 10, or Mono's `mcs` and `mono`). Python indents after `:` and dedents after `return`; Lua indents blocks and adds the closing `end`. Settings > Compilers has a row for each tool (auto finds it) and flags for each language. The names and descriptions come from `Assets/languages/*.txt`, in the same `owner|kind|name|signature|description` lines as the web docs.
- **Assembly.** `.asm`, `.s`, `.nasm`, `.masm` and `.fasm` files are highlighted (instructions, registers, directives, labels, numbers), with hovers for common instructions and registers. Canopy works out which syntax the file is written in (NASM, GAS with AT&T or Intel syntax, MASM or FASM) and which processor it's for (x86-64, x86, ARM64, ARM, RISC-V or MIPS) from the directives, registers and instructions it uses, and shows it in the status bar, like `Assembly (NASM · x86-64)`. Hovering that says why it decided that and which installed assemblers can build the file, or what to install. **Run** assembles and links with the right tools (`nasm` or `yasm` and then `ld` or the C compiler, the C compiler for GAS, `ml64` for MASM, `fasm`), and code for another processor builds with a cross compiler and runs through QEMU. Settings > Compilers > Assembly can set the syntax and processor instead.
- **Walnut safety.** Settings > Compilers > Walnut turns on Walnut's optional safety features for Build and Run: runtime checks (indexes outside memory from `new[]`, null or deleted pointers, pointers to locals of functions that already returned, integer overflow, each stopping the program with the `.wal` line), a leak report when the program ends, the garbage collector, and ownership checks (use after move or delete, deleting twice, pointers to variables that no longer exist), which also show in Problems as you type. They're all off by default because each one makes programs slower.
- **Run stats.** After Run (F5), Output ends with a line like `Ran for 1 s 530 ms 146 µs · CPU 1 s 310 ms 142 µs · peak memory 370 MB · exit code 0`, and the **Stats** tab has the details: wall time, CPU time split into the program's own code and the system's work for it, CPU use (above 100% means several cores), peak memory, page faults, context switches and disk reads and writes, how long each build step took and how much memory it used, and the last 20 runs to compare against. Times are in hours, minutes, seconds, milliseconds and microseconds, leaving out the parts that are zero. Output now also shows a program's output while it runs instead of when it ends. On Windows, peak memory is the most memory the program had committed, since that's what Windows reports for a program and everything it starts.
- **Copying from the panel**: drag over text in Output, Problems or Warnings to select it, double-click for a word, and Ctrl+C to copy. Right-click for Copy, Copy Line (or Copy Problem), Copy All, Select All and Clear Output.
- **Viewers** for files that aren't code. Images (PNG, JPEG, GIF, BMP, WebP, TIFF, TGA, QOI, PNM, ICO and SVG) open as pictures with their size and format, zoom in and out or fit the editor, and scroll sideways when they are wider than it. PDFs open as pages, drawn with Poppler's `pdftoppm`, MuPDF's `mutool`, `pdftocairo` or Ghostscript, whichever is installed. A `.tex` file opens with a live preview beside it: Canopy builds it with `latexmk`, `tectonic`, `pdflatex`, `xelatex` or `lualatex`, rebuilds when you save (or press **Build**), underlines LaTeX's errors in the source and lists them in Problems. A file that's part of a bigger document can say `% !TEX root = main.tex`. A Markdown file (`.md`, `.markdown`) opens with a rendered preview beside it that updates as you type: headings, emphasis, strikethrough, code blocks, block quotes, nested and task lists, tables, images from the folder and links (clicking a web link opens your browser, clicking a relative link opens that file). A Walnut or Fizmo math text file (`.math`, `.mt`, or `.mtb`/`.brace` for the brace syntax) opens with a typeset preview beside it, one equation per blank-line-separated block, also updating as you type. The arrow in a preview's bar hides or shows it. Programs, libraries and other files that aren't text (`.exe`, `.dll`, `.so`, ELF and Mach-O programs, archives, fonts, media, ...) open in a hex view with **Info** (what the file is: machine, subsystem, entry point, sections, the libraries it loads, archive contents), **Hex**, **Binary** (bits) and **Strings** tabs. Every viewer has a button to open the file as text or as bytes instead. The **Viewers** settings page picks the PDF and LaTeX programs and turns the LaTeX, Markdown and math previews on or off.
- **Panel** at the bottom with **Terminal**, **Problems**, **Warnings**, **Output**, **Stats**, **Console**, **Elements**, **Git**, **History**, **Ports** and **Debug Console** tabs (Git and History are described [below](#git-history-and-tasks)). Build and run output goes to Output. Errors from building (C, C++ and Walnut) and from Walnut's live checking go to Problems, warnings, toolchain problems and settings mistakes go to Warnings, and clicking one jumps to that line. In the editor, a line with an error turns the error color and the mistake gets a squiggle (red for errors, amber for warnings; both are colors you can change). **Terminal** is a full terminal emulator, like VS Code's: your real shell (bash on Linux and macOS, cmd.exe or PowerShell on Windows through ConPTY, or the one set under Settings > Explorer, Terminal and Git) runs in the workspace folder with a prompt like `you@host:/path/to/folder$`, and everything a terminal does works, from Tab completion and Ctrl+R to `vim`, `htop`, `less`, 256-color and truecolor output, the mouse in programs that use it, and Unicode (wide characters, emoji, accents). There's one per workspace folder, picked from the dropdown; **+ New** starts another, **Clear** empties it and **Kill** stops it. Drag to select and Ctrl+Shift+C to copy, Ctrl+Shift+V to paste, or right-click for Copy, Paste, Select All, Clear, New and Kill; Ctrl+C and Ctrl+V go to the program like in any terminal (Ctrl+C interrupts), the wheel or Shift+PageUp/PageDown to scroll back. A tab character in program output moves 4 columns (Settings > Explorer, Terminal and Git > Tab size; terminals usually use 8). Opening a folder starts its terminal there. Ctrl+` shows it and Ctrl+Shift+` opens a new one; other shortcuts go to the shell, so Ctrl+W, Ctrl+B and the rest do what they do in bash. **Ports** runs local web servers, like VS Code's Live Server, built on Fizmo's networking: pick a workspace folder and a port and press **Start Server**, or right-click a folder or an `.html` file and choose **Serve with Local Server**. It serves the folder at `http://localhost:5500/` (or the next free port), with `index.html` for folders, a file list when there isn't one, and `/about` finding `about.html`; with **Live reload** on, open pages reload whenever you save a file in that folder. Each server shows its URL and request count, with **Open in Browser**, **Copy URL** and **Stop**. Settings > Explorer, Terminal and Git sets the first port, the address (`0.0.0.0` lets your phone open it), live reload and the page console. **Console** is the browser's developer console for those pages, inside Canopy: whatever a served page logs (`console.log`, `warn`, `error`, `info`, `debug`, `table`, `group`, `count`, `time` and `assert`, with objects, arrays, Maps, Sets and elements shown readably), uncaught JavaScript errors and promise rejections with their stack, and files the page asked for that aren't there (a misspelled stylesheet, script or image path) show up with the file and line they came from; click it to jump there. Repeats are counted instead of listed again, the level menu and filter box narrow what's shown, and a page's old messages go away when it reloads unless **Preserve log** is on. Type JavaScript in the box at the bottom and press Enter to run it in every open page, like the browser's console (Up and Down go back through what you ran). Debug Console is a placeholder for now. **Settings > Special Characters** lists every character Walnut's `\s{...}` escape knows, sorted by type (Greek, Arrows, Math operators and relations, number sets, currency, shapes and more), each with its symbol, UTF code and built-in names, and a box for your own name for it (so `\s{dg}` can mean `°` next to the built-in `\s{deg}`). A search box finds characters by name, by U+ code or by pasting the character. Your names are saved in `walnut.bark` at the top of the project, which the Walnut compiler reads too, so the project builds the same outside Canopy. A name given twice, or one that reuses a built-in name, turns both sides red and goes in the Problems tab with its line. Hovers and `\s{` completion in the editor know your names as well.
- **Web pages, JavaScript and Node.** HTML, CSS, JavaScript and JSON files are highlighted (including the `<script>` and `<style>` parts of a page, CSS colors with a swatch you can click) and get suggestions and hovers: HTML tags, the attributes that go with each tag and their values (`type="checkbox"`, `rel="stylesheet"`); CSS properties, the values each one takes, functions like `calc()` and `var()`, pseudo-classes and at-rules; and JavaScript keywords and globals, the members of `document`, `console`, `window`, elements, events, arrays, strings and promises, jQuery (`$('#a').` lists jQuery's methods, `$.` its functions like `$.ajax`), Node (`require('` lists the built-in modules and the packages in `node_modules`, and `fs.`, `path.`, `process.` and the rest list their functions) and Express (`app.`, `req.` and `res.`). Canopy follows variables, so `const app = express()`, `const el = document.querySelector('p')` and `$('#a').addClass('b').` all know what they hold. **Problems** checks pages and stylesheets while you type: tags that are never closed or closed in the wrong place, closing tags with no opening one, ids used twice, unknown tags, images without alt text, a missing `;` in CSS, an extra or missing brace, and misspelled properties (with what you probably meant); with Node.js installed, JavaScript files and the scripts inside pages get a syntax check from `node --check` too. **Run** (F5) on an `.html` file serves its folder from **Ports** and opens the page in your browser, with the Console tab showing what it logs. A page that loads jQuery (`jquery.min.js`, `jquery-3.7.1.js` or `/__canopy/jquery.js`) gets Canopy's bundled copy when there's no such file, and the Console says so. **Run** on a `.js` file runs it with Node in its own terminal, in the folder with the nearest `package.json`; when it prints that it's listening on a port (`app.listen(3000)` in Express does), Canopy puts a server in front of it and opens that in your browser, so the Console, Elements, bundled jQuery and live reload work for Express apps too, and saving a `.js` or `.json` file restarts the app. If it needs a package that isn't installed, Canopy tells you which one to `npm install`. Node is found automatically (or set it under Settings > Compilers); Settings has switches for putting the server in front of Node apps and for restarting them on save. **Elements** is the browser's element inspector for served pages: it shows the page's live HTML as a tree you can expand and collapse, the page in the browser highlights whatever you point at, and selecting an element shows its HTML to edit. **Apply** (Ctrl+Enter) puts your version into the page and **Delete node** removes it; text and comments edit the same way. Changes only happen in the page in the browser, never in your files, and **Reload page** (or any reload) drops them. Pick the page in the dropdown when several are open. The HTML, CSS, JavaScript, jQuery, Node and Express names and descriptions come from `Assets/web/web_docs.txt`, one per line as `owner|kind|name|signature|description` (`css-values|property|word word …` lists a property's values, `css-known|property|…` the property names Problems accepts), so adding one is a line in that file.
- **Arrange it your way.** Drag the thin bars between the side bar, the editor and the panel to resize them (the pointer turns into a resize arrow). Every tab can move: drag a tab (Explorer, Terminal, Git, History, ...) onto another tab bar, or right-click it and pick **Move to the Side Bar / Panel / Secondary Side Bar**. The Secondary Side Bar appears once something is in it, so Git can live on its own on the right, or in the side bar like VS Code. Right-click any tab bar to put that area on the left, right, top or bottom; put the side bar and the panel on the same side and they share it, side by side (editor on top, both underneath). **Panel Across the Whole Window** lets the panel run under the side bars too, and **Reset Layout** puts everything back. It's all saved in the `layout` section of `workbench.bark`, which you can also edit by hand:

  ```bark
  layout {
      sidebar: left;        /* left, right, top or bottom */
      panel: bottom;
      aside: right;
      panel-span: editor;   /* editor or all */
      sidebar-size: 250px;
      panel-size: 230px;
      aside-size: 340px;
      git: aside;           /* the area each tab is in: sidebar, panel or aside */
      history: panel;
  }
  ```
- **Status bar**: the folder, problem counts, the Git branch (with `*` for uncommitted changes and ↑/↓ for commits to push or pull; click it for the Git tab), a theme switcher, the caret's line and column, the file's language and the toolchain in use.
- **Find and replace** (Ctrl+F, Ctrl+H): a bar under the tabs finds text in the file you're editing and puts a light orange box behind every match, a brighter one behind the current match, with how many there are ("3 of 12"). Enter or F3 goes to the next match and Shift+Enter or Shift+F3 to the previous one, as do the arrows. **Aa** matches case (Alt+C), **ab** whole words (Alt+W) and **.\*** reads it as a regular expression (Alt+R). **Select All** (Alt+Enter) puts a caret on every match so typing changes them all. Replace changes the current match and moves on; Replace All changes every one as a single undo step. With a regular expression, `$1` and `$&` in the replacement put in what it caught. Escape closes the bar; text selected in the editor starts the search.
- **Developer tab** (Ctrl+Shift+I, or click the version in the status bar): the window's size in pixels and as a share of the monitor, its position, monitor and scale; how long the last frame took (layout, drawing and presenting), how many frames Canopy painted in the last second and how many draw operations they used; CPU, memory and GPU use for the whole computer and for Canopy, with the processor, memory modules, graphics card, driver, clocks and temperatures where the system reports them; and the OS, kernel, architecture, desktop and uptime. It refreshes every second while it's open.
- **GUI size**: Settings > GUI scales the tabs, explorer, panel and settings. Sizes there and for the editor font take px, pt, or a part of the window (vmin, vmax, vdiag, vw and vh from 0 to 1, or % of the window's smaller side), picked from a dropdown next to the number. The editor font is picked from a dropdown too.

| Key | |
|---|---|
| `Ctrl+S` | save |
| `Ctrl+B` | build the current file |
| `F5` | build and run it |
| `Ctrl+W` | close the tab |
| `Ctrl+Shift+M`, `Ctrl+Shift+U` | show Problems, show Output |
| `Ctrl+,` | open Settings |
| `Ctrl+Shift+G`, `Ctrl+Shift+H` | show Git, show the current file's History |
| `Ctrl+Shift+R` | pick a task from `.canopy/tasks.bark` and run it |
| `F12`, `Alt+Left`, `Alt+Right` | go to definition, go back, go forward |
| `Ctrl+Space`, `Tab`, `Escape` | show suggestions, accept one, close them |
| `Ctrl+/` | comment or uncomment the selected lines, with the language's own comment (`//`, `#`, `--`, `;`) |

Settings > Shortcuts lists every key and mouse shortcut (Canopy's, the code editor's and the mouse's) with what it does. Type new keys into a box or press **Record** and then the keys; changes go to your `keys.bark`. When two shortcuts share a key, or a key can't be read, the page and Problems say which two clash and where each one is set (a line of `keys.bark`, or Canopy's default), and clicking the problem jumps there.

The editor itself is `UI/canopy.grove` and `UI/canopy.bark`. Grove scripts in it can call into Canopy with `grove::call`: `canopy.open_folder`, `canopy.open_file` (path and an optional line), `canopy.save`, `canopy.build`, `canopy.run`, `canopy.show_panel`, `canopy.active_file`, `canopy.workspace`, `canopy.settings_path` and `canopy.version`.

## Git, history and tasks

The terminal is still there for anything, but the everyday Git work has its own tab so you don't have to type it.

**Git** (`Ctrl+Shift+G`, or click the branch in the status bar) has three columns:

- **Changes.** The first dropdown picks the repository: every repository in the workspace is listed, including ones in its subfolders, so opening a folder that holds several projects (like `Canopy`, `Walnut` and `Grove` side by side) shows each of them, and **Clone Repository…** at the bottom downloads another. The second dropdown switches branches (remote branches you don't have yet are listed too, and picking one checks it out with tracking). Then **Fetch**, **Pull** and **Push**: Push publishes a new branch with `-u` the first time, and if the repository has no remote yet it asks for the URL and adds it as `origin`. Under them is how the branch stands against its upstream (`↑2 origin/dev`, `up to date`, `not pushed yet`). Type a message and press **Commit** (or Enter): with nothing staged it stages everything first (`git add -A`), otherwise it commits just what's staged. A long message scrolls inside the box (with … at the cut-off end), and the arrow keys, Home, End and clicks move the caret anywhere in it to edit. **Commit & Push** (or Ctrl+Enter) pushes too. Below is every changed file, in **Staged Changes** and **Changes** like VS Code, colored by what happened to it (yellow changed, green new, red deleted and struck through) with **Open**, **History**, **Stage**/**Unstage** and **Discard**, plus **Stage All**, **Unstage All** and **Discard All**. Discarding keeps the old text in local history first. **More** has the rest: branches, merging, tags, stashes, amending and undoing the last commit, discarding everything, cloning, SSH keys and the tasks file. A folder that isn't a repository gets **Initialize Repository** and **Clone Repository…** buttons (a folder you cloned is already a repository, so it doesn't).

**Cloning.** When you're signed in to GitHub, **Clone Repository…** lists every repository you can see (yours, ones you collaborate on and your organizations', most recently pushed first): type to narrow the list and click one, or paste any URL instead. Signed out, it asks for the URL. Then your system's folder picker opens: the repository goes in a new folder named after it inside the one you pick (or straight into an empty folder that already has its name). Without a folder picker (on Linux, install `zenity` or `kdialog`), Canopy asks for the path. Settings > Explorer, Terminal and Git > Clone with picks `https` or `ssh` URLs for the list.
- **Tasks**, from `.canopy/tasks.bark` and your own `.canopy/tasks.local.bark` (below), each with a **Run** button.
- **Commits**, **Branches** and **Tags**. Commits lists the current branch with its branches and tags; click one to see its changes in a tab, or right-click to copy its hash, start a branch or tag there, revert it or check it out. Branches lists local and remote branches with how far each is ahead or behind, and **Switch**, **Push**/**Publish** and **Delete** buttons (right-click for Merge and Rename); **+ New Branch** starts one from where you are. Tags lists every tag with **Push** and **Delete** (right-click to check one out or delete it on the remote too); **+ New Tag** suggests the next version after your newest `v*` tag, makes an annotated tag if you add a message after a `|`, and offers to push it.

**Changes, VS Code style.** Clicking a file opens its diff in a tab: unstaged changes compare the staged version with your file (unsaved edits included), staged ones compare the last commit with what's staged. Every block of changes has its own buttons: **Stage** or **Revert** in an unstaged diff, **Unstage** in a staged one, so you can commit part of a file. The bar has **Stage File**, **Unstage File** or **Discard File**, and clicking a line jumps to it in the file. The explorer colors files and folders the same way (yellow changed, green new), and greys out anything `.gitignore` ignores.

**Passwords.** When Git needs a username, password, token or SSH passphrase (pushing to a private HTTPS remote without a credential helper, say), Canopy shows a small window to type it in, with passwords hidden as dots, and hands the answer straight to Git without keeping it. GitHub doesn't accept account passwords over HTTPS, so paste a personal access token there, or use SSH keys or a credential helper (Git for Windows' Credential Manager signs in through the browser) so it's never asked at all.

**Signing in to GitHub.** GitHub doesn't accept account passwords for Git, so **More → Sign in to GitHub…** signs Canopy in the way VS Code does: GitHub opens in your browser at github.com/login/device, you paste the code Canopy shows (it's already copied) and press **Authorize**, and the dialog closes by itself. From then on Git's pushes and pulls to GitHub use it without asking, and if GitHub ever turns one down, Canopy offers to sign in again. The sign-in is kept in your system's secure store (Windows Credential Manager, the macOS Keychain, or the Linux keyring through `secret-tool`), never in a file; on Linux without `secret-tool` (the `libsecret-tools` package) it lasts only until Canopy closes. When Git's own credential helper is a secure one (Git Credential Manager, `osxkeychain`, `libsecret`), Canopy hands the sign-in to it too, so `git` in a terminal works as well. **More → Sign Out of GitHub** removes it; you can also revoke it on GitHub under Settings > Applications > Authorized OAuth Apps. Signing in through the browser needs Canopy's GitHub OAuth app: make one under GitHub > Settings > Developer settings > OAuth Apps (any homepage and callback URL), tick **Enable Device Flow**, and build Canopy with `-DCANOPY_GITHUB_CLIENT_ID=<its Client ID>` (the ID isn't a secret), or set it under Settings > Explorer, Terminal and Git > GitHub app client ID. Without one, Canopy offers to take a pasted token instead.

**SSH.** Any Git server works over SSH (GitHub, GitLab, Bitbucket, your own), not just GitHub's sign-in. **More → Create SSH Key…** makes an Ed25519 key in `~/.ssh` with the label and passphrase you choose and copies its public half; **Copy SSH Public Key** copies it again, **Add SSH Key to GitHub** adds it to your account when you're signed in (sign-ins made before this asked for the `write:public_key` permission, so with an older one Canopy opens GitHub's New SSH key page with the key copied instead), **Test SSH Connection…** runs `ssh -T` against a host and says whether your key works, and **Switch origin Between SSH and HTTPS…** rewrites the remote (`https://github.com/you/app.git` ⇄ `git@github.com:you/app.git`). Passphrases and a new server's fingerprint are asked in Canopy's own window; the fingerprint question has **Trust and connect** and **Don't connect** buttons, and trusting saves it to `known_hosts`.

**Protected branches.** Deleting a branch listed under Settings > Explorer, Terminal and Git > Protected branches (`main master` by default), deleting a branch or tag on the remote, or undoing a commit that's already pushed to a protected branch asks you to type its name first. That guards against slips on your own computer; to actually stop people pushing to `main` or deleting it, turn on branch protection (or rulesets) in the repository's settings on GitHub, since that's enforced by the server whichever editor or terminal someone uses.

Git runs in the background, so the window never waits on it, and its output goes to **Output**. The tab notices changes made outside Canopy every few seconds (Settings > Explorer, Terminal and Git > Git refresh).

**History** (`Ctrl+Shift+H`) is the timeline of the file you're editing: a copy from every time you saved it in Canopy (kept in your settings folder under `history/`, 50 per file by default) mixed with every Git commit that changed it, following renames. Click a version (or **Compare**) to see it against the current text, or right-click to compare it with the version before, open it read-only, restore it, show the whole commit, or delete a saved copy. Restoring keeps what you had as another saved copy, so nothing is lost. Settings > Explorer, Terminal and Git turns local history off or changes how many copies are kept.

Diffs open as tabs: removed lines in red, added lines in green, both line numbers, and the file's name with how many lines were added and removed. Clicking a line opens the file there.

### Tasks

`.canopy/tasks.bark` turns a routine into one button. **Create tasks.bark** in the Git tab writes a starting file; each rule is a task, and its steps run in order, stopping at the first one that fails. `tasks.bark` is meant to be committed, so everyone working on the project gets the same tasks, like `.vscode/tasks.json`. Tasks only you want go in `.canopy/tasks.local.bark` (**Create my tasks**, or More → Create My Tasks): Canopy adds `*.local.bark` to `.canopy/.gitignore` so it's never committed, shows those tasks marked *yours*, and a task there with the same name as a shared one replaces it for you:

```bark
release {
    label: "Release";
    ask: message "Commit message";
    ask: version "Version" "{next-patch}";
    confirm: "Release v{version} from {branch} to main?";
    set-version: "CMakeLists.txt";
    run: "git add -A";
    try: "git commit -m {message}";
    run: "git push";
    run: "git checkout main";
    run: "git merge --ff-only dev";
    run: "git push";
    run: "git tag v{version}";
    run: "git push origin v{version}";
    run: "git checkout dev";
}

cmake {
    label: "CMake Build";
    on-build: instead;
    ask: jobs "Parallel jobs (-j)" 8 range(1, 256);
    run: "cmake -S . -B build -DCMAKE_BUILD_TYPE=Release";
    run: "cmake --build build -j {jobs}";
}
```

| Property | |
|---|---|
| `label`, `description` | the name on the button and the line under it |
| `ask: name "Prompt" default` | asks for a value before running and makes it `{name}`. The default can use placeholders, and an empty answer takes it. A number default (`8`) asks for a whole number; add `range(1, 64)` for limits, `number` for decimals, `bool` for a yes/no switch, `choice("Release", "Debug")` for a list to pick from, or `required` when it can't be left empty. An ask called `message` starts with whatever is in the Git tab's message box |
| `set: name value` | gives a value a name for the task, filled in after the asks, so `set: tag "v{version}";` works. `default: name value` does the same unless an ask or a `$variable` already gives it a value |
| `confirm: "..."` | asks OK/Cancel (after the asks) before anything runs |
| `run: "..."` | runs a program. The command is split into arguments first and placeholders are filled into each one, so `-m {message}` is one argument whatever the message contains: quotes, `;` and `$` can't break out of it |
| `try: "..."` | the same, but a failure doesn't stop the task (handy for `git commit` when there's nothing new to commit) |
| `shell: "..."` | runs a line through the shell (`sh` or `cmd.exe`), for pipes and `&&`. Placeholders are quoted for the shell |
| `set-version: "file" ...` | writes `{version}` into each file: `project(... VERSION x.y.z)` in `CMakeLists.txt`, `"version": "..."` in `package.json`, `version = "..."` in `Cargo.toml`/`pyproject.toml`, or a file that's only a version. With no file it uses the version file |
| `version-file: "file"` | where `{current-version}` comes from (otherwise the first `set-version` file, or `CMakeLists.txt`, `package.json`, `Cargo.toml`, `pyproject.toml` or `VERSION`) |
| `cwd: "folder"` | where the steps run, relative to the workspace |
| `on-build`, `on-run` | `before`, `after` or `instead`: run this task when you press **Build** (`Ctrl+B`) or **Run** (`F5`). `before` builds only if the task worked, `after` runs it only if the build worked, `instead` replaces Canopy's own build (handy for a CMake project) |
| `keep-going: true` | don't stop at a failing step |

Placeholders: `{version}` (an ask's answer, or the current version), `{current-version}`, `{next-patch}`, `{next-minor}`, `{next-major}`, `{branch}`, `{root}`, `{file}`, `{file-dir}`, `{file-name}`, `{file-stem}`, `{date}`, every ask's and `set`'s name, and the file's Bark variables: `$remote = "origin";` at the top makes `{remote}` (and `$remote`) usable in every task in the file. `{name:value}` uses `value` when nothing sets `name`, `{{` and `}}` are a literal brace, and shell variables like `${HOME}` are left alone. Mistakes in the file (an unknown property, a bad `on-build`, a `{placeholder}` nothing sets, a setting given twice) show at the top of the Tasks column.

Placeholders, asks and steps aren't special to Canopy: they're Grove's Bark actions (`grove/bark/actions.hpp`, see Grove's `bark.md`), which any program can use to give its own Bark files steps, prompts and values.

## Commands

| Command | |
|---|---|
| `canopy [folder or files]` | open the editor and return; `--wait` stays until it closes |
| `canopy install [--desktop]` | put `canopy` on your PATH and Canopy in your applications menu (and on your desktop) |
| `canopy uninstall` | undo `canopy install` |
| `canopy doctor` | which compilers and tools will be used, and what's missing |
| `canopy settings` | every setting in effect and where it came from |
| `canopy settings --create` | create your settings file with every option listed, commented out |
| `canopy build <file>` | build one file (C, C++, Walnut, Grove, Rust, Zig, Python, Lua, Java, C# or assembly) |
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
| `CANOPY_GROVE_TAG` | `v0.1.22` | Grove tag to fetch |
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
| `Include/canopy`, `Source` | `canopy_core` (settings and themes, languages, toolchain detection, builds and background jobs, compiler message parsing, workspaces and recent folders, the terminal's shell process, Git commands and parsers with a line diff, local file history, `tasks.bark`) and the shell that runs the UI (with the Settings page and the Git and History tabs) |
| `Tools/canopy.cpp`, `Tools/canopy_gui.cpp` | the `canopy` command, and `canopy-gui` (Windows' windowed launcher); both call `run_cli` in `Source/cli.cpp` |
| `Packaging` | the icon (SVG, PNGs, `.ico`), the Windows resource file, and the `.deb`/`.rpm` install scripts |
| `cmake` | `CanopyPackaging.cmake` (CPack, Walnut bundling, static linking) and per-format options |
| `UI` | the editor itself, as a Grove document and Bark stylesheet |
| `Config` | the built-in settings (`editor.bark`, `compilers.bark`, `build.bark`, `workbench.bark`, `highlighting.bark`, `keys.bark`) and themes (`themes/`), compiled into Canopy |
| `Tests` | unit tests and the hello-world files the build tests use |

## Branches

- `main` is what's released. Every release is a `v*` tag on `main`.
- `dev` is where work happens. Push to `dev` (or open pull requests into it), and merge `dev` into `main` when it's ready to release.

CI builds and tests both branches on Linux and Windows.
