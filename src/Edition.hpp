#pragma once
#include <algorithm>
#include <array>

namespace cocos2d { class CCLabelBMFont; class CCNode; }

namespace versus {
constexpr bool isMembershipEdition() {
#if defined(VERSUS_MEMBERSHIP) && VERSUS_MEMBERSHIP
    return true;
#else
    return false;
#endif
}
constexpr char const* editionMarker() {
    return isMembershipEdition() ? "versus-edition:membership" : "versus-edition:standard";
}
constexpr std::array<unsigned char, 3> nicknameColor(float position) {
    auto const t = std::clamp(position, 0.f, 1.f);
    return {static_cast<unsigned char>(70.f + 185.f * t), 255,
        static_cast<unsigned char>(100.f + 155.f * t)};
}
// Static per-glyph gradient, applied only when a label changes. No shaders,
// textures, scheduled updates, or global Geometry Dash nickname hooks.
void styleNickname(cocos2d::CCLabelBMFont* label);
void addMembershipAura(cocos2d::CCNode* icon, bool membership);
}
