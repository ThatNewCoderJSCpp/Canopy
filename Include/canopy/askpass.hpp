#ifndef CANOPY_ASKPASS_HPP
#define CANOPY_ASKPASS_HPP

#include <string>

namespace canopy {

bool askpass_yes_no(const std::string& prompt);
bool askpass_secret(const std::string& prompt);
std::string askpass_title(const std::string& prompt);
int run_askpass(const std::string& prompt);

} // namespace canopy

#endif // CANOPY_ASKPASS_HPP
