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
    struct Group {
        RandomMapQuery query;
        std::vector<LevelInfo> candidates;
        std::set<int> pages;
        int total = 100, requests = 0;
        bool exhausted() const { return requests >= 3 || pages.size() >= size_t(std::max(1, (total + 9) / 10)); }
    };
    std::vector<Group> groups;
    size_t groupIndex = 0;
    std::mt19937 rng {std::random_device{}()};
    std::string roomID, key;
    Ref<GJSearchObject> search;
    Clock::time_point began, requested, next;
    bool pending = false, completed = false, failed = false;

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
        std::vector<LevelInfo> candidates;
        for (auto& group : groups) for (auto& level : group.candidates)
            if (std::none_of(candidates.begin(), candidates.end(), [&](auto const& old) { return old.id == level.id; }))
                candidates.push_back(std::move(level));
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
        if (!room || !value.random || !validSelection(value)) { done({}, "Room/filter changed."); return; }
        callback = std::move(done); filter = value; roomID = room->id;
        groups.clear(); groupIndex = 0;
        for (auto query : randomMapQueries(filter)) groups.push_back({query});
        completed = failed = false; began = next = Clock::now();
    }
    void setupPageInfo(gd::string info, char const* value) override {
        if (!owns(value)) return;
        std::string text(info);
        int count = 0;
        auto end = text.find(':');
        std::from_chars(text.data(), text.data() + (end == std::string::npos ? text.size() : end), count);
        if (count > 0) groups[groupIndex].total = std::min(count, 10000);
    }
    void loadLevelsFinished(CCArray* levels, char const* value) override {
        if (!owns(value) || completed) return;
        auto& candidates = groups[groupIndex].candidates;
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
        if (now - began > std::chrono::seconds(90)) { finish(); return; }
        if (completed) {
            releaseDelegate(); completed = false;
            auto& group = groups[groupIndex];
            // Page zero discovers the result count; don't give its popular
            // maps a guaranteed place in every draw when more pages exist.
            if (group.requests == 1 && group.total > 10 && !failed) group.candidates.clear();
            if (failed) group.requests = 3; // skip unavailable tier, don't stall the other tiers
            size_t count = 0;
            bool allSampled = true;
            for (auto const& item : groups) {
                count += item.candidates.size();
                allSampled &= item.requests >= 2 || item.exhausted();
            }
            if ((allSampled && count >= 20) || std::all_of(groups.begin(), groups.end(), [](auto const& g) { return g.exhausted(); })) {
                finish(); return;
            }
            do { groupIndex = (groupIndex + 1) % groups.size(); } while (groups[groupIndex].exhausted());
            failed = false; next = now + std::chrono::milliseconds(400);
        }
        if (pending) {
            if (now - requested > std::chrono::seconds(12)) { completed = true; failed = true; }
            return;
        }
        auto* manager = GameLevelManager::sharedState();
        if (now < next || manager->m_levelManagerDelegate) return;
        auto& group = groups[groupIndex];
        int page = 0;
        if (group.requests) {
            int const maxPage = std::max(0, (group.total - 1) / 10);
            page = std::uniform_int_distribution<int>(0, maxPage)(rng);
            for (int i = 0; group.pages.contains(page) && i <= maxPage; ++i) page = (page + 1) % (maxPage + 1);
        }
        search = GJSearchObject::create(SearchType::MostLiked);
        search->m_page = page;
        search->m_starFilter = true;
        search->m_length = filter.platformer ? "5" : "0,1,2,3,4";
        // User's star groupings differ from GD's difficulty labels (4* and 5*
        // are both native Hard). Query broadly, then enforce exact stars above.
        search->m_difficulty = std::to_string(group.query.difficulty);
        search->m_demonFilter = static_cast<GJDifficulty>(group.query.demonFilter);
        key = search->getKey();
        if (manager->isDLActive(key.c_str())) { next = now + std::chrono::seconds(1); return; }
        group.pages.insert(page);
        pending = true; requested = now; ++group.requests;
        manager->m_levelManagerDelegate = this;
        manager->getOnlineLevels(search);
    }
};
}
void findRandomMaps(MapSelection filter, RandomMapsCallback callback) {
    RandomSearch::get().begin(filter, std::move(callback));
}
}
