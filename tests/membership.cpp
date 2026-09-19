#include "RoomMembership.hpp"
#include "RoomControls.hpp"
#include "VersusService.hpp"
#include <iostream>
#include <stdexcept>

void check(bool condition, char const* name) {
    if (!condition) throw std::runtime_error(name);
    std::cout << "PASS " << name << '\n';
}

int main() {
    auto parsed = matjson::parse(R"({"host":{"uid":"host"},"hostSeen":100000,"started":false,"hostReady":true})");
    auto room = parsed.unwrap();
    auto original = room.dump();
    check(versus::roomJoinError(room, "guest", 100100, 45000).empty(), "join empty seat");
    check(!versus::hasRoomGuest(room), "empty seat stays empty after join check");
    check(!versus::roomGuestExpired(room, 100100, 45000), "empty seat does not expire or reset host ready");
    check(room.dump() == original, "membership reads do not mutate JSON");
    check(!versus::roomJoinError(room, "host", 100100, 45000).empty(), "host cannot join own room");
    check(!versus::roomJoinError(room, "guest", 145000, 45000).empty(), "host expires at rule boundary");
    room["guest"] = nullptr;
    check(versus::roomJoinError(room, "guest", 100100, 45000).empty(), "join null seat after leave");
    room["guest"] = matjson::Value::object();
    check(versus::roomJoinError(room, "guest", 100100, 45000).empty(), "empty profile is not a player");
    room["guest"]["uid"] = "guest";
    room["guestSeen"] = 100000;
    check(versus::hasRoomGuest(room), "occupied seat detected");
    check(versus::roomJoinError(room, "third", 100100, 45000) == "This room is full.", "third player rejected");
    check(versus::roomJoinError(room, "guest", 100100, 45000).empty(), "existing guest can recover membership");
    check(!versus::roomGuestExpired(room, 144999, 45000), "live guest preserved");
    check(versus::roomGuestExpired(room, 145000, 45000), "guest expiration matches security rule");
    room["started"] = true;
    check(versus::roomJoinError(room, "third", 100100, 45000) == "The match has already started.", "started match rejects joins");
    check(versus::roomJoinError(room, "guest", 100100, 45000).empty(), "recover active guest");
    using versus::readyControl;
    using versus::ReadyAction;
    check(readyControl(false, false, true, false, false, false, false).action == ReadyAction::ChooseMap, "host without map can choose instead of dead Ready");
    check(!readyControl(false, false, false, false, false, false, false).enabled(), "guest cannot choose missing map");
    check(readyControl(false, false, false, true, false, false, false).action == ReadyAction::Download, "failed download remains retryable");
    check(readyControl(false, false, false, true, false, true, false).action == ReadyAction::Download, "unfinished download cannot set Ready");
    check(readyControl(false, false, false, true, true, true, false).action == ReadyAction::Ready, "completed cache overrides stale download UI state");
    check(readyControl(false, false, true, true, false, false, true).action == ReadyAction::Unready, "can unready after cache loss");
    check(!readyControl(false, true, true, true, true, false, false).enabled(), "pending readiness prevents duplicate click");
    check(!readyControl(true, false, true, true, true, false, true).enabled(), "launch locks readiness");
    versus::LevelInfo before, after;
    after.autoLevel = true;
    check(before != after, "auto-level changes invalidate queued readiness");
    after = before; after.demon = true;
    check(before != after, "demon changes invalidate queued readiness");
    versus::GameRules previous, changed;
    changed.practice = true;
    check(previous != changed, "practice changes invalidate queued readiness");
}
