#ifndef CANOPY_INPUT_TEXT_HPP
#define CANOPY_INPUT_TEXT_HPP

#include <grove/input/input.hpp>
#include <grove/markup/events.hpp>

#include <string>
#include <type_traits>
#include <utility>

namespace canopy {

template <typename E, typename = void>
struct event_has_edit_text : std::false_type {};

template <typename E>
struct event_has_edit_text<E, std::void_t<decltype(std::declval<const E&>().edit_text), decltype(std::declval<const E&>().has_edit_text)>> : std::true_type {};

template <typename E>
std::string live_input_text(const E& e) {
    if constexpr (event_has_edit_text<E>::value) {
        if (e.has_edit_text) return e.edit_text;
    }
    return e.target ? grove::input_string(*e.target).value_or("") : std::string();
}

} // namespace canopy

#endif // CANOPY_INPUT_TEXT_HPP
