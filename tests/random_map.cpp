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
        MapSelection filter{true, CATEGORY_MASKS[category], platformer};
        LevelInfo level{123, "Test", difficulty, stars, difficulty >= 6, false, actualType};
        int lows[] = {2, 3, 4, 6, 8}, highs[] = {2, 3, 5, 7, 9};
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
    check(!matchesRandomMap(valid, {true, 8192, false}), "invalid mask");
    check(!validSelection({true, 0, false}), "empty random selection");
    check(validSelection({false, 0, false}), "manual mode needs no filter");
    for (int mask = 1; mask <= ALL_MAP_CHOICES; ++mask) {
        MapSelection filter{true, mask, false};
        auto preview = randomFilterPreview(filter);
        int previewBits = 0, lastCategory = -1;
        for (auto const& entry : preview) {
            check(entry.category > lastCategory && entry.category < 10, "preview categories ordered and unique");
            lastCategory = entry.category;
            std::string expectedStars;
            for (int bit = 0; bit < 13; ++bit) if (mask & CATEGORY_MASKS[entry.category] & (1 << bit)) {
                previewBits |= 1 << bit;
                if (!expectedStars.empty()) expectedStars += "/";
                expectedStars += std::to_string(bit < 8 ? bit + 2 : 10);
            }
            check(!expectedStars.empty() && entry.stars == expectedStars, "preview shows exact selected stars only");
            ++checks;
        }
        check(previewBits == mask, "preview omits no selected difficulty or star");
        for (int bit = 0; bit < 13; ++bit) {
            LevelInfo sample{123, "Test", bit < 8 ? 3 : CATEGORY_FACES[bit - 3], bit < 8 ? bit + 2 : 10, bit >= 8};
            check(matchesRandomMap(sample, filter) == ((mask & (1 << bit)) != 0), "OR/star selection mismatch");
            ++checks;
        }
    }
    auto queries = randomMapQueries({true, 4 | 8 | 256 | 2048, false});
    auto demons = randomFilterPreview({true, 256 | 1024, false});
    check(demons.size() == 2 && demons[0].category == 5 && demons[1].category == 7 &&
        demons[0].stars == "10" && demons[1].stars == "10", "easy and hard demon preview");
    check(randomFilterPreview({true, 2, false})[0].stars == "3", "unchecked 4-star hidden");
    check(randomFilterPreview({true, 12, true})[0].stars == "4/5", "Hard shows both selected stars");
    check(randomFilterPreview({true, 6, true}).size() == 2, "Normal 3 and Hard 4 have separate faces");
    check(legacySelectionMask(1) == 2 && legacySelectionMask(2) == 12, "legacy category conversion matches native stars");
    check(randomFilterPreview({false, 8191, false}).empty(), "manual mode has no filter preview");
    check(randomFilterPreview({true, 0, false}).empty(), "empty filter preview safe");
    check(randomFilterPreview({true, 8192, false}).empty(), "invalid filter preview safe");
    check(queries.size() == 3, "4/5 stars share one native search");
    check(queries[0].difficulty == 3 && queries[1].demonFilter == 1 && queries[2].demonFilter == 4, "native OR queries");
    valid.id = 0;
    check(!matchesRandomMap(valid, {true, 1, false}), "invalid ID");
    valid.id = 1; valid.name.clear();
    check(!matchesRandomMap(valid, {true, 1, false}), "empty name");
    valid.name.assign(65, 'x');
    check(!matchesRandomMap(valid, {true, 1, false}), "oversize name");
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
