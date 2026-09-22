#pragma once
class PlayLayer;
class GJBaseGameLayer;
class PlayerObject;
class GameObject;
namespace versus::battle {
void begin(PlayLayer* layer);
bool activeFor(GJBaseGameLayer* layer);
bool blocksGameplay(GJBaseGameLayer* layer);
bool blocksInput(GJBaseGameLayer* layer);
bool blocksPause(GJBaseGameLayer* layer);
void sample(PlayLayer* layer);
void died(PlayLayer* layer);
void survivedLethalHit(PlayLayer* layer, PlayerObject* player, GameObject* hazard);
void completed(PlayLayer* layer);
bool beforeReset(PlayLayer* layer);
void afterReset(PlayLayer* layer);
void paused(PlayLayer* layer, bool paused);
bool allowPracticeToggle(PlayLayer* layer, bool practice);
bool requestQuit(PlayLayer* layer);
bool consumeMainMenuReturn();
}
