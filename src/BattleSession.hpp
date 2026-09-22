#pragma once
class PlayLayer;
class GJBaseGameLayer;
namespace versus::battle {
void begin(PlayLayer* layer);
bool activeFor(GJBaseGameLayer* layer);
bool blocksGameplay(GJBaseGameLayer* layer);
bool blocksInput(GJBaseGameLayer* layer);
bool blocksPause(GJBaseGameLayer* layer);
void sample(PlayLayer* layer);
void died(PlayLayer* layer);
void completed(PlayLayer* layer);
bool beforeReset(PlayLayer* layer);
void afterReset(PlayLayer* layer);
void paused(PlayLayer* layer, bool paused);
bool allowPracticeToggle(PlayLayer* layer, bool practice);
bool requestQuit(PlayLayer* layer);
bool consumeMainMenuReturn();
}
