#ifndef CANOPY_SPECIAL_NAMES_HPP
#define CANOPY_SPECIAL_NAMES_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace canopy {

struct ProjectName {
    std::string name;
    std::uint32_t code = 0;
    std::size_t line = 0;
};

struct ProjectNameProblem {
    std::size_t line = 0;
    std::string message;
};

struct ProjectNameEntry {
    std::string name;
    std::string value;
    bool quoted = false;
    std::size_t line = 0;
    bool ok = false;
    std::uint32_t code = 0;
    std::string problem;
};

struct ProjectNames {
    std::string path;
    std::vector<ProjectName> names;
    std::vector<ProjectNameProblem> problems;
    std::vector<ProjectNameEntry> entries;
};

bool special_character(std::string_view name, std::uint32_t& code);

std::string special_character_suggestion(std::string_view name);

std::vector<std::string_view> special_character_names(std::string_view prefix);

bool builtin_special_character(std::string_view name, std::uint32_t& code);

const char* builtin_special_category(std::uint32_t code);

std::vector<std::string_view> builtin_special_character_names(std::string_view prefix);

std::size_t special_name_distance(std::string_view a, std::string_view b);

bool special_names_alike(char a, char b);

const char* project_file_name();

std::string find_project_file(const std::string& from);

ProjectNames parse_project_names(std::string_view text, const std::string& path);

ProjectNames read_project_names(const std::string& path);

void use_project_names(const ProjectNames& names);

void clear_project_names();

const std::vector<ProjectName>& project_names();

std::string special_character_text(std::uint32_t code);

const std::vector<std::string>& special_character_categories();

std::string special_character_category(std::uint32_t code);

std::vector<std::uint32_t> builtin_special_codes();

std::vector<std::string_view> builtin_names_for(std::uint32_t code);

std::string project_name_value_text(const std::string& typed);

std::string project_names_block(const std::vector<ProjectNameEntry>& entries);

std::string replace_project_names(const std::string& file_text, const std::vector<ProjectNameEntry>& entries);

bool write_project_names(const std::string& path, const std::vector<ProjectNameEntry>& entries);

} // namespace canopy

#endif // CANOPY_SPECIAL_NAMES_HPP
