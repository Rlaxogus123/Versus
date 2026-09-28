#include "Edition.hpp"
#include <Geode/Geode.hpp>
#include <limits>

using namespace geode::prelude;
namespace versus {
void addMembershipAura(CCNode* icon, bool membership) {
    if (!icon || !membership || icon->getChildByID("membership-aura"_spr)) return;
    auto* aura = CCParticleSystemQuad::create("fireballEffect.plist", false);
    if (!aura) return;
    aura->setID("membership-aura"_spr);
    aura->setTotalParticles(18);
    aura->setPositionType(kCCPositionTypeGrouped);
    aura->setEmitterMode(kCCParticleModeGravity);
    aura->setSourcePosition({0.f, 0.f}); aura->setPosition({0.f, 0.f});
    aura->setPosVar({13.f, 13.f}); aura->setDuration(-1.f);
    aura->setLife(.8f); aura->setLifeVar(.2f); aura->setEmissionRate(16.f);
    aura->setAngle(90.f); aura->setAngleVar(40.f);
    aura->setSpeed(9.f); aura->setSpeedVar(4.f); aura->setGravity({0.f, 5.f});
    aura->setStartSize(7.f); aura->setStartSizeVar(2.f); aura->setEndSize(0.f);
    aura->setStartColor({.3f, 1.f, .45f, .65f}); aura->setStartColorVar({0.f, 0.f, 0.f, 0.f});
    aura->setEndColor({1.f, 1.f, 1.f, 0.f}); aura->setEndColorVar({0.f, 0.f, 0.f, 0.f});
    aura->setBlendAdditive(true); aura->setAutoRemoveOnFinish(false);
    icon->addChild(aura, -1); aura->resetSystem();
}
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
