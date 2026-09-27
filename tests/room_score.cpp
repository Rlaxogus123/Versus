#include "RoomScore.hpp"
#include "RandomMapPolicy.hpp"
#include <iostream>
#include <stdexcept>
using namespace versus;
void check(bool condition, char const* text) { if (!condition) throw std::runtime_error(text); }
int main() {
    auto room = matjson::parse(R"({"host":{"uid":"host"},"guest":{"uid":"guest"},"battle":{}})").unwrap();
    resetRoomScore(room);
    auto checkScore = [&](int h, int g) {
        check(room["score"]["host"].asInt().unwrapOr(-1) == h, "host wins");
        check(room["score"]["guest"].asInt().unwrapOr(-1) == g, "guest wins");
    };
    auto finish = [&](std::string id, std::string winner, bool draw = false) {
        room["battle"]["id"] = id; room["battle"]["winnerUid"] = winner;
        room["battle"]["draw"] = draw; room["battle"]["finishedAt"] = 123;
        settleRoomScore(room);
    };
    checkScore(0, 0);
    finish("a", "host"); checkScore(1, 0);
    finish("a", "host"); checkScore(1, 0);
    finish("b", "guest"); checkScore(1, 1);
    finish("c", "host"); checkScore(2, 1);
    finish("d", "", true); checkScore(2, 1);
    room["battle"]["id"] = "e"; room["battle"]["finishedAt"] = 0;
    settleRoomScore(room); checkScore(2, 1);
    resetRoomScore(room); checkScore(0, 0);
    room.erase("guest"); finish("f", "host"); checkScore(0, 0);

    auto legacy = matjson::parse(R"({"host":{"uid":"host"},"guest":{"uid":"guest"},"battle":{"id":"old","winnerUid":"host","draw":false,"finishedAt":123}})").unwrap();
    settleRoomScore(legacy);
    check(legacy["score"]["host"].asInt().unwrapOr(-1) == 1, "legacy room gains score safely");
    auto idle = matjson::Value::object(); auto before = idle.dump();
    settleRoomScore(idle);
    check(idle.dump() == before, "unfinished/missing match reads do not mutate room");

    RoomInfo view; view.host.uid = "host"; view.guest = PlayerProfile{}; view.guest->uid = "guest";
    view.hostWins = 2; view.guestWins = 1;
    check(roomWins(view, true) == 2 && roomWins(view, false) == 1, "stored display");
    view.battle = BattleInfo{}; view.battle->id = "m"; view.battle->winnerUid = "host"; view.battle->finishedAt = 123;
    check(roomWins(view, true) == 3 && roomWins(view, false) == 1, "pending result display");
    view.scoredMatch = "m";
    check(roomWins(view, true) == 2, "committed result not doubled");
    view.guest.reset(); check(roomWins(view, true) == 0 && roomWins(view, false) == 0, "departure display");
    std::cout << "Room win settlement/reset/display checks passed\n";
}
