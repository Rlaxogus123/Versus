#pragma once
#include "VersusService.hpp"
#include <algorithm>
#include <cmath>
#include <bit>

namespace versus {
inline constexpr char const* RANDOM_DIFFICULTIES[] = {
    "Easy", "Normal", "Hard", "Harder", "Insane",
    "Easy Demon", "Medium Demon", "Hard Demon", "Insane Demon", "Extreme Demon"
};
inline constexpr int CATEGORY_MASKS[] = {1, 2, 12, 48, 192, 256, 512, 1024, 2048, 4096};
inline constexpr int CATEGORY_FACES[] = {1, 2, 3, 4, 5, 7, 8, 6, 9, 10};
inline constexpr int ALL_MAP_CHOICES = 8191;
inline int legacySelectionMask(int difficulty) {
    return difficulty >= 0 && difficulty < 10 ? CATEGORY_MASKS[difficulty] : 0;
}
inline constexpr int64_t DRAW_LEAD_MS = 2000;
inline constexpr int64_t DRAW_SPIN_MS = 6500;
inline constexpr int64_t DRAW_END_MS = DRAW_LEAD_MS + DRAW_SPIN_MS + 1200;
inline bool validSelection(MapSelection const& filter) {
    return filter.mask >= 0 && filter.mask <= ALL_MAP_CHOICES && (!filter.random || filter.mask != 0);
}
inline bool matchesRandomMap(LevelInfo const& level, MapSelection const& filter) {
    if (!validSelection(filter) || level.id <= 0 || level.name.empty() || level.name.size() > 64 ||
        level.stars <= 0 || level.autoLevel || level.platformer != filter.platformer) return false;
    if (!level.demon) {
        return level.stars >= 2 && level.stars <= 9 && (filter.mask & (1 << (level.stars - 2))) != 0;
    }
    constexpr int demons[] = {7, 8, 6, 9, 10}; // native GJDifficulty values
    for (int i = 0; i < 5; ++i)
        if (level.stars == 10 && level.difficulty == demons[i]) return (filter.mask & (1 << (8 + i))) != 0;
    return false;
}
inline std::string selectionSummary(MapSelection const& filter) {
    int categories = 0, last = 0;
    for (int i = 0; i < 10; ++i) if (filter.mask & CATEGORY_MASKS[i]) { ++categories; last = i; }
    return categories == 1 ? RANDOM_DIFFICULTIES[last] : std::to_string(categories) + " difficulties selected";
}
struct RandomFilterPreview {
    int category;
    std::string stars;
};
inline std::vector<RandomFilterPreview> randomFilterPreview(MapSelection const& filter) {
    std::vector<RandomFilterPreview> result;
    if (!filter.random || !validSelection(filter)) return result;
    for (int category = 0; category < 10; ++category) {
        int bits = filter.mask & CATEGORY_MASKS[category];
        if (!bits) continue;
        std::string stars;
        for (int bit = 0; bit < 13; ++bit) if (bits & (1 << bit)) {
            if (!stars.empty()) stars += "/";
            stars += std::to_string(bit < 8 ? bit + 2 : 10);
        }
        result.push_back({category, std::move(stars)});
    }
    return result;
}
struct RandomMapQuery { int difficulty; int demonFilter; };
inline std::vector<RandomMapQuery> randomMapQueries(MapSelection const& filter) {
    std::vector<RandomMapQuery> result;
    // GD groups 4* and 5* together. Enforce the exact star bits after search.
    constexpr int nativeMasks[] = {1, 2, 12, 48, 192};
    for (int i = 0; i < 5; ++i) if (filter.mask & nativeMasks[i]) result.push_back({i + 1, 0});
    for (int i = 0; i < 5; ++i) if (filter.mask & (1 << (8 + i))) result.push_back({-2, i + 1});
    return result;
}
inline int roomWins(RoomInfo const& room, bool host) {
    if (!room.guest) return 0;
    int wins = host ? room.hostWins : room.guestWins;
    if (room.battle && room.battle->finishedAt > 0 && room.scoredMatch != room.battle->id &&
        !room.battle->draw && room.battle->winnerUid == (host ? room.host.uid : room.guest->uid)) ++wins;
    return wins;
}
inline bool drawingMap(RoomInfo const& room) { return room.mapDraw && !room.mapDraw->settled; }
inline bool needsRandomDraw(RoomInfo const& room) {
    return room.mapSelection.random && (!room.mapDraw || !room.mapDraw->settled);
}
// Position is deterministic for both peers, with several full laps and a
// smooth deceleration ending exactly on the chosen card (no final jump).
inline double roulettePosition(int count, int selected, int64_t elapsed) {
    if (count <= 0) return 0;
    double t = std::clamp(double(elapsed - DRAW_LEAD_MS) / DRAW_SPIN_MS, 0., 1.);
    return (count * 5. + selected) * (1. - std::pow(1. - t, 3.));
}
}
