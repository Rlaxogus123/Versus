#pragma once
#include "VersusService.hpp"
class GJGameLevel;
namespace versus {
bool mapCached(int64_t levelID);
void ensureMapCached(int64_t levelID, Done callback);
void preloadBattleAssets();
void preloadRunnerAssets();
GJGameLevel* cachedLevel(int64_t levelID);
}
