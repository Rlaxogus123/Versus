#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/modify/CreatorLayer.hpp>
#include <Geode/utils/async.hpp>
#include "VersusService.hpp"
#include "VersusUI.hpp"

using namespace geode::prelude;

namespace {

enum class VersionGate {
    Checking,
    Current,
    UpdateRequired,
};

VersionGate s_versionGate = VersionGate::Checking;
async::TaskHolder<Result<std::optional<VersionInfo>>> s_updateCheck;
WeakRef<CCMenuItemSpriteExtra> s_versusButton;
WeakRef<CCSprite> s_versusSprite;
WeakRef<CCLabelBMFont> s_versusTitle;

void refreshVersusButton() {
    bool const available = s_versionGate == VersionGate::Current;
    if (auto sprite = s_versusSprite.lock()) {
        sprite->setColor(available ? ccWHITE : ccc3(105, 105, 105));
        sprite->setOpacity(available ? 255 : 145);
    }
    if (auto title = s_versusTitle.lock()) {
        title->setString(s_versionGate == VersionGate::Checking ? "Checking Version..." :
            s_versionGate == VersionGate::UpdateRequired ? "Update Required" : "Versus Mode!");
        title->setColor(available ? ccWHITE : ccc3(170, 170, 170));
    }
    // Keep touch enabled so a visually disabled button can explain why entry
    // is blocked instead of silently ignoring the player.
    if (auto button = s_versusButton.lock()) button->setEnabled(true);
}

void checkVersusVersion() {
    auto* mod = Mod::get();
    if (!mod) {
        s_versionGate = VersionGate::Current;
        return;
    }
    s_updateCheck.spawn(mod->checkUpdates(), [](Result<std::optional<VersionInfo>> result) {
        if (result.isErr()) {
            // An index/network outage must not permanently lock the mode.
            s_versionGate = VersionGate::Current;
        }
        else {
            s_versionGate = std::move(result).unwrap().has_value()
                ? VersionGate::UpdateRequired
                : VersionGate::Current;
        }
        refreshVersusButton();
    });
}

}

$on_mod(Loaded) {
    versus::Service::get().initialize();
    checkVersusVersion();
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
            s_versusSprite = activeSprite;
        }
        button->setEnabled(true);
        button->setTarget(this, menu_selector(VersusCreatorLayer::onOpenVersus));
        s_versusButton = button;

        auto* title = CCLabelBMFont::create("Versus Mode!", "goldFont.fnt");
        title->setScale(.38f);
        title->limitLabelWidth(button->getContentSize().width + 38.f, .38f, .25f);
        title->setPosition({
            button->getPositionX(),
            button->getPositionY() + button->getContentSize().height / 2.f + 10.f
        });
        title->setID("versus-mode-label"_spr);
        menu->addChild(title, 10);
        s_versusTitle = title;
        refreshVersusButton();

        return true;
    }

    void onOpenVersus(CCObject*) {
        if (s_versionGate != VersionGate::Current) {
            FLAlertLayer::create("Versus",
                s_versionGate == VersionGate::Checking
                    ? "Checking for the latest Versus version. Please try again shortly."
                    : "A newer Versus version is available. Please update before entering.",
                "OK")->show();
            return;
        }
        if (versus::Service::get().room()) {
            versus::showRoom();
        }
        else {
            versus::showLobby();
        }
    }
};
