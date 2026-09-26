#include "BattleSession.hpp"
#include "BattleProgress.hpp"
#include "BattleHUD.hpp"
#include "MapCache.hpp"
#include "SpectatorRunner.hpp"
#include "VersusAudio.hpp"
#include "VersusService.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/PauseLayer.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <Geode/binding/CCCircleWave.hpp>
#include <Geode/binding/UILayer.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>

using namespace geode::prelude;
namespace versus::battle {
namespace {
using Clock = std::chrono::steady_clock;
constexpr float RESULT_REVEAL_TIME_SCALE = 1.3f;
constexpr float RESULT_PROGRESS_TIME = 1.4f * RESULT_REVEAL_TIME_SCALE;
constexpr float RESULT_DECISION_TIME = .875f * RESULT_REVEAL_TIME_SCALE;
constexpr float RESULT_SUMMARY_TIME = RESULT_PROGRESS_TIME + RESULT_DECISION_TIME;
constexpr float RESULT_EXECUTION_TIME = 4.2f;
constexpr float RESULT_CONFIRM_TIME = 20.f;
constexpr float RESULT_CONFIRM_START = RESULT_SUMMARY_TIME + RESULT_EXECUTION_TIME;
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

// Use GD's square fragments and star textures, with bounded one-shot emitters.
CCParticleSystemQuad* nativeBurst(CCNode* parent, CCPoint position, char const* preset,
    ccColor3B tint, unsigned count, float duration, float life, float speed, float size) {
    auto* particles = CCParticleSystemQuad::create(preset, false);
    if (!particles) return nullptr;
    particles->setTotalParticles(count);
    particles->setEmitterMode(kCCParticleModeGravity);
    particles->setPositionType(kCCPositionTypeGrouped);
    particles->setSourcePosition({0.f, 0.f});
    particles->setPosition(position);
    particles->setPosVar({4.f, 4.f});
    particles->setDuration(duration);
    particles->setLife(life);
    particles->setLifeVar(life * .25f);
    particles->setEmissionRate(static_cast<float>(count) / duration);
    particles->setAngle(90.f);
    particles->setAngleVar(180.f);
    particles->setSpeed(speed);
    particles->setSpeedVar(speed * .3f);
    particles->setGravity({0.f, -35.f});
    particles->setStartSize(size);
    particles->setStartSizeVar(size * .3f);
    particles->setEndSize(0.f);
    particles->setEndSizeVar(0.f);
    particles->setStartColor({tint.r / 255.f, tint.g / 255.f, tint.b / 255.f, 1.f});
    particles->setStartColorVar({.08f, .08f, .08f, 0.f});
    particles->setEndColor({tint.r / 255.f, tint.g / 255.f, tint.b / 255.f, 0.f});
    particles->setEndColorVar({0.f, 0.f, 0.f, 0.f});
    particles->setBlendAdditive(true);
    particles->setAutoRemoveOnFinish(true);
    parent->addChild(particles, 16);
    particles->resetSystem();
    return particles;
}

void nativeWave(CCNode* parent, CCPoint position, ccColor3B tint, float radius) {
    if (auto* wave = CCCircleWave::create(8.f, radius, .4f, false, true)) {
        wave->m_color = tint;
        wave->setPosition(position);
        parent->addChild(wave, 15);
    }
}

void winnerEffect(CCNode* parent, CCPoint position, PlayerProfile const& player) {
    auto tint = color(player.color1);
    tint = ccc3((tint.r + 255) / 2, (tint.g + 255) / 2, (tint.b + 255) / 2);
    nativeWave(parent, position, tint, 66.f);
    nativeBurst(parent, position, "explodeEffect.plist", tint, 52, .06f, .5f, 105.f, 5.f);
    nativeBurst(parent, position, "glitterEffectIcon.plist", ccc3(255, 228, 135),
        20, .16f, .65f, 45.f, 10.f);
}

void impactEffect(CCNode* parent, CCPoint position, ccColor3B tint) {
    nativeWave(parent, position, ccWHITE, 80.f);
    nativeBurst(parent, position, "explodeEffect.plist", tint, 64, .06f, .48f, 185.f, 7.f);
    nativeBurst(parent, position, "glitterEffectIcon.plist", ccWHITE, 20, .08f, .4f, 110.f, 9.f);
    parent->runAction(CCSequence::create(CCDelayTime::create(.09f),
        CallFuncExt::create([parent, position, tint] { nativeWave(parent, position, tint, 58.f); }), nullptr));
}

void projectileTrail(CCNode* shot, float direction, ccColor3B tint) {
    if (auto* particles = nativeBurst(shot, {0.f, 0.f}, "fireballEffect.plist", tint,
            32, .44f, .2f, 35.f, 5.f)) {
        particles->setPositionType(kCCPositionTypeFree);
        particles->setAngle(direction > 0.f ? 180.f : 0.f);
        particles->setAngleVar(25.f);
        particles->setGravity({0.f, 0.f});
        particles->setZOrder(-1);
    }
}

CCSprite* projectileSprite(char const* frame, ccColor3B tint) {
    auto* sprite = CCSprite::createWithSpriteFrameName(frame);
    if (!sprite) sprite = CCSprite::create("square.png");
    sprite->setColor(tint);
    auto const size = sprite->getContentSize();
    sprite->setScale(26.f / std::max(1.f, std::max(size.width, size.height)));
    return sprite;
}
class Session final : public CCNode {
public:
    WeakRef<PlayLayer> play;
    std::string roomID, battleID, uid, firstUid;
    PlayerProfile hostProfile, guestProfile;
    GameRules rules;
    BattlePlayerState local;
    BattlePlayerState other;
    BattleInfo finalResult;
    bool host = false, active = false, releasing = false, startingTurn = false;
    bool reporting = false, dirty = false, forcingQuit = false, mainMenu = false;
    bool quitDialog = false, warnedPause = false, leaving = false, timedOut = false;
    bool finished = false, spectating = false, returning = false, resultVisible = false;
    bool winnerRevealed = false, executionRevealed = false, confirmationVisible = false;
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
    WeakRef<CCLabelBMFont> hostPercent, guestPercent;
    WeakRef<CCLayerColor> hostGauge, guestGauge;
    WeakRef<CCLayerColor> transitionShade, resultTimerFill;
    WeakRef<CCNodeRGBA> progressSection, decisionBanner, actorSection, resultPopup;
    WeakRef<CCNode> resultRoot;
    float resultTimerWidth = 0.f;
    float displayedHostPercent = 0.f;
    float displayedGuestPercent = 0.f;

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
        if (!active || !layer) return false;
        auto owner = play.lock();
        return owner && static_cast<GJBaseGameLayer*>(owner.data()) == layer;
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
        if (auto* ui = layer->m_uiLayer) {
            if (auto* pause = ui->m_pauseBtn) {
                if (auto* parent = pause->getParent())
                    pause->setPosition(parent->convertToNodeSpace({window.width / 2.f, window.height - 26.f}));
            }
        }
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
        audio::stopResultMusic();
        ++generation;
        // Never swap a live Geode WeakRef from an old PlayLayer to a new one.
        // Its controller releases the old layer while checking the new target,
        // which can run other mods' teardown hooks during level construction.
        play = nullptr;
        play = layer;
        active = true; finished = false; leaving = false; timedOut = false; mainMenu = false; forcingQuit = false;
        reporting = false; dirty = true; quitDialog = false; warnedPause = false;
        returning = false; resultVisible = false;
        winnerRevealed = false; executionRevealed = false; confirmationVisible = false;
        progressSection = nullptr; decisionBanner = nullptr; actorSection = nullptr;
        transitionShade = nullptr; resultTimerFill = nullptr;
        hostPercent = nullptr; guestPercent = nullptr;
        hostGauge = nullptr; guestGauge = nullptr;
        resultPopup = nullptr; resultRoot = nullptr;
        displayedHostPercent = 0.f; displayedGuestPercent = 0.f;
        returnRetry = 0.f; runBest = 0; lastRecordedRun = 0; runner = nullptr;
        reportFailures = 0; reportError.clear();
        host = Service::get().isHost(); uid = Service::get().profile().uid;
        roomID = room->id; battleID = room->battle->id; rules = room->rules;
        firstUid = room->battle->firstUid;
        hostProfile = room->host; guestProfile = *room->guest;
        local = host ? room->battle->host : room->battle->guest;
        other = host ? room->battle->guest : room->battle->host;
        reportTimer = 0.f; uiTimer = 0.f;
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
        if (layer->m_isTestMode || layer->m_isPracticeMode != rules.practice) return;
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
    void detachSession(PlayLayer* layer) {
        if (!active || !layer) return;
        auto owner = play.lock();
        if (!owner || owner.data() != layer) return;
        active = false;
        reporting = false;
        play = nullptr;
        audio::stopResultMusic();
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
    void forfeit(bool leaveRoom) {
        if (!active || finished || leaving) return;
        saveAttempt();
        local.forfeited = true; local.inAttempt = false; local.paused = false;
        local.pausedAt = 0; local.spectating = true; spectating = true;
        dirty = true; reportTimer = 0.f;
        leaving = leaveRoom;
        timedOut = !leaveRoom;
        if (leaveRoom) resultBegan = Clock::now();
        report(true);
        if (!leaveRoom) {
            if (auto owner = play.lock()) dismissPause(owner.data());
        }
        label(status, leaveRoom ? "Leaving match..." : "Pause limit reached. Waiting for result...");
    }
    bool askQuit(PlayLayer* layer) {
        if (!owns(layer) || forcingQuit) return false;
        if (finished) {
            if (confirmationVisible) exitAfterResult();
            return true;
        }
        if (quitDialog || leaving) return true;
        quitDialog = true;
        auto epoch = generation;
        createQuickPopup("Leave Versus?", "Leaving this match counts as a <cr>forfeit</c>.", "Stay", "Leave",
            [this, epoch](FLAlertLayer*, bool yes) {
                if (epoch != generation || !active) return;
                quitDialog = false;
                if (yes) forfeit(true);
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
    void onResultExit(CCObject*) {
        if (finished && confirmationVisible) exitAfterResult();
    }
    void endVisual(BattleInfo const& info) {
        if (finished || leaving) return;
        if (local.inAttempt) {
            auto const& final = host ? info.host : info.guest;
            runBest = std::min(runBest, final.bestPercent);
            saveAttempt();
        }
        finished = true; finalResult = info; resultBegan = Clock::now();
        if (auto game = runner.lock()) game->removeFromParent(); runner = nullptr;
        auto owner = play.lock(); if (!owner) return;
        dismissPause(owner.data());
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_MACOS)
        PlatformToolbox::toggleLockCursor(false);
        PlatformToolbox::showCursor();
#endif
        FMODAudioEngine::sharedEngine()->pauseAllMusic(true);
        auto window = CCDirector::sharedDirector()->getWinSize();
        auto* root = CCNode::create(); root->setID("battle-result"_spr);
        auto* shade = CCLayerColor::create(ccc4(5, 20, 50, 175)); root->addChild(shade);
        auto section = [root](int z) {
            auto* node = CCNodeRGBA::create();
            node->setCascadeOpacityEnabled(true);
            node->setOpacity(0);
            root->addChild(node, z);
            return node;
        };
        auto* progress = section(5); progressSection = progress;
        auto* curtain = CCLayerColor::create(ccc4(0, 0, 0, 0));
        root->addChild(curtain, 10); transitionShade = curtain;
        auto* decision = CCNodeRGBA::create();
        decision->setCascadeOpacityEnabled(true);
        decision->setOpacity(0);
        progress->addChild(decision, 15); decisionBanner = decision;
        auto* actors = section(20); actorSection = actors;
        auto const winnerName = info.winnerUid == hostProfile.uid ? hostProfile.name : guestProfile.name;
        auto* title = CCLabelBMFont::create(info.draw ? "DRAW" : (winnerName + " WINS!").c_str(), "bigFont.fnt");
        title->setPosition({window.width / 2.f, window.height / 2.f + 94.f});
        title->limitLabelWidth(window.width - 40.f, .75f, .3f);
        decision->addChild(title, 10);
        auto resultLabel = [window](CCNode* parent, std::string text, float x, float y, float width, float scale,
                                            char const* font = "chatFont.fnt") {
            auto* value = CCLabelBMFont::create(text.c_str(), font);
            value->setScale(std::min(scale, width / std::max(1.f, value->getContentSize().width)));
            value->setPosition({window.width / 2.f + x, window.height / 2.f + y}); parent->addChild(value, 5); return value;
        };
        resultLabel(decision, info.draw ? "Equal result" : info.winnerUid == uid ? "You won!" : "You lost",
            0.f, 65.f, window.width-40.f, .95f);
        auto* hostPreview = icon(hostProfile, 1.8f);
        auto* guestPreview = icon(guestProfile, 1.8f);
        guestPreview->setFlipX(true);
        hostPreview->setPosition({window.width / 2.f - 95.f, window.height / 2.f - 10.f});
        guestPreview->setPosition({window.width / 2.f + 95.f, window.height / 2.f - 10.f});
        progress->addChild(hostPreview);
        progress->addChild(guestPreview);
        resultLabel(progress, hostProfile.name, -95.f, -56.f, 170.f, .85f);
        resultLabel(progress, guestProfile.name, 95.f, -56.f, 170.f, .85f);
        hostPercent = resultLabel(progress, "0%", -95.f, -79.f, 140.f, .5f, "bigFont.fnt");
        if (auto value = hostPercent.lock()) value->setColor(
            info.draw || info.winnerUid == hostProfile.uid ? ccc3(179, 237, 255) : ccc3(235, 235, 235));
        resultLabel(progress, "VS", 0.f, -79.f, 35.f, .55f, "bigFont.fnt");
        guestPercent = resultLabel(progress, "0%", 95.f, -79.f, 140.f, .5f, "bigFont.fnt");
        if (auto value = guestPercent.lock()) value->setColor(
            info.draw || info.winnerUid == guestProfile.uid ? ccc3(179, 237, 255) : ccc3(235, 235, 235));
        auto gauge = [progress, window](float x, ccColor4B tint) {
            constexpr float width = 145.f;
            auto* track = CCLayerColor::create(ccc4(28, 49, 67, 255));
            track->setContentSize({width, 7.f});
            track->setPosition({window.width / 2.f + x - width / 2.f, window.height / 2.f - 99.f});
            progress->addChild(track, 5);
            auto* fill = CCLayerColor::create(tint);
            fill->setContentSize({0.f, 7.f});
            track->addChild(fill);
            return fill;
        };
        hostGauge = gauge(-95.f, ccc4(80, 205, 255, 255));
        guestGauge = gauge(95.f, ccc4(255, 139, 178, 255));
        if (rules.practice && rules.mode == 0) {
            resultLabel(progress, fmt::format("{} attempts", info.host.attemptsUsed + 1), -95.f, -113.f, 170.f, .6f);
            resultLabel(progress, fmt::format("{} attempts", info.guest.attemptsUsed + 1), 95.f, -113.f, 170.f, .6f);
        }
        returnStatus = resultLabel(root, "", 0.f, -133.f, window.width-40.f, .65f);
        if (auto footer = returnStatus.lock()) footer->setZOrder(40);
        if (!info.draw) {
            auto const& loserState = info.winnerUid == hostProfile.uid ? info.guest : info.host;
            if (loserState.forfeited || loserState.pausedAt > 0) {
                auto* reason = CCLabelBMFont::create(
                    info.winnerUid == uid ? (loserState.forfeited ? "Opponent forfeited" : "Opponent timed out") :
                        (loserState.forfeited ? "Match forfeited" : "Pause timeout"), "chatFont.fnt");
                reason->setScale(.75f);
                reason->setPosition({window.width / 2.f, window.height / 2.f + 36.f});
                decision->addChild(reason);
            }
        }
        auto* a = icon(hostProfile, 1.8f); auto* b = icon(guestProfile, 1.8f); b->setFlipX(true);
        actors->setContentSize(window);
        actors->setAnchorPoint({.5f, .5f});
        actors->setPosition(window / 2.f);
        actors->setVisible(false);
        a->setPosition({window.width / 2.f - 95.f, window.height / 2.f - 10.f});
        b->setPosition({window.width / 2.f + 95.f, window.height / 2.f - 10.f});
        actors->addChild(a); actors->addChild(b);
        if (!info.draw) {
            bool leftWins = info.winnerUid == hostProfile.uid;
            auto* winner = leftWins ? a : b; auto* loser = leftWins ? b : a;
            float direction = leftWins ? 1.f : -1.f;
            unsigned variant = 0;
            for (unsigned char value : battleID) variant = variant * 33u + value;
            variant %= 2u;
            float const executionStart = RESULT_SUMMARY_TIME + .55f;
            float impactDelay = executionStart + .48f;
            if (variant == 0) {
                // Fast fireball with recoil.
                auto* shot = CCNode::create();
                shot->addChild(projectileSprite("fireball_01_001.png", ccc3(255, 193, 82)));
                shot->setPosition(winner->getPosition());
                shot->setScale(.2f);
                shot->setVisible(false);
                actors->addChild(shot, 4);
                impactDelay = executionStart + .48f;
                shot->runAction(CCSequence::create(CCDelayTime::create(executionStart),
                    CCShow::create(),
                    CallFuncExt::create([shot, direction] {
                        projectileTrail(shot, direction, ccc3(255, 157, 65));
                    }),
                    CCSpawn::create(
                        CCEaseSineIn::create(CCMoveBy::create(.48f, {direction * 190.f, 0.f})),
                        CCEaseBackOut::create(CCScaleTo::create(.30f, 1.25f)), nullptr),
                    CCRemoveSelf::create(), nullptr));
                winner->runAction(CCSequence::create(CCDelayTime::create(executionStart - .28f),
                    CCSpawn::create(CCRotateTo::create(.28f, -direction * 22.f),
                        CCMoveBy::create(.28f, {-direction * 18.f, 0.f}), nullptr),
                    CCSpawn::create(CCRotateTo::create(.42f, direction * 8.f),
                        CCMoveBy::create(.42f, {direction * 15.f, 0.f}), nullptr),
                    CCEaseSineOut::create(CCRotateTo::create(.38f, 0.f)), nullptr));
            }
            else {
                // Three staggered energy spikes cross the frame.
                impactDelay = executionStart + .58f;
                for (int i = -1; i <= 1; ++i) {
                    auto* shot = CCNode::create();
                    auto* spike = projectileSprite("spike_01_001.png", ccc3(144, 224, 255));
                    spike->setRotation(direction * 90.f);
                    shot->addChild(spike);
                    shot->setPosition(winner->getPosition() + CCPoint{0.f, i * 16.f});
                    shot->setScale(.35f);
                    shot->setVisible(false);
                    actors->addChild(shot, 4);
                    shot->runAction(CCSequence::create(
                        CCDelayTime::create(executionStart + (i + 1) * .07f),
                        CCShow::create(),
                        CallFuncExt::create([shot, direction] {
                            projectileTrail(shot, direction, ccc3(126, 217, 255));
                        }),
                        CCSpawn::create(
                            CCEaseSineIn::create(CCMoveBy::create(.44f, {direction * 190.f, 0.f})),
                            CCEaseBackOut::create(CCScaleTo::create(.25f, 1.f)), nullptr),
                        CCRemoveSelf::create(), nullptr));
                }
                winner->runAction(CCSequence::create(CCDelayTime::create(executionStart - .30f),
                    CCSpawn::create(CCJumpBy::create(.58f, {direction * 34.f, 0.f}, 30.f, 1),
                        CCRotateBy::create(.58f, -direction * 360.f), nullptr),
                    CCEaseSineOut::create(CCRotateTo::create(.25f, 0.f)), nullptr));
            }

            loser->runAction(CCSequence::create(CCDelayTime::create(impactDelay), CCSpawn::create(
                CCEaseSineIn::create(CCRotateBy::create(1.05f, direction * 540.f)),
                CCEaseSineIn::create(CCMoveBy::create(1.05f, {direction * 135.f, -85.f})),
                CCEaseSineIn::create(CCScaleTo::create(1.05f, .08f)), nullptr), nullptr));

            root->runAction(CCSequence::create(
                CCDelayTime::create(executionStart),
                CallFuncExt::create([] { audio::play(audio::Cue::Throw); }), nullptr));
            root->runAction(CCSequence::create(
                CCDelayTime::create(impactDelay),
                CallFuncExt::create([] { audio::play(audio::Cue::Blast); }), nullptr));

            // Move and tilt the full actor layer as a camera rig.
            actors->runAction(CCSequence::create(
                CCDelayTime::create(executionStart - .25f),
                CCSpawn::create(
                    CCEaseSineOut::create(CCScaleTo::create(.25f, 1.12f)),
                    CCEaseSineOut::create(CCRotateTo::create(.25f, direction * 2.2f)),
                    CCEaseSineOut::create(CCMoveTo::create(.25f,
                        window / 2.f + CCPoint{-direction * 11.f, 4.f})), nullptr),
                CCDelayTime::create(std::max(0.f, impactDelay - executionStart)),
                CCSpawn::create(CCScaleTo::create(.07f, 1.30f),
                    CCRotateTo::create(.07f, -direction * 3.2f),
                    CCMoveBy::create(.07f, {direction * 14.f, -5.f}), nullptr),
                CCMoveBy::create(.045f, {-direction * 9.f, 5.f}),
                CCMoveBy::create(.045f, {direction * 7.f, -4.f}),
                CCSpawn::create(
                    CCEaseSineOut::create(CCScaleTo::create(.58f, 1.06f)),
                    CCEaseSineOut::create(CCRotateTo::create(.58f, 0.f)),
                    CCEaseSineOut::create(CCMoveTo::create(.58f, window / 2.f)), nullptr), nullptr));

            auto* flash = CCLayerColor::create(ccc4(255, 255, 255, 0));
            root->addChild(flash, 28);
            flash->runAction(CCSequence::create(
                CCDelayTime::create(impactDelay),
                CCFadeTo::create(.045f, 195),
                CCFadeTo::create(.20f, 0), nullptr));
            auto const impactPosition = loser->getPosition();
            auto const impactTint = variant == 0 ? ccc3(255, 177, 70) : ccc3(125, 220, 255);
            actors->runAction(CCSequence::create(
                CCDelayTime::create(impactDelay),
                CallFuncExt::create([actors, impactPosition, impactTint] {
                    impactEffect(actors, impactPosition, impactTint);
                }), nullptr));
        }
        auto* popup = CCNodeRGBA::create();
        popup->setCascadeOpacityEnabled(true);
        popup->setOpacity(0); popup->setVisible(false);
        popup->setID("result-confirmation"_spr);
        float const panelWidth = std::min(390.f, window.width - 24.f);
        float const panelHeight = std::min(190.f, window.height - 24.f);
        auto* panel = CCLayerColor::create(ccc4(8, 29, 56, 248));
        panel->setContentSize({panelWidth, panelHeight});
        panel->setPosition({(window.width - panelWidth) / 2.f, (window.height - panelHeight) / 2.f});
        popup->addChild(panel);
        auto popupLabel = [popup, window](std::string const& value, float x, float y,
                                          float width, float scale, char const* font = "bigFont.fnt") {
            auto* text = CCLabelBMFont::create(value.c_str(), font);
            text->setPosition({window.width / 2.f + x, window.height / 2.f + y});
            text->limitLabelWidth(width, scale, .2f);
            popup->addChild(text, 2);
            return text;
        };
        popupLabel("MATCH RESULT", 0.f, 76.f, panelWidth - 24.f, .48f);
        popupLabel(info.draw ? "DRAW" : winnerName + " WINS!", 0.f, 55.f, panelWidth - 24.f, .55f);

        constexpr float playerX = 82.f;
        auto* hostResultIcon = icon(hostProfile,
            !info.draw && info.winnerUid == hostProfile.uid ? 1.35f : 1.05f);
        auto* guestResultIcon = icon(guestProfile,
            !info.draw && info.winnerUid == guestProfile.uid ? 1.35f : 1.05f);
        guestResultIcon->setFlipX(true);
        hostResultIcon->setPosition({window.width / 2.f - playerX, window.height / 2.f + 10.f});
        guestResultIcon->setPosition({window.width / 2.f + playerX, window.height / 2.f + 10.f});
        if (!info.draw && info.winnerUid != hostProfile.uid) hostResultIcon->setOpacity(115);
        if (!info.draw && info.winnerUid != guestProfile.uid) guestResultIcon->setOpacity(115);
        popup->addChild(hostResultIcon, 3);
        popup->addChild(guestResultIcon, 3);
        popupLabel("VS", 0.f, 8.f, 38.f, .42f);
        popupLabel(hostProfile.name, -playerX, -25.f, 125.f, .48f, "chatFont.fnt");
        popupLabel(guestProfile.name, playerX, -25.f, 125.f, .48f, "chatFont.fnt");
        auto* hostScore = popupLabel(fmt::format("{}%", info.host.bestPercent),
            -playerX, -44.f, 100.f, .42f);
        auto* guestScore = popupLabel(fmt::format("{}%", info.guest.bestPercent),
            playerX, -44.f, 100.f, .42f);
        if (info.draw || info.winnerUid == hostProfile.uid) hostScore->setColor(ccc3(179, 237, 255));
        if (info.draw || info.winnerUid == guestProfile.uid) guestScore->setColor(ccc3(179, 237, 255));

        auto* menu = CCMenu::create(); menu->setPosition({window.width / 2.f, window.height / 2.f - 72.f});
        auto* button = ButtonSprite::create("Return to Room", "goldFont.fnt", "GJ_button_01.png");
        button->setScale(.72f);
        menu->addChild(CCMenuItemSpriteExtra::create(button, this, menu_selector(Session::onResultExit)));
        popup->addChild(menu, 3);
        root->addChild(popup, 50); resultPopup = popup;
        resultTimerWidth = std::min(440.f, window.width - 48.f);
        auto* timerTrack = CCLayerColor::create(ccc4(255, 255, 255, 48));
        timerTrack->setContentSize({resultTimerWidth, 2.f});
        timerTrack->setPosition({(window.width - resultTimerWidth) / 2.f, 8.f});
        root->addChild(timerTrack, 60);
        auto* timerFill = CCLayerColor::create(ccc4(255, 255, 255, 240));
        timerFill->setContentSize({resultTimerWidth, 2.f});
        timerFill->setPosition(timerTrack->getPosition());
        root->addChild(timerFill, 61); resultTimerFill = timerFill;
        // PauseLayer and native end screens are siblings of PlayLayer. Put the
        // result above them in the scene so it remains visible for both seats.
        if (auto* scene = owner->getParent()) scene->addChild(root, 100010);
        else owner->addChild(root, 100010);
        resultRoot = root;
        resultVisible = true;
        audio::play(audio::Cue::Finish);
        audio::startResultMusic();
        progress->runAction(CCFadeIn::create(.42f));
    }
    float resultElapsed() const {
        return std::chrono::duration<float>(Clock::now() - resultBegan).count();
    }
    void updateResultVisual(float elapsed, float dt) {
        if (!resultVisible) return;
        float remaining = 0.f;
        if (elapsed < RESULT_SUMMARY_TIME)
            remaining = 1.f - elapsed / RESULT_SUMMARY_TIME;
        else if (elapsed < RESULT_CONFIRM_START)
            remaining = 1.f - (elapsed - RESULT_SUMMARY_TIME) / RESULT_EXECUTION_TIME;
        else
            remaining = 1.f - (elapsed - RESULT_CONFIRM_START) / RESULT_CONFIRM_TIME;
        if (auto bar = resultTimerFill.lock())
            bar->setContentSize({resultTimerWidth * std::clamp(remaining, 0.f, 1.f), 2.f});
        float const lerpAmount = std::clamp(
            -std::expm1(-8.f * std::max(0.f, dt) / RESULT_REVEAL_TIME_SCALE), 0.f, 1.f);
        displayedHostPercent = std::lerp(displayedHostPercent,
            static_cast<float>(finalResult.host.bestPercent), lerpAmount);
        displayedGuestPercent = std::lerp(displayedGuestPercent,
            static_cast<float>(finalResult.guest.bestPercent), lerpAmount);
        if (elapsed >= RESULT_PROGRESS_TIME ||
            std::abs(displayedHostPercent - finalResult.host.bestPercent) < .01f)
            displayedHostPercent = static_cast<float>(finalResult.host.bestPercent);
        if (elapsed >= RESULT_PROGRESS_TIME ||
            std::abs(displayedGuestPercent - finalResult.guest.bestPercent) < .01f)
            displayedGuestPercent = static_cast<float>(finalResult.guest.bestPercent);
        int const hostValue = static_cast<int>(std::round(displayedHostPercent));
        int const guestValue = static_cast<int>(std::round(displayedGuestPercent));
        auto setPercent = [](WeakRef<CCLabelBMFont> const& ref, int value) {
            if (auto item = ref.lock()) {
                auto display = fmt::format("{}%", value);
                if (display != item->getString()) {
                    item->setString(display.c_str());
                    item->limitLabelWidth(140.f, .5f, .2f);
                }
            }
        };
        setPercent(hostPercent, hostValue);
        setPercent(guestPercent, guestValue);
        if (auto bar = hostGauge.lock())
            bar->setContentSize({145.f * displayedHostPercent / 100.f, 7.f});
        if (auto bar = guestGauge.lock())
            bar->setContentSize({145.f * displayedGuestPercent / 100.f, 7.f});
        if (elapsed < RESULT_PROGRESS_TIME) return;
        if (!winnerRevealed) {
            winnerRevealed = true;
            if (auto curtain = transitionShade.lock())
                curtain->runAction(CCFadeTo::create(.21f * RESULT_REVEAL_TIME_SCALE, 65));
            if (auto banner = decisionBanner.lock())
                banner->runAction(CCFadeIn::create(.385f * RESULT_REVEAL_TIME_SCALE));
            if (!finalResult.draw) if (auto result = resultRoot.lock()) {
                auto window = CCDirector::sharedDirector()->getWinSize();
                float const winnerX = finalResult.winnerUid == hostProfile.uid ? -95.f : 95.f;
                winnerEffect(result.data(), {window.width / 2.f + winnerX, window.height / 2.f - 10.f},
                    finalResult.winnerUid == hostProfile.uid ? hostProfile : guestProfile);
            }
        }
        if (elapsed < RESULT_SUMMARY_TIME) {
            return;
        }
        if (elapsed < RESULT_CONFIRM_START) {
            if (!executionRevealed) {
                executionRevealed = true;
                if (!finalResult.draw) {
                    if (auto section = progressSection.lock()) section->runAction(CCSequence::create(
                        CCFadeOut::create(.22f), CCHide::create(), nullptr));
                    if (auto section = actorSection.lock()) {
                        section->setVisible(true);
                        section->setOpacity(0);
                        section->runAction(CCSequence::create(CCDelayTime::create(.22f),
                            CCFadeIn::create(.35f), nullptr));
                    }
                }
            }
            return;
        }
        if (!confirmationVisible) {
            confirmationVisible = true;
            if (auto section = actorSection.lock()) section->runAction(CCSequence::create(
                CCFadeOut::create(.28f), CCHide::create(), nullptr));
            if (auto section = progressSection.lock()) section->runAction(CCSequence::create(
                CCFadeOut::create(.28f), CCHide::create(), nullptr));
            if (auto curtain = transitionShade.lock()) curtain->runAction(CCFadeTo::create(.35f, 120));
            if (auto popup = resultPopup.lock()) {
                popup->setVisible(true);
                popup->runAction(CCSequence::create(CCDelayTime::create(.35f),
                    CCFadeIn::create(.7f), nullptr));
            }
        }
        auto seconds = std::max(0, static_cast<int>(std::ceil(
            RESULT_CONFIRM_START + RESULT_CONFIRM_TIME - elapsed)));
        label(returnStatus, fmt::format("Review result  |  Auto-return in {}s", seconds));
    }
    void exitAfterResult() {
        if (!active || forcingQuit || returning) return;
        auto quit = [this] {
            forcingQuit = true; mainMenu = leaving;
            audio::stopResultMusic();
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
        auto owner = play.lock(); if (!owner) { audio::stopResultMusic(); active = false; return; }
        returnRetry -= dt;
        if (finished || leaving) {
            auto elapsed = resultElapsed();
            if (finished) updateResultVisual(elapsed, dt);
            float const exitTime = resultVisible ? RESULT_CONFIRM_START + RESULT_CONFIRM_TIME : 3.f;
            if (elapsed >= exitTime && returnRetry <= 0.f)
                exitAfterResult();
            return;
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
            if (local.paused && local.pausedAt && now - local.pausedAt >= 30000) forfeit(false);
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
            else if (!finished && !leaving) label(status, reportFailures >= 3 ? "Retrying match sync... " + reportError :
                timedOut ? "Pause limit reached. Waiting for result..." : local.paused ? "Paused - 30s maximum" :
                other.paused ? "Opponent paused" : spectating && terminal(other, rules) ? "Confirming match result..." : "");
        }
    }
};
}
void begin(PlayLayer* layer) { Session::get().beginSession(layer); }
void detach(PlayLayer* layer) { Session::get().detachSession(layer); }
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
