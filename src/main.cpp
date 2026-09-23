#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/modify/CreatorLayer.hpp>
#include "VersusService.hpp"
#include "VersusUI.hpp"

using namespace geode::prelude;

$on_mod(Loaded) {
    versus::Service::get().initialize();
}

class $modify(VersusCreatorLayer, CreatorLayer) {
    bool init() {
        if (!CreatorLayer::init()) return false;

        auto* menu = this->getChildByID("creator-buttons-menu");
        auto* button = menu
            ? typeinfo_cast<CCMenuItemSpriteExtra*>(
                menu->getChildByID("versus-button")
            )
            : nullptr;
        if (!button) {
            log::warn("Unable to find the built-in Versus button");
            return true;
        }

        auto* activeSprite = CCSprite::createWithSpriteFrameName(
            "GJ_versusBtn_001.png"
        );
        if (activeSprite) {
            if (auto* oldSprite = button->getChildByType<CCSprite*>(0)) {
                activeSprite->setScale(oldSprite->getScale());
            }
            button->setSprite(activeSprite);
        }
        button->setEnabled(true);
        button->setTarget(this, menu_selector(VersusCreatorLayer::onOpenVersus));

        auto* badge = CCNodeRGBA::create();
        badge->setCascadeOpacityEnabled(true);
        badge->setContentSize({106.f, 23.f});
        badge->setPosition({
            button->getPositionX() - 53.f,
            button->getPositionY() + button->getContentSize().height / 2.f + 1.f
        });
        badge->setID("versus-mode-label"_spr);
        auto* shadow = NineSlice::create("square02b_001.png");
        shadow->setContentSize({110.f, 25.f});
        shadow->setPosition({55.f, 10.f});
        shadow->setColor(ccBLACK);
        shadow->setOpacity(75);
        badge->addChild(shadow, -2);
        auto* plate = NineSlice::create("square02b_001.png");
        plate->setContentSize({106.f, 23.f});
        plate->setPosition({53.f, 12.f});
        plate->setColor(ccc3(20, 73, 148));
        plate->setOpacity(235);
        badge->addChild(plate, -1);
        auto* title = CCLabelBMFont::create("Versus Mode!", "goldFont.fnt");
        title->setScale(.38f);
        title->limitLabelWidth(94.f, .38f, .25f);
        title->setPosition({53.f, 12.f});
        badge->addChild(title);
        badge->runAction(CCRepeatForever::create(CCSequence::create(
            CCEaseSineInOut::create(CCScaleTo::create(1.15f, 1.025f)),
            CCEaseSineInOut::create(CCScaleTo::create(1.15f, 1.f)), nullptr)));
        menu->addChild(badge, 10);

        return true;
    }

    void onOpenVersus(CCObject*) {
        if (versus::Service::get().room()) {
            versus::showRoom();
        }
        else {
            versus::showLobby();
        }
    }
};
