#ifndef CANOPY_SHORTCUTS_HPP
#define CANOPY_SHORTCUTS_HPP

#include "settings.hpp"

#include <grove/input/keymap.hpp>

#include <optional>
#include <string>
#include <vector>

namespace canopy {

struct Shortcut {
    std::string id;
    std::string label;
    std::string help;
    std::string group;
    std::vector<grove::Chord> defaults;
    bool editor = false;
    bool mouse = false;
    bool fixed = false;
    std::string fixed_text;
};

const std::vector<Shortcut>& shortcuts();
const Shortcut* find_shortcut(const std::string& id);
const std::vector<std::string>& shortcut_groups();

struct ShortcutBinding {
    const Shortcut* shortcut = nullptr;
    std::vector<grove::Chord> chords;
    std::string text;
    std::string file;
    std::size_t line = 0;
    bool custom = false;
    bool invalid = false;
};

struct ShortcutConflict {
    grove::Chord chord;
    const Shortcut* first = nullptr;
    const Shortcut* second = nullptr;
    std::string first_where;
    std::string second_where;
    std::string file;
    std::size_t line = 0;
    std::string message;
};

std::vector<ShortcutBinding> shortcut_bindings(const Settings& settings);
std::string shortcut_where(const ShortcutBinding& binding);
std::vector<ShortcutConflict> shortcut_conflicts(const std::vector<ShortcutBinding>& bindings);
std::vector<std::string> shortcut_errors(const std::vector<ShortcutBinding>& bindings);
const ShortcutBinding* binding_for(const std::vector<ShortcutBinding>& bindings, const std::string& id);
std::optional<std::string> command_for(const std::vector<ShortcutBinding>& bindings, const grove::Chord& pressed);
std::string editor_keymap(const std::vector<ShortcutBinding>& bindings);

} // namespace canopy

#endif // CANOPY_SHORTCUTS_HPP
