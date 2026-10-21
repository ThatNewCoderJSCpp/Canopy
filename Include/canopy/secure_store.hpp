#ifndef CANOPY_SECURE_STORE_HPP
#define CANOPY_SECURE_STORE_HPP

#include <optional>
#include <string>

namespace canopy {

std::string secure_store_name();
bool secure_store_available();
bool secure_store_save(const std::string& service, const std::string& account, const std::string& secret);
std::optional<std::string> secure_store_load(const std::string& service, const std::string& account);
bool secure_store_delete(const std::string& service, const std::string& account);

} // namespace canopy

#endif // CANOPY_SECURE_STORE_HPP
