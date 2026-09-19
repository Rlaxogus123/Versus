#include "LevelSelector.hpp"
#include "VersusUI.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/modify/LevelBrowserLayer.hpp>
#include <Geode/modify/LevelCell.hpp>
#include <Geode/modify/LevelSearchLayer.hpp>

#include <algorithm>
#include <utility>

using namespace geode::prelude;

namespace {
struct PickerState {
    bool active = false;
    bool closing = false;
    int generation = 0;
    unsigned int stackDepth = 0;
    std::string roomID;
    std::function<void(versus::LevelInfo)> selected;
};

PickerState s_picker;

bool sameRoom() {
    auto const& room = versus::Service::get().room();
    return room && room->id == s_picker.roomID;
}

bool ownsNode(CCNode* node) {
    if (!s_picker.active) return false;
    for (; node; node = node->getParent()) {
        if (auto* marker = node->getChildByID("native-level-picker"_spr)) {
            return marker->getTag() == s_picker.generation;
        }
    }
    return false;
}

// Native search pushes result scenes on the director stack. Remove these on
// every exit so a later Back cannot resurrect an abandoned Versus picker.
void unwindSearch(unsigned int stackDepth) {
    auto* director = CCDirector::sharedDirector();
    auto* stack = director->m_pobScenesStack;
    if (stackDepth > 0 && stack && stack->count() > stackDepth) {
        director->popToSceneStackLevel(static_cast<int>(stackDepth));
    }
}

void returnToRoom(std::optional<versus::LevelInfo> level = std::nullopt) {
    if (!s_picker.active || s_picker.closing) return;
    s_picker.closing = true;
    auto generation = s_picker.generation;
    // Unwinding destroys native result cells. Do it after the touch/scheduler
    // callback has returned, and reject a queued exit from an older picker.
    Loader::get()->queueInMainThread([generation, level = std::move(level)]() mutable {
        if (!s_picker.active || s_picker.generation != generation) return;
        auto roomID = s_picker.roomID;
        auto onSelected = std::move(s_picker.selected);
        versus::cancelLevelSearch();

        auto& service = versus::Service::get();
        auto const& room = service.room();
        if (!room || room->id != roomID) {
            auto notice = service.takeNotice();
            versus::showLobby(notice.empty() ? "This room is no longer available." : notice);
            return;
        }

        // Callback owns only the selected value, never the destroyed RoomLayer.
        if (level && service.isHost() && !room->started && onSelected) {
            onSelected(std::move(*level));
        }
        versus::showRoom();
    });
}

class PickerWatch final : public CCNode {
public:
    static PickerWatch* create() {
        auto* watch = new PickerWatch;
        if (!watch->init()) {
            delete watch;
            return nullptr;
        }
        watch->autorelease();
        watch->setID("native-level-picker"_spr);
        watch->setTag(s_picker.generation);
        watch->schedule(schedule_selector(PickerWatch::checkRoom), .25f);
        return watch;
    }

    void checkRoom(float) {
        if (!s_picker.active || getTag() != s_picker.generation) return;
        // During a fade both scenes may be ticking. Navigate only from the
        // current native scene, after the transition has finished.
        CCNode* scene = this;
        while (scene->getParent()) scene = scene->getParent();
        if (scene != CCDirector::sharedDirector()->getRunningScene()) return;
        if (!sameRoom() || !versus::Service::get().isHost() ||
            versus::Service::get().room()->started) {
            returnToRoom();
        }
    }
};

void markPicker(CCNode* layer) {
    if (!s_picker.active) return;
    if (auto* watch = PickerWatch::create()) layer->addChild(watch);
}

versus::LevelInfo describeLevel(GJGameLevel* level) {
    versus::LevelInfo info;
    info.id = level->m_levelID.value();
    info.name = std::string(level->m_levelName);
    info.stars = level->m_stars.value();
    info.demon = level->m_demon.value() != 0;
    info.autoLevel = level->m_autoLevel;
    if (info.autoLevel) {
        info.difficulty = static_cast<int>(GJDifficulty::Auto);
    }
    else if (info.demon) {
        switch (level->m_demonDifficulty) {
            case 3: info.difficulty = static_cast<int>(GJDifficulty::DemonEasy); break;
            case 4: info.difficulty = static_cast<int>(GJDifficulty::DemonMedium); break;
            case 5: info.difficulty = static_cast<int>(GJDifficulty::DemonInsane); break;
            case 6: info.difficulty = static_cast<int>(GJDifficulty::DemonExtreme); break;
            default: info.difficulty = static_cast<int>(GJDifficulty::Demon); break;
        }
    }
    else {
        int difficulty = std::clamp(level->getAverageDifficulty(), 0, 5);
        info.difficulty = difficulty == 0 ? static_cast<int>(GJDifficulty::NA) : difficulty;
    }
    return info;
}
}

namespace versus {
bool isLevelSearchActive() {
    return s_picker.active;
}

void cancelLevelSearch() {
    if (!s_picker.active) return;
    auto depth = s_picker.stackDepth;
    s_picker.active = false;
    s_picker.closing = false;
    s_picker.selected = {};
    s_picker.roomID.clear();
    s_picker.stackDepth = 0;
    unwindSearch(depth);
}

void openLevelSearch(std::function<void(LevelInfo)> onSelected) {
    auto& service = Service::get();
    if (s_picker.active || !service.room() || !service.isHost() || service.room()->started) {
        return;
    }
    auto* director = CCDirector::sharedDirector();
    s_picker.active = true;
    ++s_picker.generation;
    s_picker.roomID = service.room()->id;
    s_picker.stackDepth = director->m_pobScenesStack
        ? director->m_pobScenesStack->count() : 1;
    s_picker.selected = std::move(onSelected);

    auto* scene = LevelSearchLayer::scene(0);
    if (!scene) {
        cancelLevelSearch();
        FLAlertLayer::create("Versus", "Unable to open level search.", "OK")->show();
        return;
    }
    director->replaceScene(CCTransitionFade::create(.25f, scene));
}
}

class $modify(VersusNativeLevelSearch, LevelSearchLayer) {
    bool init(int type) {
        if (!LevelSearchLayer::init(type)) return false;
        markPicker(this);
        return true;
    }

    void onBack(CCObject* sender) {
        if (ownsNode(this)) {
            returnToRoom();
            return;
        }
        LevelSearchLayer::onBack(sender);
    }

    void keyBackClicked() {
        if (ownsNode(this)) {
            returnToRoom();
            return;
        }
        LevelSearchLayer::keyBackClicked();
    }
};

class $modify(VersusNativeLevelBrowser, LevelBrowserLayer) {
    bool init(GJSearchObject* search) {
        if (!LevelBrowserLayer::init(search)) return false;
        markPicker(this);
        return true;
    }
};

class $modify(VersusNativeLevelCell, LevelCell) {
    void onClick(CCObject* sender) {
        if (!ownsNode(this)) {
            LevelCell::onClick(sender);
            return;
        }
        auto& service = versus::Service::get();
        if (!sameRoom() || !service.isHost() || service.room()->started) {
            returnToRoom();
            return;
        }
        if (!m_level || m_level->m_levelID.value() <= 0 ||
            m_level->m_levelType == GJLevelType::Editor ||
            m_level->m_levelType == GJLevelType::Main) {
            FLAlertLayer::create("Versus", "Choose an online level.", "OK")->show();
            return;
        }
        returnToRoom(describeLevel(m_level));
    }
};
