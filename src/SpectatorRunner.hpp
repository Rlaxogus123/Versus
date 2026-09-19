#pragma once
#include "VersusService.hpp"
#include <Geode/Geode.hpp>
namespace versus {
class SpectatorRunner : public cocos2d::CCLayer {
    struct Impl;
    std::unique_ptr<Impl> m;
    bool init(PlayerProfile const&, GameRules const&);
public:
    SpectatorRunner();
    ~SpectatorRunner();
    static SpectatorRunner* create(PlayerProfile const&, GameRules const&);
    void setOpponentState(BattlePlayerState const&);
    void update(float) override;
    void registerWithTouchDispatcher() override;
    bool ccTouchBegan(cocos2d::CCTouch*, cocos2d::CCEvent*) override;
    void ccTouchEnded(cocos2d::CCTouch*, cocos2d::CCEvent*) override;
    void ccTouchCancelled(cocos2d::CCTouch*, cocos2d::CCEvent*) override;
    void keyDown(cocos2d::enumKeyCodes, double) override;
    void keyUp(cocos2d::enumKeyCodes, double) override;
};
void preloadRunnerAssets();
}
