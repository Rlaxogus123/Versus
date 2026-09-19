#pragma once

#include <matjson.hpp>
#include <cstdint>
#include <string>

namespace versus {
// All membership checks are const: mutable matjson subscripts insert objects.
inline bool hasRoomGuest(matjson::Value const& room) {
    return !room["guest"]["uid"].asString().unwrapOr("").empty();
}

inline bool roomGuestExpired(matjson::Value const& room, int64_t now, int64_t timeout) {
    return hasRoomGuest(room) && now - room["guestSeen"].asInt().unwrapOr(0) >= timeout;
}

inline std::string roomJoinError(matjson::Value const& room, std::string const& uid,
    int64_t now, int64_t timeout) {
    if (room["host"]["uid"].asString().unwrapOr("") == uid) return "This is already your room.";
    if (now - room["hostSeen"].asInt().unwrapOr(0) >= timeout) return "The host disconnected.";
    if (room["guest"]["uid"].asString().unwrapOr("") == uid) return {};
    if (room["started"].asBool().unwrapOr(false)) return "The match has already started.";
    if (hasRoomGuest(room)) return "This room is full.";
    return {};
}
}
