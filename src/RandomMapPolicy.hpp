#pragma once
#include "VersusService.hpp"
#include <algorithm>
#include <cmath>

namespace versus {
inline constexpr char const* RANDOM_DIFFICULTIES[] = {
    "Easy 2*", "Normal 3-4*", "Hard 5*", "Harder 6-7*", "Insane 8-9*",
    "Easy Demon", "Medium Demon", "Hard Demon", "Insane Demon", "Extreme Demon"
};
inline constexpr int64_t DRAW_LEAD_MS = 2000;
inline constexpr int64_t DRAW_SPIN_MS = 6500;
inline constexpr int64_t DRAW_END_MS = DRAW_LEAD_MS + DRAW_SPIN_MS + 1200;
inline bool validSelection(MapSelection const& filter) {
    return filter.difficulty >= 0 && filter.difficulty < 10;
}
inline bool matchesRandomMap(LevelInfo const& level, MapSelection const& filter) {
    if (!validSelection(filter) || level.id <= 0 || level.name.empty() || level.name.size() > 64 ||
        level.stars <= 0 || level.autoLevel || level.platformer != filter.platformer) return false;
    if (filter.difficulty < 5) {
        constexpr int low[] = {2, 3, 5, 6, 8};
        constexpr int high[] = {2, 4, 5, 7, 9};
        return !level.demon && level.stars >= low[filter.difficulty] && level.stars <= high[filter.difficulty];
    }
    constexpr int demons[] = {7, 8, 6, 9, 10}; // native GJDifficulty values
    return level.demon && level.stars == 10 && level.difficulty == demons[filter.difficulty - 5];
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
