#include "RandomMapSearch.hpp"
#include "RandomMapPolicy.hpp"
#include "LevelSelector.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GJSearchObject.hpp>
#include <Geode/binding/LevelManagerDelegate.hpp>
#include <chrono>
#include <charconv>
#include <random>
#include <set>

using namespace geode::prelude;
namespace versus {
namespace {
// Own the native delegate only for our matching search key. Persistent storage
// keeps late GD callbacks safe after timeout/cancellation; no scene pointers.
class RandomSearch final : public CCNode, public LevelManagerDelegate {
    using Clock = std::chrono::steady_clock;
    RandomMapsCallback callback;
    MapSelection filter;
    std::vector<LevelInfo> candidates;
    std::set<int> pages;
    std::mt19937 rng {std::random_device{}()};
    std::string roomID, key;
    Ref<GJSearchObject> search;
    Clock::time_point began, requested, next;
    bool pending = false, completed = false, failed = false;
    int total = 100, requests = 0;

    bool owns(char const* value) const { return pending && value && key == value; }
    void releaseDelegate() {
        auto* manager = GameLevelManager::sharedState();
        if (manager->m_levelManagerDelegate == this) manager->m_levelManagerDelegate = nullptr;
        pending = false;
    }
    void finish(std::string error = {}) {
        releaseDelegate();
        auto done = std::exchange(callback, {});
        search = nullptr;
        if (error.empty() && candidates.size() < 2) error = "Not enough rated maps found. Try again or change the filter.";
        std::shuffle(candidates.begin(), candidates.end(), rng);
        if (candidates.size() > 10) candidates.resize(10);
        auto result = error.empty() ? std::move(candidates) : std::vector<LevelInfo>{};
        if (done) done(std::move(result), std::move(error));
    }
public:
    static RandomSearch& get() {
        static auto* value = [] {
            auto* node = new RandomSearch;
            node->init();
            CCDirector::sharedDirector()->getScheduler()->scheduleUpdateForTarget(node, 1, false);
            node->release();
            return node;
        }();
        return *value;
    }
    void begin(MapSelection value, RandomMapsCallback done) {
        if (callback) { done({}, "Random map search is already running."); return; }
        auto const& room = Service::get().room();
        if (!room || !validSelection(value)) { done({}, "Room/filter changed."); return; }
        callback = std::move(done); filter = value; roomID = room->id;
        candidates.clear(); pages.clear(); requests = 0; total = 100;
        completed = failed = false; began = next = Clock::now();
    }
    void setupPageInfo(gd::string info, char const* value) override {
        if (!owns(value)) return;
        std::string text(info);
        int count = 0;
        auto end = text.find(':');
        std::from_chars(text.data(), text.data() + (end == std::string::npos ? text.size() : end), count);
        if (count > 0) total = std::min(count, 10000);
    }
    void loadLevelsFinished(CCArray* levels, char const* value) override {
        if (!owns(value) || completed) return;
        if (levels) for (auto* level : CCArrayExt<GJGameLevel*>(levels)) {
            auto info = describeLevel(level);
            if (matchesRandomMap(info, filter) && std::none_of(candidates.begin(), candidates.end(),
                [&](auto const& old) { return old.id == info.id; })) candidates.push_back(std::move(info));
        }
        completed = true; // process/release after native callback enumeration
    }
    void loadLevelsFinished(CCArray* levels, char const* value, int) override { loadLevelsFinished(levels, value); }
    void loadLevelsFailed(char const* value) override { if (owns(value)) { completed = true; failed = true; } }
    void loadLevelsFailed(char const* value, int) override { loadLevelsFailed(value); }
    void update(float) override {
        if (!callback) return;
        auto const now = Clock::now();
        auto const& room = Service::get().room();
        if (!room || room->id != roomID || room->mapSelection != filter || !room->guest ||
            !room->hostReady || !room->guestReady || !Service::get().isHost()) {
            finish("Room/filter changed."); return;
        }
        if (now - began > std::chrono::seconds(45)) { finish("Level search timed out. Try again."); return; }
        if (completed) {
            releaseDelegate(); completed = false;
            // Page zero discovers the result count; don't give its popular
            // maps a guaranteed place in every draw when more pages exist.
            if (requests == 1 && total > 10) candidates.clear();
            if (candidates.size() >= 20 || requests >= 7 || pages.size() >= size_t(std::max(1, (total + 9) / 10))) {
                finish(); return;
            }
            if (failed && requests >= 3 && candidates.empty()) { finish("Unable to search rated maps. Try again."); return; }
            failed = false; next = now + std::chrono::milliseconds(400);
        }
        if (pending) {
            if (now - requested > std::chrono::seconds(12)) finish("Level search timed out. Try again.");
            return;
        }
        auto* manager = GameLevelManager::sharedState();
        if (now < next || manager->m_levelManagerDelegate) return;
        int page = 0;
        if (requests) {
            int const maxPage = std::max(0, (total - 1) / 10);
            page = std::uniform_int_distribution<int>(0, maxPage)(rng);
            for (int i = 0; pages.contains(page) && i <= maxPage; ++i) page = (page + 1) % (maxPage + 1);
        }
        pages.insert(page);
        search = GJSearchObject::create(SearchType::MostLiked);
        search->m_page = page;
        search->m_starFilter = true;
        search->m_length = filter.platformer ? "5" : "0,1,2,3,4";
        // User's star groupings differ from GD's difficulty labels (4* and 5*
        // are both native Hard). Query broadly, then enforce exact stars above.
        constexpr char const* difficulties[] = {"1", "2,3", "3", "4", "5", "-2", "-2", "-2", "-2", "-2"};
        search->m_difficulty = difficulties[filter.difficulty];
        if (filter.difficulty >= 5) search->m_demonFilter = static_cast<GJDifficulty>(filter.difficulty - 4);
        key = search->getKey();
        if (manager->isDLActive(key.c_str())) { next = now + std::chrono::seconds(1); return; }
        pending = true; requested = now; ++requests;
        manager->m_levelManagerDelegate = this;
        manager->getOnlineLevels(search);
    }
};
}
void findRandomMaps(MapSelection filter, RandomMapsCallback callback) {
    RandomSearch::get().begin(filter, std::move(callback));
}
}
