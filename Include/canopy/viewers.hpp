#ifndef CANOPY_VIEWERS_HPP
#define CANOPY_VIEWERS_HPP

#include "build.hpp"
#include "settings.hpp"

#include <grove/input/code_buffer.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace canopy {

enum class ViewerKind {
    Text,
    Image,
    Pdf,
    Latex,
    Binary
};

const char* to_string(ViewerKind kind) noexcept;
std::optional<ViewerKind> viewer_kind_from_name(const std::string& name);

bool is_image_path(const std::string& path);
bool is_pdf_path(const std::string& path);
bool is_latex_path(const std::string& path);
bool is_binary_path(const std::string& path);
bool looks_binary(std::string_view bytes);
ViewerKind viewer_kind_for(const std::string& path, std::string_view head);
ViewerKind viewer_kind_for(const std::string& path);

std::optional<std::uint64_t> file_size(const std::string& path);
std::optional<std::string> read_file_range(const std::string& path, std::uint64_t offset, std::size_t size);
std::string human_size(std::uint64_t bytes);
std::string hex_offset(std::uint64_t offset, std::uint64_t file_size);

struct FileFact {
    std::string label;
    std::string value;
};

struct ImageInfo {
    std::string format;
    unsigned width = 0;
    unsigned height = 0;
    std::string detail;

    bool known_size() const noexcept { return width > 0 && height > 0; }
};

ImageInfo image_info(std::string_view bytes, const std::string& path = {});

struct BinarySection {
    std::string name;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
    std::uint64_t address = 0;
    std::string flags;
};

struct BinaryInfo {
    std::string kind;
    std::string summary;
    std::vector<FileFact> facts;
    std::vector<BinarySection> sections;
    std::vector<std::string> libraries;
};

BinaryInfo describe_binary(std::string_view bytes, const std::string& path = {});

enum class ByteView {
    Hex,
    Bits,
    Strings
};

struct ByteDump {
    std::string text;
    std::vector<grove::CodeSpan> spans;
};

std::size_t byte_view_page_size(ByteView view);
ByteDump hex_dump(std::string_view bytes, std::uint64_t base_offset, std::uint64_t file_size);
ByteDump bits_dump(std::string_view bytes, std::uint64_t base_offset, std::uint64_t file_size);
ByteDump strings_dump(std::string_view bytes, std::uint64_t base_offset, std::uint64_t file_size, std::size_t min_length = 4);
ByteDump byte_dump(ByteView view, std::string_view bytes, std::uint64_t base_offset, std::uint64_t file_size);
std::string sections_table(const std::vector<BinarySection>& sections);

struct ViewerTool {
    std::string key;
    std::string path;
    std::string name;
};

std::string viewer_tool_name(const std::string& path);
std::vector<std::string> pdf_renderer_candidates();
std::vector<std::string> latex_engine_candidates();
std::vector<std::string> viewer_tool_folders(const std::string& key);
std::optional<ViewerTool> find_viewer_tool(const Settings& settings, const std::string& key);
std::string viewer_install_hint(const std::string& key);

int pdf_resolution(const Settings& settings);
std::vector<std::string> pdf_render_command(const ViewerTool& tool, const std::string& pdf, const std::string& out_dir, int dpi);
std::vector<std::string> rendered_pages(const std::string& out_dir);
std::optional<std::size_t> pdf_page_count(std::string_view bytes);

std::string latex_root_file(const std::string& path, const std::string& text);
bool latex_is_document(const std::string& text);
std::vector<std::string> latex_build_command(const ViewerTool& tool, const std::string& tex, const std::string& out_dir, const std::string& flags);
std::string latex_pdf_path(const std::string& tex, const std::string& out_dir);
std::vector<CompilerMessage> parse_latex_log(const std::string& output, const std::string& tex);

std::string viewer_cache_dir(const std::string& path);

} // namespace canopy

#endif // CANOPY_VIEWERS_HPP
