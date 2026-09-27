#pragma once
#include <matjson.hpp>
#include <string>
#include <utility>

namespace versus {
inline void resetRoomScore(matjson::Value& room) {
    auto score = matjson::Value::object();
    score["host"] = 0; score["guest"] = 0; score["lastMatch"] = "";
    room["score"] = std::move(score);
}
inline void settleRoomScore(matjson::Value& room) {
    auto const& snapshot = std::as_const(room);
    auto const& battle = snapshot["battle"];
    if (battle["finishedAt"].asInt().unwrapOr(0) <= 0 || !snapshot["guest"].isObject()) return;
    auto const id = battle["id"].asString().unwrapOr("");
    if (id.empty() || snapshot["score"]["lastMatch"].asString().unwrapOr("") == id) return;
    auto score = matjson::Value::object();
    int host = snapshot["score"]["host"].asInt().unwrapOr(0);
    int guest = snapshot["score"]["guest"].asInt().unwrapOr(0);
    auto winner = battle["winnerUid"].asString().unwrapOr("");
    if (!winner.empty() && !battle["draw"].asBool().unwrapOr(false)) {
        if (winner == snapshot["host"]["uid"].asString().unwrapOr("")) ++host;
        else if (winner == snapshot["guest"]["uid"].asString().unwrapOr("")) ++guest;
    }
    score["host"] = host; score["guest"] = guest; score["lastMatch"] = id;
    room["score"] = std::move(score);
}
}
