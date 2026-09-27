#pragma once
#include "VersusService.hpp"
class GJGameLevel;
namespace versus {
// File-weighted preparation progress, not a byte-weighted network percentage.
// The level endpoint exposes no byte progress; its dependencies are known only
// after the level arrives. Audio uses GD's actual per-request percentages.
struct MapDownloadProgress {
    bool mapReady = false;
    int songsDone = 0, songsTotal = 0, soundsDone = 0, soundsTotal = 0;
    int percent = 0;
    bool complete() const { return mapReady && songsDone == songsTotal && soundsDone == soundsTotal; }
    std::string caption() const;
    std::string detail() const;
};
MapDownloadProgress mapDownloadProgress(int64_t levelID);
bool mapCached(int64_t levelID);
void ensureMapCached(int64_t levelID, Done callback);
void preloadBattleAssets();
void preloadRunnerAssets();
GJGameLevel* cachedLevel(int64_t levelID);
}
