#pragma once
#include "VersusService.hpp"
#include <algorithm>

namespace versus::battle {
inline bool terminal(BattlePlayerState const& player, GameRules const& rules) {
    return player.forfeited || player.cheated || player.cleared ||
        (rules.mode == 0 && !rules.practice && player.attemptsUsed >= rules.attempts);
}
inline bool earlyAttemptWin(BattlePlayerState const& candidate, BattlePlayerState const& exhausted,
    GameRules const& rules) {
    if (rules.mode != 0 || rules.practice || candidate.forfeited || candidate.cheated ||
        exhausted.forfeited || exhausted.cheated ||
        exhausted.attemptsUsed < rules.attempts || candidate.bestPercent <= exhausted.bestPercent)
        return false;
    // The ongoing run already spends an attempt, even before its death callback.
    int const spent = candidate.attemptsUsed + (candidate.inAttempt ? 1 : 0);
    return spent < exhausted.attemptsUsed;
}
inline bool finishAttempt(BattlePlayerState& player, GameRules const& rules, int percent) {
    // Native death, the frame observer and reset can all notify the same death.
    if (!player.inAttempt || terminal(player, rules)) return false;
    player.currentPercent = std::clamp(percent, 0, 100);
    player.bestPercent = std::max(player.bestPercent, player.currentPercent);
    ++player.attemptsUsed;
    player.inAttempt = false;
    player.spectating = terminal(player, rules);
    return true;
}
inline void reconcileProgress(BattlePlayerState& local, BattlePlayerState const& confirmed, GameRules const& rules) {
    // A response to a previous run must never undo a newer local death/reset.
    local.attemptsUsed = std::max(local.attemptsUsed, confirmed.attemptsUsed);
    local.bestPercent = std::max(local.bestPercent, confirmed.bestPercent);
    local.cleared = local.cleared || confirmed.cleared;
    local.forfeited = local.forfeited || confirmed.forfeited;
    local.cheated = local.cheated || confirmed.cheated;
    if (terminal(local, rules)) { local.inAttempt = false; local.spectating = true; }
}
inline bool showRunner(GameRules const& rules, BattlePlayerState const& local,
    BattlePlayerState const& opponent, bool waitingForTurn, bool resultKnown, bool revealActive) {
    if (resultKnown || revealActive || local.forfeited || terminal(opponent, rules)) return false;
    bool reached = rules.mode == 1 && local.bestPercent >= rules.targetPercent;
    bool opponentReached = rules.mode == 1 && opponent.bestPercent >= rules.targetPercent;
    return !opponentReached && (waitingForTurn || terminal(local, rules) || reached);
}
}
