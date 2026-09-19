#include "BattleSession.hpp"
#include "BattleProgress.hpp"
#include "BattleHUD.hpp"
#include "MapCache.hpp"
#include "SpectatorRunner.hpp"
#include "VersusService.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/PauseLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <Geode/binding/UILayer.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>

using namespace geode::prelude;
namespace versus::battle {
namespace {
using Clock = std::chrono::steady_clock;
ccColor3B color(int rgb) { return ccc3((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255); }
SimplePlayer* icon(PlayerProfile const& profile, float scale = .85f) {
    auto* manager = GameManager::sharedState();
    int const count = manager ? std::max(1, manager->countForType(IconType::Cube)) : 1;
    auto* result = SimplePlayer::create(std::clamp(profile.icon, 1, count));
    result->setColors(color(profile.color1), color(profile.color2));
    result->setGlowOutline(ccWHITE);
    result->setScale(scale);
    return result;
}
class Session final : public CCNode {
public:
    WeakRef<PlayLayer> play;
    std::string roomID, battleID, uid, firstUid;
    PlayerProfile hostProfile, guestProfile;
    GameRules rules;
    BattlePlayerState local;
    BattlePlayerState other;
    bool host = false, active = false, releasing = false, startingTurn = false;
    bool reporting = false, dirty = false, forcingQuit = false, mainMenu = false;
    bool quitDialog = false, warnedPause = false, leaving = false;
    bool finished = false, spectating = false, returning = false;
    int runBest = 0, lastRecordedRun = 0;
    int reportFailures = 0;
    std::string reportError;
    float returnRetry = 0.f;
    WeakRef<SpectatorRunner> runner;
    uint64_t generation = 0;
    float reportTimer = 0.f, uiTimer = 0.f;
    int64_t revealUntil = 0;
    Clock::time_point resultBegan;
    WeakRef<CCNode> hud;
    WeakRef<BattleHUD> playerHUD;
    WeakRef<CCLabelBMFont> status, returnStatus;

    static Session& get() {
        static auto* session = [] {
            auto* node = new Session;
            node->init();
            CCDirector::sharedDirector()->getScheduler()->scheduleUpdateForTarget(node, 2, false);
            node->release();
            return node;
        }();
        return *session;
    }
    bool owns(GJBaseGameLayer* layer) const {
        auto owner = play.lock();
        return active && owner && static_cast<GJBaseGameLayer*>(owner.data()) == layer;
    }
    bool sameBattle() const {
        auto const& room = Service::get().room();
        return room && room->id == roomID && room->battle && room->battle->id == battleID;
    }
    bool sequence() const { return rules.mode == 0 && rules.sequence && !rules.practice; }
    bool done() const {
        return terminal(local, rules);
    }
    bool blocked() const {
        return finished || leaving || spectating || Service::get().serverNow() < revealUntil;
    }
    void label(WeakRef<CCLabelBMFont> const& ref, std::string const& value) {
        if (auto target = ref.lock(); target && value != target->getString()) {
            target->setString(value.c_str());
            target->limitLabelWidth(CCDirector::sharedDirector()->getWinSize().width - 32.f, .65f, .2f);
        }
    }
    void hudSetup(PlayLayer* layer) {
        auto window = CCDirector::sharedDirector()->getWinSize();
        auto* root = CCNode::create();
        root->setID("battle-hud"_spr);
        auto* cards = BattleHUD::create(hostProfile, guestProfile, rules);
        if (cards) {
            root->addChild(cards); playerHUD = cards;
            cards->updatePlayers(host ? local : other, host ? other : local);
        }
        auto make = [root](std::string const& text, CCPoint position, float scale) {
            auto* label = CCLabelBMFont::create(text.c_str(), "chatFont.fnt");
            label->setScale(scale); label->setPosition(position); root->addChild(label); return label;
        };
        auto* info = make("", {window.width / 2.f, window.height - 91.f}, .65f);
        info->setAlignment(kCCTextAlignmentCenter); status = info;
        if (sequence()) {
            auto* reveal = CCNode::create();
            reveal->setPosition(window / 2.f);
            auto* panel = CCScale9Sprite::create("square02b_001.png");
            panel->setContentSize({225.f, 120.f});
            panel->setColor(ccc3(7, 35, 88));
            panel->setOpacity(235);
            reveal->addChild(panel);
            auto const& first = firstUid == hostProfile.uid ? hostProfile : guestProfile;
            auto* firstIcon = icon(first, 1.35f);
            firstIcon->setPosition({0.f, 9.f});
            reveal->addChild(firstIcon, 2);
            auto* title = make("FIRST TURN", {0.f, 45.f}, .7f);
            title->retain(); title->removeFromParentAndCleanup(false); reveal->addChild(title); title->release();
            auto* name = make(first.name, {0.f, -38.f}, .8f);
            name->retain(); name->removeFromParentAndCleanup(false); reveal->addChild(name); name->release();
            root->addChild(reveal, 20);
            firstIcon->runAction(CCRepeatForever::create(CCSequence::create(
                CCEaseSineInOut::create(CCScaleTo::create(.35f, 1.55f, .2f)),
                CCEaseSineInOut::create(CCScaleTo::create(.35f, 1.35f, 1.35f)), nullptr)));
            reveal->runAction(CCSequence::create(CCDelayTime::create(4.65f),
                CCSpawn::create(CCFadeOut::create(.3f), CCScaleTo::create(.3f, .8f), nullptr),
                CCRemoveSelf::create(), nullptr));
        }
        layer->addChild(root, 100005); hud = root;
    }
    void beginSession(PlayLayer* layer) {
        auto const& room = Service::get().room();
        if (!room || !room->battle || !room->guest) return;
        ++generation;
        active = true; finished = false; leaving = false; mainMenu = false; forcingQuit = false;
        reporting = false; dirty = true; quitDialog = false; warnedPause = false;
        returning = false; returnRetry = 0.f; runBest = 0; lastRecordedRun = 0; runner = nullptr;
        reportFailures = 0; reportError.clear();
        host = Service::get().isHost(); uid = Service::get().profile().uid;
        roomID = room->id; battleID = room->battle->id; rules = room->rules;
        firstUid = room->battle->firstUid;
        hostProfile = room->host; guestProfile = *room->guest;
        local = host ? room->battle->host : room->battle->guest;
        other = host ? room->battle->guest : room->battle->host;
        play = layer; reportTimer = 0.f; uiTimer = 0.f;
        revealUntil = sequence() && room->launch ? room->launch->releasedAt + 8000 : 0;
        spectating = sequence() && room->battle->activeUid != uid;
        local.spectating = spectating;
        if (!spectating) { local.inAttempt = true; local.runNumber = std::max(1, local.runNumber); }
        releasing = true;
        if (layer->m_isPracticeMode != rules.practice) layer->togglePracticeMode(rules.practice);
        releasing = false;
        preloadBattleAssets();
        hudSetup(layer);
        if (blocked()) FMODAudioEngine::sharedEngine()->pauseAllMusic(true);
    }
    int percent(PlayLayer* layer) const { return std::clamp(layer->getCurrentPercentInt(), 0, 100); }
    void sampleState(PlayLayer* layer) {
        if (!owns(layer) || releasing || startingTurn || blocked() || local.paused || !local.inAttempt) return;
        if ((layer->m_player1 && layer->m_player1->m_isDead) ||
            (layer->m_gameState.m_isDualMode && layer->m_player2 && layer->m_player2->m_isDead)) {
            death(layer); return;
        }
        int current = percent(layer);
        if (current != local.currentPercent) { local.currentPercent = current; dirty = true; }
        local.bestPercent = std::max(local.bestPercent, current);
        runBest = std::max(runBest, current);
        if (rules.mode == 1 && current >= rules.targetPercent) {
            // Reaching the percentage target ends this run without claiming a
            // full level clear. The other player may still tie on their current run.
            saveAttempt();
            local.inAttempt = false; local.spectating = true; spectating = true; dirty = true;
            FMODAudioEngine::sharedEngine()->pauseAllMusic(true);
        }
    }
    void saveAttempt() {
        if (local.runNumber <= lastRecordedRun || local.runNumber <= 0) return;
        Service::get().recordAttempt(local.runNumber, runBest);
        lastRecordedRun = local.runNumber;
    }
    void death(PlayLayer* layer) {
        if (!owns(layer) || releasing || startingTurn || !local.inAttempt || blocked()) return;
        if (!finishAttempt(local, rules, percent(layer))) return;
        runBest = std::max(runBest, local.currentPercent); saveAttempt();
        dirty = true; reportTimer = 0.f;
        if (done()) {
            spectating = true; local.spectating = true;
            FMODAudioEngine::sharedEngine()->pauseAllMusic(true);
        }
    }
    void clear(PlayLayer* layer) {
        if (!owns(layer) || local.cleared || finished || leaving || spectating) return;
        local.currentPercent = 100; local.bestPercent = 100; local.cleared = true;
        runBest = 100; saveAttempt();
        local.inAttempt = false; local.spectating = true; spectating = true; dirty = true; reportTimer = 0.f;
        FMODAudioEngine::sharedEngine()->pauseAllMusic(true);
    }
    bool resetBefore(PlayLayer* layer) {
        if (!owns(layer) || releasing) return true;
        if (startingTurn) return true;
        if (blocked() || done()) return false;
        if (local.inAttempt) death(layer); // A manual restart consumes the current attempt too.
        return !done();
    }
    void resetAfter(PlayLayer* layer) {
        if (!owns(layer) || releasing || done()) return;
        local.currentPercent = 0; local.inAttempt = true; local.spectating = false; spectating = false;
        ++local.runNumber; runBest = 0; dirty = true; reportTimer = 0.f;
    }
    void pause(PlayLayer* layer, bool value) {
        if (!owns(layer) || finished || local.paused == value) return;
        local.paused = value; local.pausedAt = value ? Service::get().serverNow() : 0;
        warnedPause = false; dirty = true; reportTimer = 0.f;
    }
    void report(bool force = false) {
        if (!active || finished || !sameBattle() || reporting || (!dirty && !force)) return;
        bool position = false; // Local spectator runner needs only progress.
        reporting = true; dirty = false;
        auto epoch = generation;
        Service::get().reportBattle(local, position, [this, epoch](bool success, std::string detail) {
            if (epoch != generation) return;
            reporting = false;
            if (!success) {
                dirty = true;
                if (detail != "Room update in progress.") {
                    reportError = std::move(detail);
                    if (++reportFailures == 3) log::warn("Versus progress update failed: {}", reportError);
                }
            }
            else { reportFailures = 0; reportError.clear(); }
        });
    }
    void forfeit() {
        if (!active || finished || leaving) return;
        saveAttempt();
        local.forfeited = true; local.inAttempt = false; local.paused = false;
        local.pausedAt = 0; local.spectating = true; spectating = true;
        dirty = true; reportTimer = 0.f; leaving = true;
        resultBegan = Clock::now();
        report(true);
        label(status, "Leaving match...");
    }
    bool askQuit(PlayLayer* layer) {
        if (!owns(layer) || forcingQuit) return false;
        if (finished) { exitAfterResult(); return true; }
        if (quitDialog || leaving) return true;
        quitDialog = true;
        auto epoch = generation;
        createQuickPopup("Leave Versus?", "Leaving this match counts as a <cr>forfeit</c>.", "Stay", "Leave",
            [this, epoch](FLAlertLayer*, bool yes) {
                if (epoch != generation || !active) return;
                quitDialog = false;
                if (yes) forfeit();
            });
        return true;
    }
    void dismissPause(PlayLayer* owner) {
        // GD attaches PauseLayer to the scene, above every PlayLayer child.
        // Remove it before presenting results and unpause the action manager.
        auto* parent = owner->getParent();
        auto* children = parent ? parent->getChildren() : nullptr;
        if (children) {
            for (int i = static_cast<int>(children->count()) - 1; i >= 0; --i) {
                if (auto* pause = typeinfo_cast<PauseLayer*>(children->objectAtIndex(i)))
                    pause->removeFromParentAndCleanup(true);
            }
        }
        if (owner->m_isPaused) owner->resume();
    }
    void endVisual(BattleInfo const& info) {
        if (finished || leaving) return;
        if (local.inAttempt) {
            auto const& final = host ? info.host : info.guest;
            runBest = std::min(runBest, final.bestPercent);
            saveAttempt();
        }
        finished = true; resultBegan = Clock::now();
        if (auto game = runner.lock()) game->removeFromParent(); runner = nullptr;
        auto owner = play.lock(); if (!owner) return;
        dismissPause(owner.data());
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_MACOS)
        PlatformToolbox::showCursor();
#endif
        FMODAudioEngine::sharedEngine()->pauseAllMusic(true);
        auto window = CCDirector::sharedDirector()->getWinSize();
        auto* root = CCNode::create(); root->setID("battle-result"_spr);
        auto* shade = CCLayerColor::create(ccc4(5, 20, 50, 220)); root->addChild(shade);
        std::string outcome = info.draw ? "DRAW" : info.winnerUid == uid ? "VICTORY" : "DEFEAT";
        auto* title = CCLabelBMFont::create(outcome.c_str(), "bigFont.fnt");
        title->setPosition({window.width / 2.f, window.height / 2.f + 94.f}); title->setScale(.75f); root->addChild(title);
        auto resultLabel = [root, window](std::string text, float x, float y, float width, float scale) {
            auto* value = CCLabelBMFont::create(text.c_str(), "chatFont.fnt");
            value->setScale(std::min(scale, width / std::max(1.f, value->getContentSize().width)));
            value->setPosition({window.width / 2.f + x, window.height / 2.f + y}); root->addChild(value, 5); return value;
        };
        auto winnerName = info.winnerUid == hostProfile.uid ? hostProfile.name : guestProfile.name;
        resultLabel(info.draw ? "Draw - equal result" : winnerName + " wins!", 0.f, 65.f, window.width-40.f, .95f);
        resultLabel(hostProfile.name, -95.f, -56.f, 170.f, .85f);
        resultLabel(guestProfile.name, 95.f, -56.f, 170.f, .85f);
        resultLabel(fmt::format("{}%", info.host.bestPercent), -95.f, -77.f, 170.f, 1.1f);
        resultLabel(fmt::format("{}%", info.guest.bestPercent), 95.f, -77.f, 170.f, 1.1f);
        if (rules.practice && rules.mode == 0) {
            resultLabel(fmt::format("{} attempts", info.host.attemptsUsed + 1), -95.f, -96.f, 170.f, .6f);
            resultLabel(fmt::format("{} attempts", info.guest.attemptsUsed + 1), 95.f, -96.f, 170.f, .6f);
        }
        returnStatus = resultLabel("Returning to the room...", 0.f, -118.f, window.width-40.f, .65f);
        if (!info.draw) {
            auto const& loserState = info.winnerUid == hostProfile.uid ? info.guest : info.host;
            if (loserState.forfeited || loserState.pausedAt > 0) {
                auto* reason = CCLabelBMFont::create(
                    info.winnerUid == uid ? (loserState.forfeited ? "Opponent forfeited" : "Opponent timed out") :
                        (loserState.forfeited ? "Match forfeited" : "Pause timeout"), "chatFont.fnt");
                reason->setScale(.75f);
                reason->setPosition({window.width / 2.f, window.height / 2.f + 36.f});
                root->addChild(reason);
            }
        }
        auto* a = icon(hostProfile, 1.8f); auto* b = icon(guestProfile, 1.8f); b->setFlipX(true);
        a->setPosition({window.width / 2.f - 95.f, window.height / 2.f - 10.f});
        b->setPosition({window.width / 2.f + 95.f, window.height / 2.f - 10.f});
        root->addChild(a); root->addChild(b);
        if (!info.draw) {
            bool leftWins = info.winnerUid == hostProfile.uid;
            auto* winner = leftWins ? a : b; auto* loser = leftWins ? b : a;
            float direction = leftWins ? 1.f : -1.f;
            unsigned variant = 0;
            for (unsigned char value : battleID) variant = variant * 33u + value;
            variant %= 3u;
            if (variant == 0) {
                // Dash-punch.
                winner->runAction(CCSequence::create(CCDelayTime::create(.5f),
                    CCEaseBackIn::create(CCMoveBy::create(.22f, {direction * 145.f, 0.f})),
                    CCMoveBy::create(.35f, {-direction * 45.f, 0.f}), nullptr));
            }
            else {
                // Fireball or spike volley, selected consistently for both clients.
                auto* shot = CCDrawNode::create();
                if (variant == 1) {
                    shot->drawDot({0.f, 0.f}, 10.f, {1.f, .28f, .08f, 1.f});
                    shot->drawDot({0.f, 0.f}, 5.f, {1.f, .92f, .28f, 1.f});
                }
                else {
                    CCPoint spike[] = {{direction * 15.f, 0.f}, {-direction * 10.f, 9.f}, {-direction * 7.f, 0.f}, {-direction * 10.f, -9.f}};
                    shot->drawPolygon(spike, 4, {.65f, .9f, 1.f, 1.f}, 1.5f, {1.f, 1.f, 1.f, 1.f});
                }
                shot->setPosition(winner->getPosition());
                shot->setScale(.2f);
                root->addChild(shot, 4);
                shot->runAction(CCSequence::create(CCDelayTime::create(.45f),
                    CCSpawn::create(CCMoveBy::create(.35f, {direction * 190.f, 0.f}),
                        CCEaseBackOut::create(CCScaleTo::create(.2f, 1.f)), nullptr),
                    CCRemoveSelf::create(), nullptr));
                winner->runAction(CCSequence::create(CCDelayTime::create(.42f),
                    CCRotateBy::create(.12f, -direction * 18.f),
                    CCRotateBy::create(.2f, direction * 18.f), nullptr));
            }
            float const impactDelay = variant == 0 ? .72f : .80f;
            loser->runAction(CCSequence::create(CCDelayTime::create(impactDelay), CCSpawn::create(
                CCRotateBy::create(.5f, direction * 360.f), CCMoveBy::create(.5f, {direction * 100.f, -70.f}),
                CCScaleTo::create(.5f, 0.f), nullptr), nullptr));
        }
        owner->addChild(root, 100010);
    }
    void exitAfterResult() {
        if (!active || forcingQuit || returning) return;
        auto quit = [this] {
            forcingQuit = true; mainMenu = leaving;
            auto owner = play.lock();
            if (leaving) Service::get().leaveRoom([](bool, std::string) {});
            if (owner) owner->onQuit();
            active = false;
        };
        if (leaving || !sameBattle()) { quit(); return; }
        returning = true; auto epoch = generation;
        Service::get().acknowledgeResult([this, epoch, quit](bool ok, std::string detail) {
            if (epoch != generation) return;
            returning = false;
            if (ok) quit();
            else { returnRetry = 1.f; label(returnStatus, detail); }
        });
    }
    void updateSpectator(PlayLayer*, float) {
        auto const& room = Service::get().room();
        bool waitingTurn = sequence() && room && room->battle && room->battle->activeUid != uid && !done();
        bool show = showRunner(rules, local, other, waitingTurn, finished || leaving,
            Service::get().serverNow() < revealUntil);
        auto game = runner.lock();
        if (show && !game) {
            auto owner = play.lock();
            if (owner) {
                auto* created = SpectatorRunner::create(host ? guestProfile : hostProfile, rules);
                if (created) { owner->addChild(created, 100006); runner = created; game = runner.lock(); }
            }
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_MACOS)
            PlatformToolbox::showCursor();
#endif
        }
        if (game) {
            if (show) game->setOpponentState(other);
            else { game->removeFromParent(); runner = nullptr; }
        }
    }
    void update(float dt) override {
        if (!active) return;
        auto owner = play.lock(); if (!owner) { active = false; return; }
        returnRetry -= dt;
        if ((finished || leaving) && Clock::now() - resultBegan >= std::chrono::seconds(3)) {
            if (returnRetry <= 0.f) exitAfterResult(); return;
        }
        if (!sameBattle()) {
            if (!finished && !leaving) {
                // A canceled/deleted match is not evidence that we won. In
                // particular, never acknowledge or reset a newer match here.
                finished = true; resultBegan = Clock::now();
                if (auto game = runner.lock()) game->removeFromParent(); runner = nullptr;
                dismissPause(owner.data());
                FMODAudioEngine::sharedEngine()->pauseAllMusic(true);
                label(status, "Match ended. Returning to the room...");
            }
            return;
        }
        auto const snapshot = *Service::get().room()->battle;
        other = host ? snapshot.guest : snapshot.host;
        reconcileProgress(local, host ? snapshot.host : snapshot.guest, rules);
        if (done()) spectating = true;
        if (snapshot.finishedAt && !finished && !leaving) endVisual(snapshot);
        auto now = Service::get().serverNow();
        if (!finished && !leaving) {
            sampleState(owner.data());
            if (local.paused && local.pausedAt && now - local.pausedAt >= 30000) forfeit();
            else if (local.paused && local.pausedAt && now - local.pausedAt >= 20000 && !warnedPause) {
                warnedPause = true;
                FLAlertLayer::create("Versus", "Paused for 20 seconds.\nResume before 30 seconds or forfeit.", "OK")->show();
            }
            if (other.paused && other.pausedAt && now - other.pausedAt >= 30000) dirty = true;
            if (sequence() && now >= revealUntil && !done() && snapshot.activeUid == uid && spectating) {
                spectating = false; local.spectating = false;
                startingTurn = true;
                owner->resetLevel();
                startingTurn = false;
                owner->m_player1->setVisible(true);
                if (auto game = runner.lock()) game->setVisible(false);
                owner->startGame();
                FMODAudioEngine::sharedEngine()->resumeAllMusic();
            }
            if (revealUntil && now >= revealUntil) {
                revealUntil = 0;
                if (!spectating) { owner->startGame(); FMODAudioEngine::sharedEngine()->resumeAllMusic(); }
            }
        }
        updateSpectator(owner.data(), dt);
        reportTimer -= dt;
        if (reportTimer <= 0.f) {
            reportTimer = .5f;
            report(local.paused);
        }
        uiTimer -= dt;
        if (uiTimer <= 0.f) {
            uiTimer = .1f;
            if (auto cards = playerHUD.lock()) cards->updatePlayers(host ? local : other, host ? other : local);
            if (revealUntil > now) {
                auto const& name = firstUid == hostProfile.uid ? hostProfile.name : guestProfile.name;
                label(status, fmt::format("{} goes first\nStarting in {}", name, (revealUntil - now + 999) / 1000));
            }
            else if (!finished && !leaving) label(status, reportFailures >= 3 ? "Retrying match sync... " + reportError : local.paused ? "Paused - 30s maximum" :
                other.paused ? "Opponent paused" : spectating && terminal(other, rules) ? "Confirming match result..." : "");
        }
    }
};
}
void begin(PlayLayer* layer) { Session::get().beginSession(layer); }
bool activeFor(GJBaseGameLayer* layer) { return Session::get().owns(layer); }
bool blocksGameplay(GJBaseGameLayer* layer) { auto& s = Session::get(); return s.owns(layer) && s.blocked(); }
bool blocksInput(GJBaseGameLayer* layer) { return blocksGameplay(layer); }
bool blocksPause(GJBaseGameLayer* layer) { auto& s = Session::get(); return s.owns(layer) && (s.finished || s.leaving); }
void sample(PlayLayer* layer) { Session::get().sampleState(layer); }
void died(PlayLayer* layer) { Session::get().death(layer); }
void completed(PlayLayer* layer) { Session::get().clear(layer); }
bool beforeReset(PlayLayer* layer) { return Session::get().resetBefore(layer); }
void afterReset(PlayLayer* layer) { Session::get().resetAfter(layer); }
void paused(PlayLayer* layer, bool value) { Session::get().pause(layer, value); }
bool allowPracticeToggle(PlayLayer* layer, bool practice) {
    auto& s = Session::get(); return !s.owns(layer) || s.releasing || practice == s.rules.practice;
}
bool requestQuit(PlayLayer* layer) { return Session::get().askQuit(layer); }
bool consumeMainMenuReturn() {
    auto& s = Session::get(); bool value = s.mainMenu; s.mainMenu = false; return value;
}
}
