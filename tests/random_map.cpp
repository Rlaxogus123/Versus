#include "RandomMapPolicy.hpp"
#include "RoomControls.hpp"
#include <iostream>
#include <stdexcept>

using namespace versus;
void check(bool condition, char const* message) {
    if (!condition) throw std::runtime_error(message);
}
int main() {
    int checks = 0;
    // Exhaust every star count, native demon tier, category and map type.
    for (int category = 0; category < 10; ++category)
    for (bool platformer : {false, true})
    for (bool actualType : {false, true})
    for (int stars = 0; stars <= 11; ++stars)
    for (int difficulty = 1; difficulty <= 10; ++difficulty) {
        MapSelection filter{true, category, platformer};
        LevelInfo level{123, "Test", difficulty, stars, difficulty >= 6, false, actualType};
        int lows[] = {2, 3, 5, 6, 8}, highs[] = {2, 4, 5, 7, 9};
        int demonTiers[] = {7, 8, 6, 9, 10};
        bool expected = platformer == actualType && (category < 5
            ? difficulty < 6 && stars >= lows[category] && stars <= highs[category]
            : stars == 10 && difficulty == demonTiers[category - 5]);
        check(matchesRandomMap(level, filter) == expected, "difficulty/type filter mismatch");
        level.autoLevel = true;
        check(!matchesRandomMap(level, filter), "auto maps must not enter the draw");
        checks += 2;
    }
    LevelInfo valid{123, "Test", 1, 2};
    check(!matchesRandomMap(valid, {true, -1, false}), "invalid category");
    check(!matchesRandomMap(valid, {true, 10, false}), "invalid category");
    valid.id = 0;
    check(!matchesRandomMap(valid, {true, 0, false}), "invalid ID");
    valid.id = 1; valid.name.clear();
    check(!matchesRandomMap(valid, {true, 0, false}), "empty name");
    valid.name.assign(65, 'x');
    check(!matchesRandomMap(valid, {true, 0, false}), "oversize name");
    for (int count = 2; count <= 10; ++count) for (int winner = 0; winner < count; ++winner) {
        double last = 0;
        for (int64_t elapsed = -1000; elapsed <= DRAW_END_MS + 2000; elapsed += 7) {
            auto position = roulettePosition(count, winner, elapsed);
            check(std::isfinite(position) && position >= last && position <= count * 5. + winner, "roulette bounds/monotonicity");
            if (elapsed <= DRAW_LEAD_MS) check(position == 0, "lead-in must remain still");
            if (elapsed >= DRAW_LEAD_MS + DRAW_SPIN_MS) check(position == count * 5. + winner, "wrong endpoint");
            last = position; ++checks;
        }
    }
    RoomInfo room;
    check(!needsRandomDraw(room), "manual mode must bypass roulette");
    room.mapSelection.random = true;
    check(needsRandomDraw(room) && !drawingMap(room), "random pre-draw state");
    room.mapDraw = MapDraw{};
    check(needsRandomDraw(room) && drawingMap(room), "active draw state");
    room.mapDraw->settled = true;
    check(!needsRandomDraw(room) && !drawingMap(room), "settled draw state");
    std::cout << checks << " random-map filter/roulette checks passed\n";
}
