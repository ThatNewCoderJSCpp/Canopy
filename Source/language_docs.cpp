#include <canopy/language_docs.hpp>

namespace canopy {

namespace {

const std::vector<DocEntry>& grove_tag_table() {
    static const std::vector<DocEntry> entries = {
        { "b", "", "inline text" },
        { "body", "", "document structure" },
        { "br", "", "images and breaks" },
        { "button", "", "controls" },
        { "center", "", "a block that fills the space left over and puts its contents in the middle." },
        { "code", "", "inline text" },
        { "column", "", "blocks; `container` is the same as `div`, and `pre` keeps its spaces and line breaks" },
        { "container", "", "blocks; `container` is the same as `div`, and `pre` keeps its spaces and line breaks" },
        { "div", "", "blocks; `container` is the same as `div`, and `pre` keeps its spaces and line breaks" },
        { "grove", "", "document structure" },
        { "h1", "", "headings and paragraphs" },
        { "h2", "", "headings and paragraphs" },
        { "h3", "", "headings and paragraphs" },
        { "h4", "", "headings and paragraphs" },
        { "head", "", "document structure" },
        { "hr", "", "images and breaks" },
        { "i", "", "inline text" },
        { "img", "", "images and breaks" },
        { "input_bool", "", "value: `bool`; typing accepts click to toggle; drawn as a checkbox or a switch" },
        { "input_code", "", "value: `std::string` / `const char*` / `string`; typing accepts everything; see Code editor" },
        { "input_color", "", "value: `bark::Rgba` / `uint32_t` `0xRRGGBBAA`; typing accepts `#` and hex digits" },
        { "input_file", "", "value: `std::string` / `const char*` / `string`, the chosen path (several are separated by `\\n`); typing accepts nothing; clicking opens the system picker" },
        { "input_float", "", "value: `double` / `double` / `short float`; typing accepts digits, one `.`, a leading `-` if allowed" },
        { "input_integer", "", "value: `std::int64_t` / `int64_t` / `long int`; typing accepts `0`–`9`, and a leading `-` only if `min` allows negatives" },
        { "input_radio", "", "value: `bool` per option, `string` per group; typing accepts click to pick; one per `name` group" },
        { "input_select", "", "a dropdown of `<option>`s; value: `std::string` / `const char*` / `string`, the chosen option's value; clicking opens the list" },
        { "input_slider", "", "value: `double`; typing accepts click or drag" },
        { "option", "", "one choice inside `<input_select>`: its `value` (or its text) is what's picked, `disabled` greys it out" },
        { "input_string", "", "value: `std::string` / `const char*` / `string`; typing accepts any printable text, up to `maxlength` characters" },
        { "item", "", "blocks; `container` is the same as `div`, and `pre` keeps its spaces and line breaks" },
        { "label", "", "controls" },
        { "link", "", "inline text" },
        { "list", "", "blocks; `container` is the same as `div`, and `pre` keeps its spaces and line breaks" },
        { "math", "", "MathText, see below" },
        { "modal", "", "a dialog drawn on top of the window." },
        { "p", "", "headings and paragraphs" },
        { "page", "", "one screen of the app; only one page in a group shows at a time." },
        { "panel", "", "blocks; `container` is the same as `div`, and `pre` keeps its spaces and line breaks" },
        { "pre", "", "blocks; `container` is the same as `div`, and `pre` keeps its spaces and line breaks" },
        { "row", "", "blocks; `container` is the same as `div`, and `pre` keeps its spaces and line breaks" },
        { "script", "", "Walnut, C++ or C, inline or `src=\"...\"`" },
        { "span", "", "inline text" },
        { "style", "", "Bark, inline or `src=\"file.bark\"`" },
        { "text", "", "inline text" },
        { "title", "", "document structure" },
        { "u", "", "inline text" },
        { "window", "", "a separate window, set up like `<body>`; it isn't drawn inside the window it sits in." },
    };
    return entries;
}

const std::vector<DocEntry>& grove_attribute_table() {
    static const std::vector<DocEntry> entries = {
        { "align", "blocks", "places what is inside: `center`, `top`, `bottom right`, ..." },
        { "allow", "string", "only these kinds of characters can be typed or pasted: `letters`, `digits`, `numbers`, `spaces`, `newlines`, `punctuation`, `symbols`, `special`, `uppercase`, `lowercase`, `emoji`, `ascii`, `hex`, `word`, separated by spaces" },
        { "alpha", "color", "`false` hides the picker's alpha slider" },
        { "app-id", "body", "names the app to the desktop" },
        { "block", "string", "these kinds can't be typed; `special` (punctuation and symbols) still lets `.` through so decimals work" },
        { "block-chars", "string", "single characters that are always refused" },
        { "caret", "input_code", "`line:column` (1-based) moves the caret there and scrolls to it whenever the attribute changes; anything after a `#` is ignored, so `\"12:1#2\"` moves there again" },
        { "center", "blocks", "puts what is inside in the middle, like Bark `align: center`" },
        { "chars", "string", "single characters let through on top of `allow` and `block`, like `chars=\"'-\"`" },
        { "checked", "bool, radio", "on" },
        { "class", "any", "space-separated class names, matched by `.name` in Bark" },
        { "color-swatches", "input_code", "`false` turns off the color swatches in Bark and Grove" },
        { "current", "page", "the page in its group that shows first" },
        { "disabled", "all", "greyed out and not editable" },
        { "filter", "input_file", "`Name: *.a *.b; Other: *.c`, or just `*.a *.b`" },
        { "format", "color", "how the value is written in the box: `hex` (default), `rgb`, `rgba`, `hsl` or `hsla`. Any of them can be typed whatever the format; a see-through color written as `rgb` or `hsl` shows as `rgba` or `hsla` so the alpha isn't lost" },
        { "height", "img", "height in pixels or a Bark length" },
        { "hide-value", "input_file", "don't show the chosen file's name next to the label" },
        { "hover", "input_code", "what to show in the popup the editor asked for with a `hover` event, see Hovers and Ctrl+click" },
        { "icon", "body, window", "one or more images for the window icon" },
        { "id", "any", "names the element, for `#id` in Bark, `to=\"id\"` and `find_by_id`" },
        { "indent", "input_code", "`spaces` (default) or `tabs`: what Tab and Enter insert" },
        { "language", "input_code", "`c`, `cpp`, `walnut`, `grove`, `bark`, `cmake` or `plain` (default); file extensions like `hpp` or `wal` work too" },
        { "line-numbers", "input_code", "`false` hides them" },
        { "link-words", "input_code", "`true` treats every word as a possible link, for an app that works out where it goes when it's clicked" },
        { "links", "input_code", "spans that Ctrl+click goes somewhere from (built like `semantic`); they're underlined while Ctrl is held over them" },
        { "mark", "input_code", "token kinds like `\"keyword string\"`: draws a box behind every token of those kinds, to show what a color changes" },
        { "max", "integer, float, slider", "the value is clamped into range; a slider defaults to 0..100" },
        { "maxlength", "string", "longer text is cut" },
        { "min", "integer, float, slider", "the value is clamped into range; a slider defaults to 0..100" },
        { "mode", "input_file", "`open` (default), `save` or `folder`" },
        { "multiple", "input_file", "with `open`: pick several files" },
        { "name", "radio", "the group" },
        { "open", "modal", "the modal starts open" },
        { "pattern", "string", "a regular expression the whole text has to match before it's accepted; until it does, Enter leaves the field red." },
        { "persistent", "modal", "clicking outside or Escape does not close it" },
        { "picker", "color", "`false` turns off the picker" },
        { "placeholder", "input_code", "shown while it's empty" },
        { "problems", "input_code", "errors and warnings to underline; build it with `grove::code_problems_text()`" },
        { "readonly", "input_code", "can be selected and copied but not changed" },
        { "semantic", "input_code", "colors from a compiler, drawn over the built-in highlighting; build it with `grove::code_spans_text()` (see below)" },
        { "src", "style, script, img, page", "the file to load, relative to this .grove file" },
        { "start", "input_file", "the folder the dialog starts in (default: the current value)" },
        { "step", "integer, float, slider", "Up/Down step for integer and float; a slider snaps to it" },
        { "style", "any", "Bark declarations for this element only, like `style=\"color: red\"`" },
        { "switch", "bool", "draw it as a switch instead of a checkbox" },
        { "tab-size", "input_code", "columns per tab (default 4)" },
        { "title", "input_file", "the dialog's title" },
        { "to", "any", "clicking it shows the `page` or opens the `modal` with this id" },
        { "value", "integer, float, string, slider, color, radio", "the value; for radio, the option's value in its group" },
        { "width", "img", "width in pixels or a Bark length" },
    };
    return entries;
}

const std::vector<DocEntry>& bark_property_table() {
    static const std::vector<DocEntry> entries = {
        { "accent-color", "inputs", "checked boxes and radios, slider fill and thumb, focused border, the caret and selection of `input_code`" },
        { "align", "blocks, `row`", "the easy way to place everything inside: `center`, or a side and/or a corner like `top`, `bottom right`, `center left`." },
        { "align-items", "blocks, `row`", "across it: `stretch` (default), `start`, `center`, `end`" },
        { "align-self", "a child", "its own `align-items`" },
        { "appearance", "`input_bool`", "`checkbox` (default) or `switch`" },
        { "backdrop", "`modal`", "the color drawn over the window behind an open modal (default a see-through black)" },
        { "background", "any", "fill color" },
        { "border-color", "blocks, inputs", "" },
        { "border-radius", "blocks, inputs", "rounded corners" },
        { "border-width", "blocks, inputs", "" },
        { "code-attribute", "`input_code`", "how that kind of token looks: a color, then any of `bold`, `italic` and `underline` (`code-comment: #6a737d italic;`). Leaving one out keeps Grove's own color" },
        { "code-builtin-operator", "`input_code`", "the finer kinds C, C++ and Walnut split out; one you leave out looks like the kind it came from (`code-keyword`, `code-type`, `code-variable`, `code-operator`, `code-function`, `code-constant` or `code-plain`)" },
        { "code-builtin-type", "`input_code`", "the finer kinds C, C++ and Walnut split out; one you leave out looks like the kind it came from (`code-keyword`, `code-type`, `code-variable`, `code-operator`, `code-function`, `code-constant` or `code-plain`)" },
        { "code-cast", "`input_code`", "the finer kinds C, C++ and Walnut split out; one you leave out looks like the kind it came from (`code-keyword`, `code-type`, `code-variable`, `code-operator`, `code-function`, `code-constant` or `code-plain`)" },
        { "code-comment", "`input_code`", "how that kind of token looks: a color, then any of `bold`, `italic` and `underline` (`code-comment: #6a737d italic;`). Leaving one out keeps Grove's own color" },
        { "code-constant", "`input_code`", "how that kind of token looks: a color, then any of `bold`, `italic` and `underline` (`code-comment: #6a737d italic;`). Leaving one out keeps Grove's own color" },
        { "code-control-keyword", "`input_code`", "the finer kinds C, C++ and Walnut split out; one you leave out looks like the kind it came from (`code-keyword`, `code-type`, `code-variable`, `code-operator`, `code-function`, `code-constant` or `code-plain`)" },
        { "code-declaration", "`input_code`", "the finer kinds C, C++ and Walnut split out; one you leave out looks like the kind it came from (`code-keyword`, `code-type`, `code-variable`, `code-operator`, `code-function`, `code-constant` or `code-plain`)" },
        { "code-enum-member", "`input_code`", "the finer kinds C, C++ and Walnut split out; one you leave out looks like the kind it came from (`code-keyword`, `code-type`, `code-variable`, `code-operator`, `code-function`, `code-constant` or `code-plain`)" },
        { "code-error-line", "`input_code`", "a color and any of `bold`, `italic`, `underline` for the text of a line with an error; off unless set" },
        { "code-error-underline", "`input_code`", "the squiggle under the `problems` an editor is given (default red and amber)" },
        { "code-escape", "`input_code`", "how that kind of token looks: a color, then any of `bold`, `italic` and `underline` (`code-comment: #6a737d italic;`). Leaving one out keeps Grove's own color" },
        { "code-function", "`input_code`", "how that kind of token looks: a color, then any of `bold`, `italic` and `underline` (`code-comment: #6a737d italic;`). Leaving one out keeps Grove's own color" },
        { "code-hover", "`input_code`", "the background of its hover popup (default a little lighter than `background`)" },
        { "code-keyword", "`input_code`", "how that kind of token looks: a color, then any of `bold`, `italic` and `underline` (`code-comment: #6a737d italic;`). Leaving one out keeps Grove's own color" },
        { "code-mark", "`input_code`", "the box drawn behind tokens named in the `mark` attribute (default a see-through orange)" },
        { "code-namespace", "`input_code`", "how that kind of token looks: a color, then any of `bold`, `italic` and `underline` (`code-comment: #6a737d italic;`). Leaving one out keeps Grove's own color" },
        { "code-number", "`input_code`", "how that kind of token looks: a color, then any of `bold`, `italic` and `underline` (`code-comment: #6a737d italic;`). Leaving one out keeps Grove's own color" },
        { "code-operator", "`input_code`", "how that kind of token looks: a color, then any of `bold`, `italic` and `underline` (`code-comment: #6a737d italic;`). Leaving one out keeps Grove's own color" },
        { "code-overloaded-operator", "`input_code`", "the finer kinds C, C++ and Walnut split out; one you leave out looks like the kind it came from (`code-keyword`, `code-type`, `code-variable`, `code-operator`, `code-function`, `code-constant` or `code-plain`)" },
        { "code-plain", "`input_code`", "text that isn't any kind of token (defaults to `color`)" },
        { "code-preprocessor", "`input_code`", "how that kind of token looks: a color, then any of `bold`, `italic` and `underline` (`code-comment: #6a737d italic;`). Leaving one out keeps Grove's own color" },
        { "code-property", "`input_code`", "how that kind of token looks: a color, then any of `bold`, `italic` and `underline` (`code-comment: #6a737d italic;`). Leaving one out keeps Grove's own color" },
        { "code-self", "`input_code`", "the finer kinds C, C++ and Walnut split out; one you leave out looks like the kind it came from (`code-keyword`, `code-type`, `code-variable`, `code-operator`, `code-function`, `code-constant` or `code-plain`)" },
        { "code-string", "`input_code`", "how that kind of token looks: a color, then any of `bold`, `italic` and `underline` (`code-comment: #6a737d italic;`). Leaving one out keeps Grove's own color" },
        { "code-tag", "`input_code`", "how that kind of token looks: a color, then any of `bold`, `italic` and `underline` (`code-comment: #6a737d italic;`). Leaving one out keeps Grove's own color" },
        { "code-template-scope", "`input_code`", "the finer kinds C, C++ and Walnut split out; one you leave out looks like the kind it came from (`code-keyword`, `code-type`, `code-variable`, `code-operator`, `code-function`, `code-constant` or `code-plain`)" },
        { "code-this", "`input_code`", "the finer kinds C, C++ and Walnut split out; one you leave out looks like the kind it came from (`code-keyword`, `code-type`, `code-variable`, `code-operator`, `code-function`, `code-constant` or `code-plain`)" },
        { "code-type", "`input_code`", "how that kind of token looks: a color, then any of `bold`, `italic` and `underline` (`code-comment: #6a737d italic;`). Leaving one out keeps Grove's own color" },
        { "code-unresolved", "`input_code`", "the finer kinds C, C++ and Walnut split out; one you leave out looks like the kind it came from (`code-keyword`, `code-type`, `code-variable`, `code-operator`, `code-function`, `code-constant` or `code-plain`)" },
        { "code-variable", "`input_code`", "how that kind of token looks: a color, then any of `bold`, `italic` and `underline` (`code-comment: #6a737d italic;`). Leaving one out keeps Grove's own color" },
        { "code-variable-template", "`input_code`", "the finer kinds C, C++ and Walnut split out; one you leave out looks like the kind it came from (`code-keyword`, `code-type`, `code-variable`, `code-operator`, `code-function`, `code-constant` or `code-plain`)" },
        { "code-warning-underline", "`input_code`", "the squiggle under the `problems` an editor is given (default red and amber)" },
        { "color", "text", "" },
        { "cursor", "any", "`pointer`, `text`, `default`, `crosshair`, `move`, `not-allowed`, ... while the mouse is over it" },
        { "display", "any", "`none` hides it, `row` lays its children out side by side, `inline-block` makes it only as wide as its content, `inline` puts it in the flow of the text around it" },
        { "flex", "children of a column, row items", "a number: how much of the leftover space the item gets. In a column (any block that isn't a row) this needs the column's height to be known: from `height`, from being an item in a row, or from being `body`" },
        { "font-family", "text", "a font name like `\"JetBrains Mono\"`, or `monospace`, `serif`, `sans-serif`; a font that isn't installed falls back to one of the same kind" },
        { "font-size", "text", "" },
        { "font-style", "text", "`italic`" },
        { "font-weight", "text", "" },
        { "gap", "blocks, `row`", "extra space after a block; space between the items of a row" },
        { "height", "blocks, `input_code`", "a length or a `%` of the parent's height (when the parent's height is known); with `overflow` other than `visible` the block is exactly this tall, otherwise it's the smallest height" },
        { "justify-content", "blocks, `row`", "along the direction things are laid out (down a column, across a row): `start` (default), `center`, `end`, `space-between`, `space-around`, `space-evenly`" },
        { "line-height", "`input_code`", "a multiple of the font size (`1.4`) or a length" },
        { "margin", "blocks, inputs", "1 to 4 lengths, CSS order" },
        { "math-color", "`math`", "defaults to `color`" },
        { "max-height", "blocks", "lengths or `%`; `max-height` only applies when `overflow` clips" },
        { "max-width", "blocks, row items", "lengths or `%`; in a row they bound how much of the free space an item takes" },
        { "min-height", "blocks", "lengths or `%`; `max-height` only applies when `overflow` clips" },
        { "min-width", "blocks, row items", "lengths or `%`; in a row they bound how much of the free space an item takes" },
        { "opacity", "any", "0 to 1; fades the element and everything inside it" },
        { "overflow", "blocks", "`visible` (default), `hidden` clips the content to the block, `auto` and `scroll` clip it and let the mouse wheel scroll it up and down" },
        { "overflow-x", "blocks", "`auto` or `scroll` lets the block scroll sideways (Shift+wheel); images and wide `width`s inside keep their size instead of shrinking to fit" },
        { "overflow-y", "blocks", "`overflow` for up and down only" },
        { "padding", "blocks, inputs", "1 to 4 lengths, CSS order" },
        { "size", "`body`, `window`", "`width height`; a `<window>` with no height fits its content" },
        { "text-align", "text, `math`", "`left`, `center`, `right` (`start` and `end` mean left and right)" },
        { "text-decoration", "text", "`underline`, `line-through`" },
        { "title", "`body`, `window`", "window title (the `title` attribute wins)" },
        { "white-space", "blocks", "`normal` (default) wraps text and collapses spaces; `nowrap` collapses but doesn't wrap; `pre` keeps spaces and line breaks and doesn't wrap; `pre-wrap` keeps them and wraps" },
        { "width", "blocks, inputs", "a length or a `%` of the space it's in; text inputs and sliders default to 220px; never wider than the space it's in, except inside a block with `overflow-x: auto`" },
        { "zoom", "`body`, `window`", "scales every `px` and `pt` length, font size and image: `1.25` or `125%`" },
    };
    return entries;
}

const std::vector<DocEntry>& bark_pseudo_table() {
    static const std::vector<DocEntry> entries = {
        { ":active", "", "an element while the mouse is pressed on it" },
        { ":checked", "", "a checked `input_bool` or `input_radio`" },
        { ":current", "", "the `page` showing in its group, and any element whose `to` points at a page that's showing or a modal that's open (for tabs)" },
        { ":disabled", "", "with or without the `disabled` attribute" },
        { ":empty", "", "an element with no children" },
        { ":enabled", "", "with or without the `disabled` attribute" },
        { ":first-child", "", "by position among its parent's elements" },
        { ":focus", "", "the element that has the keyboard" },
        { ":hover", "", "the element under the mouse, and its parents" },
        { ":last-child", "", "by position among its parent's elements" },
        { ":only-child", "", "by position among its parent's elements" },
    };
    return entries;
}

const std::vector<DocEntry>& bark_function_table() {
    static const std::vector<DocEntry> entries = {
        { "calc", "calc(100% - 40px)", "`+`, `-`, `*` and `/`, with `*` and `/` first and `( )` to group: `calc((100% - 2em) / 3)`" },
        { "clamp", "clamp(200px, 40vw, 500px)", "the middle value, kept between the first and last" },
        { "max", "max(50%, 200px)", "the largest" },
        { "min", "min(100%, 600px)", "the smallest, so never wider than 600px" },
    };
    return entries;
}

const std::vector<DocEntry>& bark_unit_table() {
    static const std::vector<DocEntry> entries = {
        { "vdiag", "", "the window's diagonal, √(width² + height²)" },
        { "vh", "", "the window's height" },
        { "vheight", "", "the window's height" },
        { "vmax", "", "the larger of width and height" },
        { "vmin", "", "the smaller of width and height" },
        { "vw", "", "the window's width" },
        { "vwidth", "", "the window's width" },
    };
    return entries;
}

const std::vector<DocEntry>& cmake_command_table() {
    static const std::vector<DocEntry> entries = {
        { "FetchContent_Declare", "", "FetchContent_Declare(<name> GIT_REPOSITORY <url> GIT_TAG <tag> | URL <url>) — describes a dependency to download" },
        { "FetchContent_MakeAvailable", "", "FetchContent_MakeAvailable(<name>...) — downloads and adds the declared dependencies" },
        { "add_compile_options", "", "add_compile_options(<flag>...) — compiler flags for every target made after it" },
        { "add_custom_command", "", "add_custom_command(OUTPUT <file>... COMMAND <cmd>... DEPENDS ...) — a build step that makes files" },
        { "add_custom_target", "", "add_custom_target(<name> [ALL] COMMAND <cmd>...) — a target that runs commands" },
        { "add_definitions", "", "add_definitions(-D<name>...) — definitions for every target in this folder" },
        { "add_executable", "", "add_executable(<name> <source>...) — builds a program from the sources" },
        { "add_library", "", "add_library(<name> [STATIC|SHARED|INTERFACE] <source>...) — builds a library" },
        { "add_subdirectory", "", "add_subdirectory(<dir> [<binary_dir>]) — runs <dir>/CMakeLists.txt" },
        { "add_test", "", "add_test(NAME <name> COMMAND <cmd>...) — a test for ctest" },
        { "cmake_minimum_required", "", "cmake_minimum_required(VERSION <min>[...<max>]) — the oldest CMake this project works with; sets the policies to match" },
        { "configure_file", "", "configure_file(<input> <output> [@ONLY]) — copies a file, replacing ${VAR} and @VAR@" },
        { "else", "", "else() — the branch of if() when nothing matched" },
        { "elseif", "", "elseif(<condition>) — another branch of if()" },
        { "enable_testing", "", "enable_testing() — turns on add_test() and ctest" },
        { "endforeach", "", "endforeach() — closes foreach()" },
        { "endfunction", "", "endfunction() — closes function()" },
        { "endif", "", "endif() — closes if()" },
        { "endmacro", "", "endmacro() — closes macro()" },
        { "endwhile", "", "endwhile() — closes while()" },
        { "execute_process", "", "execute_process(COMMAND <cmd>... [OUTPUT_VARIABLE <var>]) — runs a program while configuring" },
        { "file", "", "file(READ|WRITE|GLOB|GLOB_RECURSE|COPY|MAKE_DIRECTORY|... ...) — works with files" },
        { "find_package", "", "find_package(<name> [<version>] [REQUIRED] [COMPONENTS ...]) — finds an installed package" },
        { "foreach", "", "foreach(<var> <items>...) ... endforeach() — runs the block once per item" },
        { "function", "", "function(<name> <arg>...) ... endfunction() — defines a command with its own variable scope" },
        { "get_filename_component", "", "get_filename_component(<var> <file> DIRECTORY|NAME|NAME_WE|EXT|ABSOLUTE ...) — part of a path" },
        { "get_target_property", "", "get_target_property(<variable> <target> <property>) — reads a target property" },
        { "if", "", "if(<condition>) ... elseif() ... else() ... endif() — runs a block when the condition is true" },
        { "include", "", "include(<file|module>) — runs another CMake file here" },
        { "include_directories", "", "include_directories(<dir>...) — include folders for every target in this folder" },
        { "install", "", "install(TARGETS|FILES|DIRECTORY ... DESTINATION <dir>) — what `cmake --install` copies" },
        { "link_directories", "", "link_directories(<dir>...) — library folders for every target in this folder" },
        { "list", "", "list(APPEND|REMOVE_ITEM|LENGTH|GET|FIND|SORT|... <list> ...) — works on a ;-separated list" },
        { "macro", "", "macro(<name> <arg>...) ... endmacro() — defines a command that runs in the caller's scope" },
        { "math", "", "math(EXPR <variable> \"<expression>\") — integer arithmetic" },
        { "message", "", "message([STATUS|WARNING|FATAL_ERROR|SEND_ERROR|DEBUG] \"<text>\") — prints a message" },
        { "option", "", "option(<variable> \"<help>\" [ON|OFF]) — a cache variable the user can switch on or off" },
        { "project", "", "project(<name> [VERSION <v>] [LANGUAGES <lang>...]) — names the project and sets PROJECT_NAME, PROJECT_VERSION and <name>_SOURCE_DIR" },
        { "return", "", "return() — leaves the current file or function" },
        { "set", "", "set(<variable> <value>... [CACHE <type> <doc> [FORCE]] [PARENT_SCOPE]) — sets a variable" },
        { "set_property", "", "set_property(<scope> PROPERTY <name> <value>...) — sets a property" },
        { "set_target_properties", "", "set_target_properties(<target>... PROPERTIES <prop> <value>...) — sets target properties" },
        { "string", "", "string(REPLACE|REGEX|TOLOWER|TOUPPER|STRIP|SUBSTRING|... ...) — works on text" },
        { "target_compile_definitions", "", "target_compile_definitions(<target> <PRIVATE|PUBLIC|INTERFACE> <name>[=<value>]...) — preprocessor definitions" },
        { "target_compile_features", "", "target_compile_features(<target> <PRIVATE|PUBLIC|INTERFACE> <feature>...) — language features like cxx_std_17" },
        { "target_compile_options", "", "target_compile_options(<target> <PRIVATE|PUBLIC|INTERFACE> <flag>...) — compiler flags" },
        { "target_include_directories", "", "target_include_directories(<target> <PRIVATE|PUBLIC|INTERFACE> <dir>...) — folders searched for #include" },
        { "target_link_libraries", "", "target_link_libraries(<target> <PRIVATE|PUBLIC|INTERFACE> <item>...) — links libraries into a target" },
        { "target_sources", "", "target_sources(<target> <PRIVATE|PUBLIC|INTERFACE> <source>...) — adds sources to a target" },
        { "unset", "", "unset(<variable> [CACHE|PARENT_SCOPE]) — removes a variable" },
        { "while", "", "while(<condition>) ... endwhile() — repeats while the condition holds" },
    };
    return entries;
}

const std::vector<DocEntry>& cmake_variable_table() {
    static const std::vector<DocEntry> entries = {
        { "APPLE", "", "true on Apple systems" },
        { "BUILD_SHARED_LIBS", "", "ON makes add_library() default to SHARED" },
        { "CMAKE_ARCHIVE_OUTPUT_DIRECTORY", "", "where static libraries are written" },
        { "CMAKE_BINARY_DIR", "", "the top build folder" },
        { "CMAKE_BUILD_TYPE", "", "Debug, Release, RelWithDebInfo or MinSizeRel for single-config generators" },
        { "CMAKE_CURRENT_BINARY_DIR", "", "the build folder matching CMAKE_CURRENT_SOURCE_DIR" },
        { "CMAKE_CURRENT_LIST_DIR", "", "the folder of the CMake file being run" },
        { "CMAKE_CURRENT_SOURCE_DIR", "", "the source folder of the CMakeLists.txt being run" },
        { "CMAKE_CXX_COMPILER", "", "the C++ compiler" },
        { "CMAKE_CXX_FLAGS", "", "flags for every C++ compile" },
        { "CMAKE_CXX_STANDARD", "", "the C++ standard targets use, like 17 or 20" },
        { "CMAKE_CXX_STANDARD_REQUIRED", "", "ON makes the standard a requirement instead of a wish" },
        { "CMAKE_C_COMPILER", "", "the C compiler" },
        { "CMAKE_C_FLAGS", "", "flags for every C compile" },
        { "CMAKE_C_STANDARD", "", "the C standard targets use, like 11 or 17" },
        { "CMAKE_EXPORT_COMPILE_COMMANDS", "", "ON writes compile_commands.json" },
        { "CMAKE_INSTALL_PREFIX", "", "where install() puts things" },
        { "CMAKE_LIBRARY_OUTPUT_DIRECTORY", "", "where shared libraries are written" },
        { "CMAKE_MODULE_PATH", "", "folders searched by include() and find_package()" },
        { "CMAKE_PREFIX_PATH", "", "folders searched for installed packages" },
        { "CMAKE_RUNTIME_OUTPUT_DIRECTORY", "", "where programs are written" },
        { "CMAKE_SOURCE_DIR", "", "the top source folder" },
        { "CMAKE_SYSTEM_NAME", "", "Windows, Linux, Darwin, ..." },
        { "CMAKE_VERSION", "", "the CMake version running" },
        { "MSVC", "", "true with Microsoft's compiler" },
        { "PROJECT_NAME", "", "the name given to the latest project()" },
        { "PROJECT_SOURCE_DIR", "", "the source folder of the latest project()" },
        { "PROJECT_VERSION", "", "the VERSION given to project()" },
        { "UNIX", "", "true on Unix-like systems, including macOS" },
        { "WIN32", "", "true on Windows" },
    };
    return entries;
}

const std::vector<DocEntry>& c_word_table() {
    static const std::vector<DocEntry> entries = {
        { "auto", "", "the type is worked out from the initializer" },
        { "bool", "", "true or false" },
        { "char", "", "one byte; plain char may be signed or unsigned" },
        { "char16_t", "", "a UTF-16 code unit" },
        { "char32_t", "", "a UTF-32 code unit" },
        { "char8_t", "", "a UTF-8 code unit" },
        { "const", "", "can't be changed after it is set" },
        { "const_cast", "", "adds or removes const" },
        { "constexpr", "", "can be worked out at compile time" },
        { "decltype", "", "the type of an expression" },
        { "double", "", "a 64-bit floating-point number" },
        { "dynamic_cast", "", "a runtime-checked cast between polymorphic types" },
        { "float", "", "a 32-bit floating-point number" },
        { "inline", "", "may be defined in several files; a hint to inline" },
        { "int", "", "a signed integer, usually 32 bits" },
        { "int16_t", "", "a signed 16-bit integer" },
        { "int32_t", "", "a signed 32-bit integer" },
        { "int64_t", "", "a signed 64-bit integer" },
        { "int8_t", "", "a signed 8-bit integer" },
        { "intptr_t", "", "a signed integer that can hold a pointer" },
        { "long", "", "a signed integer of at least 32 bits (64 on Linux and macOS, 32 on Windows)" },
        { "namespace", "", "a named scope for declarations" },
        { "noexcept", "", "promises not to throw" },
        { "nullptr", "", "the null pointer constant" },
        { "override", "", "must override a virtual function" },
        { "ptrdiff_t", "", "the signed difference between two pointers" },
        { "reinterpret_cast", "", "reinterprets the bits as another type" },
        { "short", "", "a signed integer of at least 16 bits" },
        { "signed", "", "makes an integer type signed" },
        { "size_t", "", "an unsigned type big enough for any object size" },
        { "sizeof", "", "the size of a type or object in bytes" },
        { "static", "", "one copy for the whole program, or private to this file" },
        { "static_cast", "", "a checked conversion between related types" },
        { "template", "", "a pattern the compiler fills in with types or values" },
        { "typename", "", "names a type parameter, or says a dependent name is a type" },
        { "uint16_t", "", "an unsigned 16-bit integer" },
        { "uint32_t", "", "an unsigned 32-bit integer" },
        { "uint64_t", "", "an unsigned 64-bit integer" },
        { "uint8_t", "", "an unsigned 8-bit integer" },
        { "uintptr_t", "", "an unsigned integer that can hold a pointer" },
        { "unsigned", "", "makes an integer type unsigned" },
        { "using", "", "brings a name into scope, or makes a type alias" },
        { "virtual", "", "can be overridden by a derived class" },
        { "void", "", "no value" },
        { "wchar_t", "", "a wide character" },
    };
    return entries;
}

} // namespace

const std::vector<DocEntry>& grove_tag_docs() { return grove_tag_table(); }

const std::vector<DocEntry>& grove_attribute_docs() { return grove_attribute_table(); }

const std::vector<DocEntry>& bark_property_docs() { return bark_property_table(); }

const std::vector<DocEntry>& bark_pseudo_docs() { return bark_pseudo_table(); }

const std::vector<DocEntry>& bark_function_docs() { return bark_function_table(); }

const std::vector<DocEntry>& bark_unit_docs() { return bark_unit_table(); }

const std::vector<DocEntry>& cmake_command_docs() { return cmake_command_table(); }

const std::vector<DocEntry>& cmake_variable_docs() { return cmake_variable_table(); }

const std::vector<DocEntry>& c_word_docs() { return c_word_table(); }

const DocEntry* find_doc(const std::vector<DocEntry>& docs, const std::string& name) {
    for (const DocEntry& d : docs) {
        if (d.name == name) return &d;
    }
    return nullptr;
}

} // namespace canopy
