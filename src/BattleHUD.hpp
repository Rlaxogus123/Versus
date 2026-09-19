#pragma once

#include "VersusService.hpp"
#include <Geode/Geode.hpp>
#include <memory>

namespace versus {
// Screen-space cards. Owns no timers, input handlers, or network state.
class BattleHUD final : public cocos2d::CCNode {
    struct Impl;
    std::unique_ptr<Impl> m;
    bool init(PlayerProfile const&, PlayerProfile const&, GameRules const&);
public:
    BattleHUD();
    ~BattleHUD();
    static BattleHUD* create(PlayerProfile const& host, PlayerProfile const& guest, GameRules const& rules);
    void updatePlayers(BattlePlayerState const& host, BattlePlayerState const& guest);
};
}
