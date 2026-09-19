#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/SimplePlayer.hpp>
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

        auto* gameManager = GameManager::sharedState();
        auto* tipp7Icon = SimplePlayer::create(
            gameManager ? gameManager->getPlayerFrame() : 1
        );
        if (tipp7Icon) {
            tipp7Icon->setColors(
                ccc3(0, 110, 255),
                ccc3(255, 255, 255)
            );
            tipp7Icon->disableGlowOutline();
            tipp7Icon->setScale(0.55f);
            tipp7Icon->setPosition({
                button->getContentSize().width - 11.f,
                button->getContentSize().height - 11.f
            });
            tipp7Icon->setID("tipp7-icon"_spr);
            button->addChild(tipp7Icon, 10);
        }

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
