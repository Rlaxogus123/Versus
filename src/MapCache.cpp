#include "MapCache.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/LevelDownloadDelegate.hpp>
#include <Geode/binding/MusicDownloadManager.hpp>
#include <Geode/binding/MusicDownloadDelegate.hpp>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <deque>
#include <limits>
#include <map>
#include <set>

using namespace geode::prelude;
namespace {
using Clock = std::chrono::steady_clock;
void ids(std::set<int>& result, gd::string const& source) {
    std::string value(source);
    for (size_t start = 0; start < value.size();) {
        auto end = value.find(',', start);
        if (end == std::string::npos) end = value.size();
        int id = 0;
        auto parsed = std::from_chars(value.data() + start, value.data() + end, id);
        if (parsed.ec == std::errc() && parsed.ptr == value.data() + end && id > 0) result.insert(id);
        start = end + 1;
    }
}
std::pair<std::set<int>, std::set<int>> assets(GJGameLevel* level) {
    std::pair<std::set<int>, std::set<int>> result;
    if (level->m_songID > 0) result.first.insert(level->m_songID);
    ids(result.first, level->m_songIDs);
    ids(result.second, level->m_sfxIDs);
    return result;
}
class Cache final : public CCNode, public LevelDownloadDelegate, public MusicDownloadDelegate {
    struct Job { int id; std::vector<versus::Done> callbacks; };
    std::deque<Job> queue;
    std::optional<Job> current;
    std::map<int, Ref<GJGameLevel>> levels;
    bool pending = false;
    int pendingID = 0;
    uint64_t generation = 0, requestGeneration = 0;
    bool watchingMusic = false, askedURL = false;
    std::set<int> songs, sounds, requestedSongs, requestedSounds;
    Clock::time_point began;
    float timer = 0.f;
public:
    static Cache& get() {
        static auto* instance = [] {
            auto* cache = new Cache;
            cache->init();
            CCDirector::sharedDirector()->getScheduler()->scheduleUpdateForTarget(cache, 1, false);
            cache->release();
            return cache;
        }();
        return *instance;
    }
    GJGameLevel* level(int id) {
        if (auto found = levels.find(id); found != levels.end()) return found->second.data();
        auto* saved = GameLevelManager::sharedState()->getSavedLevel(id);
        if (!saved || saved->m_levelString.empty()) return nullptr;
        levels.emplace(id, saved);
        return saved;
    }
    bool ready(int id) {
        auto* value = level(id);
        if (!value || value->m_levelString.empty()) return false;
        auto [audio, effects] = assets(value);
        auto* manager = MusicDownloadManager::sharedState();
        return std::all_of(audio.begin(), audio.end(), [manager](int i) { return manager->isSongDownloaded(i); }) &&
            std::all_of(effects.begin(), effects.end(), [manager](int i) { return manager->isSFXDownloaded(i); });
    }
    void add(int id, versus::Done callback) {
        if (ready(id)) { callback(true, {}); return; }
        if (current && current->id == id) { current->callbacks.push_back(std::move(callback)); return; }
        for (auto& job : queue) if (job.id == id) { job.callbacks.push_back(std::move(callback)); return; }
        queue.push_back({id, {std::move(callback)}});
    }
    void finish(bool success, std::string detail) {
        if (!current) return;
        if (watchingMusic) MusicDownloadManager::sharedState()->removeMusicDownloadDelegate(this);
        watchingMusic = false;
        auto callbacks = std::move(current->callbacks);
        current.reset();
        songs.clear(); sounds.clear(); requestedSongs.clear(); requestedSounds.clear();
        // The outstanding level delegate stays owned until its matching native
        // callback; this prevents a canceled response being delivered to a new job.
        for (auto& callback : callbacks) if (callback) callback(success, detail);
    }
    void releaseDelegate() {
        pending = false;
        auto* manager = GameLevelManager::sharedState();
        if (manager->m_levelDownloadDelegate == this) manager->m_levelDownloadDelegate = nullptr;
    }
    void levelDownloadFinished(GJGameLevel* value) override {
        if (!pending) return;
        if (!value) {
            releaseDelegate();
            finish(false, "The selected map returned no data.");
            return;
        }
        if (value->m_levelID.value() != pendingID) return;
        bool valid = current && current->id == pendingID && generation == requestGeneration;
        releaseDelegate();
        if (!value->m_levelString.empty()) levels[value->m_levelID.value()] = value;
        else if (valid) finish(false, "The selected map contains no level data.");
    }
    void levelDownloadFailed(int) override {
        if (!pending) return;
        bool valid = current && current->id == pendingID && generation == requestGeneration;
        releaseDelegate();
        if (valid) finish(false, "The selected map could not be downloaded.");
    }
    void downloadSongFailed(int id, GJSongError) override {
        if (current && requestedSongs.contains(id)) finish(false, "The map's music could not be downloaded.");
    }
    void downloadSFXFailed(int id, GJSongError) override {
        if (current && requestedSounds.contains(id)) finish(false, "The map's sounds could not be downloaded.");
    }
    void update(float dt) override {
        timer -= dt;
        if (timer > 0.f) return;
        timer = .2f;
        // A native request can end without delivering our delegate (another
        // scene may have replaced it). Do not let this block all later retries.
        if (pending && (!current || generation != requestGeneration)) {
            auto* manager = GameLevelManager::sharedState();
            if (!manager->isDLActive(manager->getLevelDownloadKey(pendingID, false, 0))) releaseDelegate();
        }
        if (!current) {
            if (queue.empty()) return;
            current = std::move(queue.front()); queue.pop_front();
            ++generation;
            began = Clock::now();
            askedURL = false;
        }
        if (Clock::now() - began > std::chrono::seconds(60)) { finish(false, "Map download timed out. Try again."); return; }
        auto* value = level(current->id);
        if (!value) {
            auto* manager = GameLevelManager::sharedState();
            if (pending || manager->m_levelDownloadDelegate ||
                manager->isDLActive(manager->getLevelDownloadKey(current->id, false, 0))) return;
            pending = true; pendingID = current->id; requestGeneration = generation;
            manager->m_levelDownloadDelegate = this;
            manager->downloadLevel(pendingID, false, 0);
            return;
        }
        auto* music = MusicDownloadManager::sharedState();
        if (!watchingMusic) {
            auto dependencies = assets(value);
            songs = std::move(dependencies.first); sounds = std::move(dependencies.second);
            music->tryLoadLibraries();
            music->addMusicDownloadDelegate(this); watchingMusic = true;
        }
        if (ready(current->id)) { finish(true, {}); return; }
        bool needsURL = std::any_of(songs.begin(), songs.end(), [music](int i) {
            return i > 10000000 && !music->isSongDownloaded(i);
        }) || std::any_of(sounds.begin(), sounds.end(), [music](int i) { return !music->isSFXDownloaded(i); });
        if (needsURL && music->m_customContentURL.empty()) {
            if (!askedURL) { askedURL = true; music->getCustomContentURL(); }
            return;
        }
        int pendingAssets = 0;
        for (int i : requestedSongs) if (!music->isSongDownloaded(i)) ++pendingAssets;
        for (int i : requestedSounds) if (!music->isSFXDownloaded(i)) ++pendingAssets;
        for (int i : songs) {
            if (pendingAssets >= 3) break;
            if (music->isSongDownloaded(i) || requestedSongs.contains(i) || music->isRunningActionForSongID(i)) continue;
            requestedSongs.insert(i); ++pendingAssets; music->downloadSong(i);
            if (!current) return;
        }
        for (int i : sounds) {
            if (pendingAssets >= 3) break;
            if (music->isSFXDownloaded(i) || requestedSounds.contains(i) || music->isDLActive(music->getSFXDownloadKey(i))) continue;
            requestedSounds.insert(i); ++pendingAssets; music->downloadSFX(i);
            if (!current) return;
        }
    }
};
}
namespace versus {
GJGameLevel* cachedLevel(int64_t id) {
    return id > 0 && id <= std::numeric_limits<int>::max() ? Cache::get().level(static_cast<int>(id)) : nullptr;
}
bool mapCached(int64_t id) {
    return id > 0 && id <= std::numeric_limits<int>::max() && Cache::get().ready(static_cast<int>(id));
}
void ensureMapCached(int64_t id, Done callback) {
    if (id <= 0 || id > std::numeric_limits<int>::max()) { callback(false, "Invalid map ID."); return; }
    Cache::get().add(static_cast<int>(id), std::move(callback));
}
void preloadBattleAssets() {
    CCLabelBMFont::create("VERSUS 0123456789%", "bigFont.fnt");
    CCLabelBMFont::create("Starting / Spectating / Paused", "chatFont.fnt");
    CCSprite::createWithSpriteFrameName("GJ_versusBtn_001.png");
}
}
