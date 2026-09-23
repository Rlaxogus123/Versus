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

        auto* title = CCLabelBMFont::create("Versus Mode!", "goldFont.fnt");
        title->setScale(.38f);
        title->limitLabelWidth(button->getContentSize().width + 38.f, .38f, .25f);
        title->setPosition({
            button->getPositionX(),
            button->getPositionY() + button->getContentSize().height / 2.f + 10.f
        });
        title->setID("versus-mode-label"_spr);
        menu->addChild(title, 10);

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
