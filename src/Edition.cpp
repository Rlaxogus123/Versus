#include "Edition.hpp"
#include <Geode/Geode.hpp>
#include <limits>

using namespace geode::prelude;
namespace versus {
void styleNickname(CCLabelBMFont* label) {
    if constexpr (!isMembershipEdition()) return;
    if (!label || !label->getChildren()) return;
    label->setColor(ccWHITE);
    float left = std::numeric_limits<float>::max(), right = -left;
    for (auto* glyph : CCArrayExt<CCSprite*>(label->getChildren())) {
        if (!glyph->isVisible()) continue;
        left = std::min(left, glyph->getPositionX());
        right = std::max(right, glyph->getPositionX());
    }
    for (auto* glyph : CCArrayExt<CCSprite*>(label->getChildren())) {
        auto color = nicknameColor(right > left ? (glyph->getPositionX() - left) / (right - left) : 0.f);
        glyph->setColor(ccc3(color[0], color[1], color[2]));
    }
}
}
