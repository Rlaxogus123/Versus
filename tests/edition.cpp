#include "Edition.hpp"
#include <stdexcept>
#include <string_view>

int main() {
    using namespace versus;
    static_assert(nicknameColor(0.f) == std::array<unsigned char,3>{70,255,100});
    static_assert(nicknameColor(1.f) == std::array<unsigned char,3>{255,255,255});
    static_assert(nicknameColor(-1.f) == nicknameColor(0.f));
    static_assert(nicknameColor(2.f) == nicknameColor(1.f));
    static_assert(isMembershipEdition() == bool(EXPECT_MEMBERSHIP));
    if (std::string_view(editionMarker()) != (EXPECT_MEMBERSHIP ?
        "versus-edition:membership" : "versus-edition:standard")) throw std::runtime_error("wrong edition");
}
