#include "MapCache.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/LevelDownloadDelegate.hpp>
#include <Geode/binding/MusicDownloadManager.hpp>
#include <Geode/binding/MusicDownloadDelegate.hpp>
#include <Geode/binding/SongInfoObject.hpp>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <deque>
#include <limits>
#include <map>
#include <set>
#include <utility>

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
    std::set<int> songs, sounds, requestedSongs, requestedSounds, requestedInfo;
    std::map<int, unsigned> songAttempts, soundAttempts, infoAttempts;
    std::map<int, Clock::time_point> songRetryAt, soundRetryAt, infoRetryAt;
    unsigned levelAttempts = 0;
    Clock::time_point began, progressed, levelRetryAt;
    std::string failure;
    size_t completedAssets = 0;
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
        songs.clear(); sounds.clear(); requestedSongs.clear(); requestedSounds.clear(); requestedInfo.clear();
        songAttempts.clear(); soundAttempts.clear(); infoAttempts.clear();
        songRetryAt.clear(); soundRetryAt.clear(); infoRetryAt.clear();
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
        bool valid = current && current->id == pendingID && generation == requestGeneration;
        if (!value) {
            releaseDelegate();
            if (valid) retryLevel("The selected map returned no data.");
            return;
        }
        if (value->m_levelID.value() != pendingID) return;
        releaseDelegate();
        if (!value->m_levelString.empty()) levels[value->m_levelID.value()] = value;
        else if (valid) retryLevel("The selected map contains no level data.");
    }
    void retryLevel(std::string const& detail) {
        // Callbacks can run while native code is enumerating delegates. Finish
        // only from update(), after that enumeration has returned.
        log::warn("Versus map {} download attempt {} failed: {}", pendingID, levelAttempts, detail);
        if (levelAttempts >= 3) failure = detail;
        else levelRetryAt = Clock::now() + std::chrono::seconds(2 * levelAttempts);
    }
    void levelDownloadFailed(int id) override {
        if (!pending || (id != 0 && id != pendingID)) return;
        bool valid = current && current->id == pendingID && generation == requestGeneration;
        releaseDelegate();
        if (valid) retryLevel(fmt::format("Map {} could not be downloaded from the level server.", pendingID));
    }
    void audioFailed(int id, GJSongError error, bool sound, bool info = false) {
        auto& requested = info ? requestedInfo : sound ? requestedSounds : requestedSongs;
        if (!current || !requested.erase(id)) return;
        auto& attempts = info ? infoAttempts : sound ? soundAttempts : songAttempts;
        auto& retryAt = info ? infoRetryAt : sound ? soundRetryAt : songRetryAt;
        auto kind = info ? "Song information" : sound ? "Sound" : "Song";
        log::warn("Versus {} {} failed ({}), attempt {}", kind, id, static_cast<int>(error), attempts[id]);
        if (attempts[id] >= 3) failure = fmt::format("{} {} download failed ({}).", kind, id, static_cast<int>(error));
        else retryAt[id] = Clock::now() + std::chrono::seconds(2 * attempts[id]);
    }
    void loadSongInfoFinished(SongInfoObject* song) override {
        if (current && song && requestedInfo.erase(song->m_songID)) progressed = Clock::now();
    }
    void loadSongInfoFailed(int id, GJSongError error) override { audioFailed(id, error, false, true); }
    void downloadSongFailed(int id, GJSongError error) override { audioFailed(id, error, false); }
    void downloadSFXFailed(int id, GJSongError error) override { audioFailed(id, error, true); }
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
            began = progressed = Clock::now();
            levelRetryAt = began;
            levelAttempts = 0;
            completedAssets = 0;
            failure.clear();
            askedURL = false;
        }
        if (!failure.empty()) { finish(false, std::exchange(failure, {})); return; }
        // Large multi-song maps can take longer than a minute. The launch
        // controller separately enforces its 60-second opponent wait deadline.
        if (Clock::now() - progressed > std::chrono::seconds(120) ||
            Clock::now() - began > std::chrono::minutes(10)) {
            finish(false, "Map/audio download timed out. Try again."); return;
        }
        auto* value = level(current->id);
        if (!value) {
            auto* manager = GameLevelManager::sharedState();
            if (Clock::now() < levelRetryAt || pending || manager->m_levelDownloadDelegate ||
                manager->isDLActive(manager->getLevelDownloadKey(current->id, false, 0))) return;
            pending = true; pendingID = current->id; requestGeneration = generation;
            ++levelAttempts;
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
            progressed = Clock::now();
        }
        if (ready(current->id)) { finish(true, {}); return; }
        bool needsURL = std::any_of(songs.begin(), songs.end(), [music](int i) {
            return i > 10000000 && !music->isSongDownloaded(i);
        }) || std::any_of(sounds.begin(), sounds.end(), [music](int i) { return !music->isSFXDownloaded(i); });
        if (needsURL && music->m_customContentURL.empty()) {
            if (!askedURL) { askedURL = true; music->getCustomContentURL(); }
            return;
        }
        size_t const completed = std::count_if(songs.begin(), songs.end(), [music](int id) { return music->isSongDownloaded(id); }) +
            std::count_if(sounds.begin(), sounds.end(), [music](int id) { return music->isSFXDownloaded(id); });
        if (completed > completedAssets) { completedAssets = completed; progressed = Clock::now(); }
        int pendingAssets = static_cast<int>(requestedInfo.size());
        for (int i : requestedSongs) if (!music->isSongDownloaded(i)) ++pendingAssets;
        for (int i : requestedSounds) if (!music->isSFXDownloaded(i)) ++pendingAssets;
        for (int i : songs) {
            if (pendingAssets >= 3) break;
            if (music->isSongDownloaded(i) || requestedSongs.contains(i) || music->isRunningActionForSongID(i)) continue;
            // NG metadata carries the real audio URL. Fetch it first instead
            // of letting downloadSong fall back to the generic NG endpoint.
            if (i <= 10000000) {
                auto* info = music->getSongInfoObject(i);
                if (!info || info->m_songUrl.empty() || info->m_unloaded) {
                    if (requestedInfo.contains(i) || Clock::now() < infoRetryAt[i]) continue;
                    if (infoAttempts[i] >= 3) { failure = fmt::format("Song {} has no downloadable audio URL.", i); return; }
                    requestedInfo.insert(i); ++infoAttempts[i]; ++pendingAssets;
                    music->getSongInfo(i, true);
                    continue;
                }
            }
            if (Clock::now() < songRetryAt[i]) continue;
            requestedSongs.insert(i); ++songAttempts[i]; ++pendingAssets;
            music->downloadSong(i);
        }
        for (int i : sounds) {
            if (pendingAssets >= 3) break;
            if (music->isSFXDownloaded(i) || requestedSounds.contains(i) || music->isDLActive(music->getSFXDownloadKey(i))) continue;
            if (Clock::now() < soundRetryAt[i]) continue;
            requestedSounds.insert(i); ++soundAttempts[i]; ++pendingAssets; music->downloadSFX(i);
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
