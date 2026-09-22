#include "BattleProgress.hpp"
#include <iostream>
#include <stdexcept>
using namespace versus;
using namespace versus::battle;
void require(bool value, char const* message) { if (!value) throw std::runtime_error(message); }
int main() {
    GameRules rules; rules.attempts = 2;
    BattlePlayerState a, b; a.inAttempt = b.inAttempt = true; a.runNumber = b.runNumber = 1;
    require(finishAttempt(b, rules, 20), "first B death counted");
    require(!finishAttempt(b, rules, 20), "duplicate death ignored");
    b.inAttempt = true; ++b.runNumber;
    require(finishAttempt(b, rules, 35), "last B life counted");
    require(b.attemptsUsed == 2 && b.spectating, "B exhausted immediately spectates");
    require(showRunner(rules, b, a, false, false, false), "B sees runner while A plays");
    require(!showRunner(rules, a, b, false, false, false), "A never sees runner for B depletion");
    a.bestPercent = 80; a.attemptsUsed = 1;
    require(finishAttempt(a, rules, 60), "last A life counted");
    require(!showRunner(rules, a, b, false, false, false), "both done wait for result without runner");
    require(!showRunner(rules, b, a, false, true, false), "result hides runner");
    auto old = a; old.attemptsUsed = 0; old.bestPercent = 10; old.inAttempt = true;
    reconcileProgress(a, old, rules);
    require(a.attemptsUsed == 2 && a.bestPercent == 80 && !a.inAttempt, "stale response cannot revive run");
    rules.attempts = 3; a = {}; b = {};
    a.attemptsUsed = 1; a.inAttempt = true; a.bestPercent = 60;
    b.attemptsUsed = 3; b.bestPercent = 45;
    require(earlyAttemptWin(a, b, rules), "higher percent on fewer attempts resolves early");
    b.attemptsUsed = 2;
    require(!earlyAttemptWin(a, b, rules), "opponent with a remaining attempt can still respond");
    b.attemptsUsed = 3; a.attemptsUsed = 2;
    require(!earlyAttemptWin(a, b, rules), "current run counts toward attempts spent");
    a.attemptsUsed = 1; a.bestPercent = 45;
    require(!earlyAttemptWin(a, b, rules), "equal percent cannot resolve early");
    rules.sequence = true; a = {}; b = {}; b.inAttempt = true;
    require(showRunner(rules, a, b, true, false, false), "second sequence player waits in runner");
    require(!showRunner(rules, a, b, true, false, true), "coin reveal precedes runner");
    rules.practice = true; a.attemptsUsed = 100; a.inAttempt = true;
    require(!terminal(a, rules), "practice not exhausted by attempt limit");
    rules.mode = 1; rules.targetPercent = 40; a.bestPercent = 40;
    require(showRunner(rules, a, b, false, false, false), "target reached waits for reply attempt");
    b.bestPercent = 40;
    require(!showRunner(rules, a, b, false, false, false), "tie waits for result without runner");
    std::cout << "Battle progression regression checks passed\n";
}
