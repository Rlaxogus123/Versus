#include "MatchLaunch.hpp"
#include "MapCache.hpp"
#include "BattleSession.hpp"
#include "VersusService.hpp"
#include "VersusUI.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/LevelDownloadDelegate.hpp>
#include <Geode/binding/MusicDownloadManager.hpp>
#include <Geode/binding/MusicDownloadDelegate.hpp>
#include <Geode/binding/MenuLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <Geode/modify/GameManager.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/UILayer.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <limits>
#include <set>

using namespace geode::prelude;

namespace {
using Clock = std::chrono::steady_clock;
enum class Phase { Idle, Download, Resources, Entering, Waiting, Playing, Returning };

bool currentSceneContains(CCNode* node) {
    if (!node) return false;
    while (node->getParent()) node = node->getParent();
    return node == CCDirector::sharedDirector()->getRunningScene();
}

void collectIDs(std::set<int>& result, gd::string const& encoded) {
    std::string text(encoded);
    size_t start = 0;
    while (start < text.size()) {
        auto end = text.find(',', start);
        if (end == std::string::npos) end = text.size();
        int value = 0;
        auto parsed = std::from_chars(text.data() + start, text.data() + end, value);
        if (parsed.ec == std::errc() && parsed.ptr == text.data() + end && value > 0) result.insert(value);
        start = end + 1;
    }
}

class LaunchController final : public CCNode, public LevelDownloadDelegate, public MusicDownloadDelegate {
public:
    Phase phase = Phase::Idle;
    std::string launchID;
    std::string roomID;
    std::string handledID;
    std::string handledRoom;
    int levelID = 0;
    int64_t requestedAt = 0;
    uint64_t generation = 0;
    bool constructing = false;
    bool nativeReady = false;
    bool redirectQuit = false;
    bool downloadPending = false;
    uint64_t downloadGeneration = 0;
    int downloadID = 0;
    bool arrivalPending = false;
    bool arrivalAcknowledged = false;
    bool cancelPending = false;
    bool musicWatching = false;
    bool contentURLRequested = false;
    unsigned int cancelAttempts = 0;
    unsigned int entryFrames = 0;
    std::string cancelID;
    std::string cancelRoom;
    std::string notice;
    float timer = 0.f;
    float arrivalTimer = 0.f;
    float cancelTimer = 0.f;
    Clock::time_point began;
    Ref<GJGameLevel> level;
    WeakRef<PlayLayer> play;
    WeakRef<CCLabelBMFont> label;
    WeakRef<CCScene> staging;
    std::set<int> songs;
    std::set<int> effects;
    std::set<int> requestedSongs;
    std::set<int> requestedEffects;

    static LaunchController& get() {
        static auto* controller = [] {
            auto* result = new LaunchController;
            result->init();
            CCDirector::sharedDirector()->getScheduler()->scheduleUpdateForTarget(result, 1, false);
            result->release();
            return result;
        }();
        return *controller;
    }

    bool active() const { return phase != Phase::Idle; }
    bool gated(GJBaseGameLayer* candidate) const {
        if (!active() || phase == Phase::Playing || !candidate) return false;
        auto owner = play.lock();
        return owner && static_cast<GJBaseGameLayer*>(owner.data()) == candidate;
    }
    bool belongs(PlayLayer* candidate) const {
        if (!active() || !candidate) return false;
        auto owner = play.lock();
        return owner && owner.data() == candidate;
    }
    bool currentLaunch() const {
        auto const& room = versus::Service::get().room();
        return room && room->id == roomID && room->started && room->launch &&
            room->launch->id == launchID && room->level.id == levelID;
    }
    void text(std::string const& message) {
        if (auto target = label.lock(); target && message != target->getString()) target->setString(message.c_str());
    }
    void startCancel() {
        cancelID = launchID;
        cancelRoom = roomID;
        cancelAttempts = 0;
        cancelTimer = 0.f;
    }
    void retryCancel(float dt) {
        if (cancelID.empty() || cancelPending || cancelAttempts >= 5) return;
        auto& service = versus::Service::get();
        auto const& room = service.room();
        if (!room || room->id != cancelRoom || !room->launch || room->launch->id != cancelID) {
            cancelID.clear();
            return;
        }
        cancelTimer -= dt;
        if (cancelTimer > 0.f || service.busy()) return;
        cancelPending = true;
        ++cancelAttempts;
        auto id = cancelID;
        service.cancelLaunch(id, [this, id](bool success, std::string) {
            cancelPending = false;
            if (cancelID != id) return;
            if (success) cancelID.clear();
            else cancelTimer = 1.f;
        });
    }
    void fail(std::string message, bool cancel = true) {
        if (!active() || phase == Phase::Returning) return;
        notice = std::move(message);
        if (cancel) startCancel();
        phase = Phase::Returning;
    }
    void cancelButton(CCObject*) { fail("Match loading was canceled."); }

    void installLabel(CCNode* parent, bool cancelButtonVisible) {
        auto window = CCDirector::sharedDirector()->getWinSize();
        auto* panel = CCScale9Sprite::create("square02b_001.png");
        panel->setContentSize({340.f, 94.f});
        panel->setColor(ccc3(11, 48, 88));
        panel->setOpacity(235);
        panel->setPosition(window / 2.f);
        auto* root = CCNode::create();
        root->setID("versus-launch-wait"_spr);
        root->addChild(panel);
        auto* title = CCLabelBMFont::create("VERSUS", "bigFont.fnt");
        title->setScale(.55f);
        title->setPosition({window.width / 2.f, window.height / 2.f + 25.f});
        root->addChild(title);
        auto* status = CCLabelBMFont::create("Loading selected map...", "chatFont.fnt");
        status->setScale(.85f);
        status->setAlignment(kCCTextAlignmentCenter);
        status->setPosition({window.width / 2.f, window.height / 2.f - 2.f});
        root->addChild(status);
        label = status;
        if (cancelButtonVisible) {
            auto* menu = CCMenu::create();
            menu->setPosition({window.width / 2.f, window.height / 2.f - 29.f});
            auto* caption = CCLabelBMFont::create("Cancel", "chatFont.fnt");
            caption->setScale(.75f);
            caption->setColor(ccc3(145, 210, 255));
            menu->addChild(CCMenuItemSpriteExtra::create(caption, this, menu_selector(LaunchController::cancelButton)));
            root->addChild(menu);
        }
        parent->addChild(root, 100000);
    }

    void begin() {
        auto const& room = versus::Service::get().room();
        if (!room || !room->launch || !room->started || room->level.id <= 0 ||
            room->level.id > std::numeric_limits<int>::max()) {
            versus::showRoom();
            return;
        }
        if (active()) return;
        ++generation;
        launchID = room->launch->id;
        roomID = room->id;
        handledID = launchID;
        handledRoom = roomID;
        levelID = static_cast<int>(room->level.id);
        requestedAt = room->launch->requestedAt;
        began = Clock::now();
        phase = Phase::Download;
        arrivalPending = false;
        arrivalAcknowledged = false;
        arrivalTimer = 0.f;
        entryFrames = 0;
        nativeReady = false;
        downloadPending = false;
        timer = 0.f;
        notice.clear();
        contentURLRequested = false;
        level = nullptr;
        play = nullptr;
        songs.clear(); effects.clear(); requestedSongs.clear(); requestedEffects.clear();
        auto* scene = CCScene::create();
        auto* bg = CCLayerColor::create(ccc4(5, 25, 62, 255));
        scene->addChild(bg);
        installLabel(scene, true);
        staging = scene;
        // No fade: release the room before constructing a potentially large map.
        CCDirector::sharedDirector()->replaceScene(scene);
        FMODAudioEngine::sharedEngine()->stopAllMusic(true);
        FMODAudioEngine::sharedEngine()->stopAllEffects();
    }

    void requestDownload() {
        if (downloadPending) return;
        downloadPending = true;
        auto epoch = generation;
        versus::ensureMapCached(levelID, [this, epoch](bool success, std::string detail) {
            if (epoch != generation || phase != Phase::Download) return;
            downloadPending = false;
            if (!success) { fail(std::move(detail)); return; }
            level = versus::cachedLevel(levelID);
            phase = Phase::Resources;
            timer = 0.f;
        });
    }
    void finishDownload() {
        downloadPending = false;
        auto* manager = GameLevelManager::sharedState();
        if (manager && manager->m_levelDownloadDelegate == this) manager->m_levelDownloadDelegate = nullptr;
    }
    void levelDownloadFinished(GJGameLevel* downloaded) override {
        if (!downloadPending || !downloaded || downloaded->m_levelID.value() != downloadID) return;
        auto valid = downloadGeneration == generation && phase == Phase::Download;
        finishDownload();
        if (!valid) return;
        if (downloaded->m_levelString.empty()) { fail("The selected map contains no level data."); return; }
        level = downloaded;
        if (downloaded->m_songID > 0) songs.insert(downloaded->m_songID);
        collectIDs(songs, downloaded->m_songIDs);
        collectIDs(effects, downloaded->m_sfxIDs);
        phase = Phase::Resources;
        auto* music = MusicDownloadManager::sharedState();
        music->tryLoadLibraries();
        music->addMusicDownloadDelegate(this);
        musicWatching = true;
        timer = 0.f;
    }
    void levelDownloadFailed(int) override {
        if (!downloadPending) return;
        auto valid = downloadGeneration == generation && phase == Phase::Download;
        finishDownload();
        if (valid) fail("The selected map could not be downloaded.");
    }

    bool resourcesReady() {
        auto* music = MusicDownloadManager::sharedState();
        bool needsContentURL = std::any_of(effects.begin(), effects.end(),
            [music](int id) { return !music->isSFXDownloaded(id); }) ||
            std::any_of(songs.begin(), songs.end(),
                [music](int id) { return id > 10000000 && !music->isSongDownloaded(id); });
        if (needsContentURL && music->m_customContentURL.empty()) {
            if (!contentURLRequested) { contentURLRequested = true; music->getCustomContentURL(); }
            return false;
        }
        bool ready = true;
        int outstanding = 0;
        for (int id : requestedSongs) if (!music->isSongDownloaded(id)) ++outstanding;
        for (int id : requestedEffects) if (!music->isSFXDownloaded(id)) ++outstanding;
        for (int id : songs) {
            if (music->isSongDownloaded(id)) continue;
            ready = false;
            if (requestedSongs.contains(id) || outstanding >= 3 || music->isRunningActionForSongID(id)) continue;
            requestedSongs.insert(id); ++outstanding;
            // downloadSong chooses NG/library URLs itself. getSongInfo only
            // fetches metadata and does not start an audio download.
            music->downloadSong(id);
        }
        for (int id : effects) {
            if (music->isSFXDownloaded(id)) continue;
            ready = false;
            if (requestedEffects.contains(id) || outstanding >= 3 ||
                music->isDLActive(music->getSFXDownloadKey(id))) continue;
            requestedEffects.insert(id); ++outstanding;
            music->downloadSFX(id);
        }
        return ready;
    }
    void stopWatchingMusic() {
        if (!musicWatching) return;
        MusicDownloadManager::sharedState()->removeMusicDownloadDelegate(this);
        musicWatching = false;
    }
    void downloadSongFailed(int id, GJSongError) override {
        if (phase == Phase::Resources && requestedSongs.contains(id))
            fail("The selected map's music could not be downloaded.");
    }
    void downloadSFXFailed(int id, GJSongError) override {
        if (phase == Phase::Resources && requestedEffects.contains(id))
            fail("The selected map's sounds could not be downloaded.");
    }
    void enterLevel() {
        if (!level || !currentLaunch()) { fail("The match was canceled.", false); return; }
        stopWatchingMusic();
        constructing = true;
        phase = Phase::Entering;
        auto* scene = PlayLayer::scene(level.data(), false, false);
        constructing = false;
        if (!scene) { fail("Unable to open the selected map."); return; }
        phase = Phase::Waiting;
        CCDirector::sharedDirector()->replaceScene(scene);
        level = nullptr;
    }
    void attach(PlayLayer* layer) {
        play = nullptr;
        play = layer;
    }
    void entered(PlayLayer* layer) {
        if (!belongs(layer)) return;
        installLabel(layer, true);
        if (layer->m_uiLayer) layer->m_uiLayer->resetAllButtons();
        layer->m_queuedButtons.clear();
    }
    void releaseGame(PlayLayer* layer) {
        if (phase != Phase::Waiting) return;
        layer->m_queuedButtons.clear();
        if (layer->m_uiLayer) layer->m_uiLayer->resetAllButtons();
        if (auto* overlay = layer->getChildByID("versus-launch-wait"_spr)) overlay->removeFromParentAndCleanup(true);
        label = nullptr;
        phase = Phase::Playing;
        versus::battle::begin(layer);
        if (!versus::battle::blocksGameplay(layer)) layer->startGame();
    }
    void prepareQuit() {
        if (!active()) return;
        if (phase != Phase::Returning) {
            notice.clear();
            auto owner = play.lock();
            if (!owner || !versus::battle::activeFor(owner.data())) startCancel();
            phase = Phase::Returning;
        }
        redirectQuit = true;
    }
    void returnScene() {
        stopWatchingMusic();
        auto nativeQuit = redirectQuit;
        auto battleMenu = versus::battle::consumeMainMenuReturn();
        auto message = notice;
        phase = Phase::Idle;
        redirectQuit = false;
        constructing = false;
        play = nullptr;
        label = nullptr;
        staging = nullptr;
        level = nullptr;
        auto& service = versus::Service::get();
        auto const& room = service.room();
        if (battleMenu) {
            CCDirector::sharedDirector()->replaceScene(MenuLayer::scene(false));
        }
        else if (room && room->id == roomID) {
            auto* scene = versus::roomScene();
            CCDirector::sharedDirector()->replaceScene(scene);
        }
        else {
            auto fromService = service.takeNotice();
            if (!fromService.empty()) message = fromService;
            CCDirector::sharedDirector()->replaceScene(versus::lobbyScene(message));
            message.clear();
        }
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_MACOS)
        PlatformToolbox::toggleLockCursor(false);
        PlatformToolbox::showCursor();
#endif
        if (!message.empty()) Loader::get()->queueInMainThread([message] {
            FLAlertLayer::create("Versus", message, "OK")->show();
        });
        if (!nativeQuit) GameManager::sharedState()->fadeInMenuMusic();
    }

    void update(float dt) override {
        retryCancel(dt);
        if (!active() || phase == Phase::Playing) return;
        if (phase == Phase::Returning) {
            if (auto owner = play.lock(); owner && currentSceneContains(owner.data())) {
                redirectQuit = true;
                owner->onQuit();
            }
            else returnScene();
            return;
        }
        if (!currentLaunch()) { fail("The match was canceled or a player left.", false); return; }
        auto& service = versus::Service::get();
        auto launch = *service.room()->launch;
        auto now = service.serverNow();
        if (!launch.releasedAt && ((requestedAt > 0 && now >= requestedAt + 60000) ||
            Clock::now() - began >= std::chrono::seconds(60))) {
            fail("Both players did not enter within 60 seconds.\nPlease try again."); return;
        }
        timer -= dt;
        if (phase == Phase::Download) {
            text("Downloading the selected map...");
            if (timer <= 0.f) { timer = .5f; requestDownload(); }
            return;
        }
        if (phase == Phase::Resources) {
            text("Downloading map music and sounds...");
            if (timer <= 0.f) {
                timer = .25f;
                if (resourcesReady()) enterLevel();
            }
            return;
        }
        if (phase != Phase::Waiting) return;
        auto owner = play.lock();
        if (!owner || !nativeReady || !currentSceneContains(owner.data()) || !owner->m_player1) return;
        if (++entryFrames < 2) return;
        bool ownLoaded = service.isHost() ? launch.hostLoaded : launch.guestLoaded;
        arrivalAcknowledged = arrivalAcknowledged || ownLoaded;
        arrivalTimer -= dt;
        if (!arrivalAcknowledged && !arrivalPending && arrivalTimer <= 0.f && !service.busy()) {
            arrivalPending = true;
            auto currentGeneration = generation;
            service.markLoaded(launchID, [this, currentGeneration](bool success, std::string) {
                if (currentGeneration != generation) return;
                arrivalPending = false;
                arrivalAcknowledged = arrivalAcknowledged || success;
                arrivalTimer = 1.f;
            });
        }
        if (launch.releasedAt && launch.hostLoaded && launch.guestLoaded && arrivalAcknowledged) {
            auto remaining = launch.releasedAt + 3000 - now;
            if (remaining <= 0) { releaseGame(owner.data()); return; }
            text(fmt::format("Both players connected\nStarting in {}...", (remaining + 999) / 1000));
        }
        else {
            auto remaining = std::max<int64_t>(0, requestedAt + 60000 - now);
            text(fmt::format("Waiting for the other player...\n{} seconds remaining", (remaining + 999) / 1000));
        }
    }
};
}

namespace versus {
void launchMatch() { LaunchController::get().begin(); }
bool isMatchLaunchActive() {
    auto& controller = LaunchController::get();
    auto const& room = Service::get().room();
    return controller.active() || (room && room->id == controller.handledRoom && room->launch &&
        room->launch->id == controller.handledID);
}
}

class $modify(VersusMatchPlayLayer, PlayLayer) {
    struct Fields { bool initialized = false; };
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        auto& controller = LaunchController::get();
        if (controller.constructing && level && level->m_levelID.value() == controller.levelID) controller.attach(this);
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        m_fields->initialized = true;
        return true;
    }
    void onEnterTransitionDidFinish() {
        PlayLayer::onEnterTransitionDidFinish();
        LaunchController::get().entered(this);
    }
    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        auto& controller = LaunchController::get();
        if (controller.belongs(this)) controller.nativeReady = true;
    }
    void startGame() {
        if (LaunchController::get().gated(this) || versus::battle::blocksGameplay(this)) return;
        PlayLayer::startGame();
    }
    void startGameDelayed() {
        if (LaunchController::get().gated(this) || versus::battle::blocksGameplay(this)) return;
        PlayLayer::startGameDelayed();
    }
    void startMusic() {
        if (LaunchController::get().gated(this) || versus::battle::blocksGameplay(this)) return;
        PlayLayer::startMusic();
    }
    void pauseGame(bool unfocused) {
        if (LaunchController::get().gated(this) || versus::battle::blocksPause(this)) return;
        PlayLayer::pauseGame(unfocused);
        versus::battle::paused(this, m_isPaused);
    }
    void resume() {
        PlayLayer::resume();
        versus::battle::paused(this, false);
    }
    void resetLevel() {
        auto& controller = LaunchController::get();
        if (m_fields->initialized && controller.nativeReady && controller.gated(this)) return;
        if (!versus::battle::beforeReset(this)) return;
        PlayLayer::resetLevel();
        versus::battle::afterReset(this);
    }
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        // GD uses nullptr to mean player one on several death paths.
        auto* effective = player ? player : m_player1;
        PlayLayer::destroyPlayer(player, object);
        if (effective && (effective == m_player1 || (m_gameState.m_isDualMode && effective == m_player2))) {
            if (effective->m_isDead) versus::battle::died(this);
        }
    }
    void levelComplete() {
        PlayLayer::levelComplete();
        versus::battle::completed(this);
    }
    void showEndLayer() {
        if (versus::battle::activeFor(this)) return;
        PlayLayer::showEndLayer();
    }
    void showRetryLayer() {
        if (versus::battle::activeFor(this)) {
            // Auto-retry may be disabled globally. Its modal must not capture
            // input over the spectator runner or the match result.
            if (!versus::battle::blocksGameplay(this)) resetLevel();
            return;
        }
        PlayLayer::showRetryLayer();
    }
    void togglePracticeMode(bool practice) {
        if (!versus::battle::allowPracticeToggle(this, practice)) return;
        PlayLayer::togglePracticeMode(practice);
    }
    void onQuit() {
        if (versus::battle::requestQuit(this)) return;
        auto& controller = LaunchController::get();
        bool const versusMatch = controller.belongs(this);
        if (versusMatch) {
            controller.prepareQuit();
            versus::battle::detach(this);
        }
        PlayLayer::onQuit();
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_MACOS)
        if (versusMatch) {
            PlatformToolbox::toggleLockCursor(false);
            PlatformToolbox::showCursor();
        }
#endif
    }
};

class $modify(VersusMatchGameLayer, GJBaseGameLayer) {
    void update(float dt) {
        if (LaunchController::get().gated(this) || versus::battle::blocksGameplay(this)) return;
        GJBaseGameLayer::update(dt);
        if (auto* play = PlayLayer::get()) versus::battle::sample(play);
    }
    void handleButton(bool down, int button, bool player1) {
        if (LaunchController::get().gated(this) || versus::battle::blocksInput(this)) return;
        GJBaseGameLayer::handleButton(down, button, player1);
    }
    void processCommands(float dt, bool halfTick, bool lastTick) {
        if (LaunchController::get().gated(this) || versus::battle::blocksInput(this)) return;
        GJBaseGameLayer::processCommands(dt, halfTick, lastTick);
    }
};

class $modify(VersusMatchUILayer, UILayer) {
    bool ccTouchBegan(CCTouch* touch, CCEvent* event) {
        if (LaunchController::get().gated(m_gameLayer) || versus::battle::blocksInput(m_gameLayer)) return false;
        return UILayer::ccTouchBegan(touch, event);
    }
    void keyDown(enumKeyCodes key, double timestamp) {
        if (LaunchController::get().gated(m_gameLayer)) return;
        if (versus::battle::blocksInput(m_gameLayer) && key != KEY_Escape) return;
        UILayer::keyDown(key, timestamp);
    }
    void keyBackClicked() {
        auto& controller = LaunchController::get();
        if (controller.gated(m_gameLayer)) { controller.fail("Match loading was canceled."); return; }
        UILayer::keyBackClicked();
    }
};

class $modify(VersusBattlePauseLayer, PauseLayer) {
    void onQuit(CCObject* sender) {
        auto* layer = PlayLayer::get();
        if (layer && versus::battle::requestQuit(layer)) return;
        PauseLayer::onQuit(sender);
    }
    void onNormalMode(CCObject* sender) {
        if (versus::battle::activeFor(PlayLayer::get())) return;
        PauseLayer::onNormalMode(sender);
    }
    void onPracticeMode(CCObject* sender) {
        if (versus::battle::activeFor(PlayLayer::get())) return;
        PauseLayer::onPracticeMode(sender);
    }
};

class $modify(VersusMatchGameManager, GameManager) {
    void returnToLastScene(GJGameLevel* level) {
        auto& controller = LaunchController::get();
        if (controller.redirectQuit) { controller.returnScene(); return; }
        GameManager::returnToLastScene(level);
    }
};
