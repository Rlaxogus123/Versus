#include "VersusService.hpp"
#include "FirebaseConfig.hpp"
#include "RoomMembership.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/utils/web.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <deque>
#include <iomanip>
#include <memory>
#include <limits>
#include <random>
#include <sstream>
#include <unordered_set>
#include <utility>

using namespace geode::prelude;

namespace versus {
namespace {
using Json = matjson::Value;
using Clock = std::chrono::steady_clock;
struct State {
    bool initialized = false;
    bool authenticating = false;
    bool writing = false;
    bool maintenanceWriting = false;
    bool polling = false;
    bool dispatchingAction = false;
    float pollTime = 0;
    float heartbeatTime = 0;
    uint64_t epoch = 0;
    uint64_t revision = 0;
    int64_t serverOffset = 0;
    bool preciseServerClock = false;
    int64_t bestClockRoundTrip = std::numeric_limits<int64_t>::max();
    Clock::time_point clockMeasuredAt;
    std::string token;
    std::string refreshToken;
    std::string apiKey;
    std::string notice;
    PlayerProfile profile;
    std::optional<RoomInfo> room;
    Clock::time_point expires;
    Clock::time_point lastContact = Clock::now();
    Clock::time_point nextHistoryAttempt = Clock::now();
    bool historyWriting = false;
    std::unordered_set<std::string> recordedMatches;
    std::vector<Done> connectCallbacks;
    std::deque<std::function<void()>> pendingActions;
};
State& state() { static State value; return value; }
int64_t systemNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
int64_t nowMs() { return systemNowMs() + state().serverOffset; }
void syncServerClock(web::WebResponse const& response) {
    if (state().preciseServerClock) return;
    auto date = response.header("Date");
    if (!date) return;
    std::tm time{};
    std::istringstream input{std::string(*date)};
    input.imbue(std::locale::classic());
    input >> std::get_time(&time, "%a, %d %b %Y %H:%M:%S GMT");
    if (input.fail()) return;
#ifdef GEODE_IS_WINDOWS
    auto seconds = _mkgmtime(&time);
#else
    auto seconds = timegm(&time);
#endif
    if (seconds > 0) state().serverOffset = static_cast<int64_t>(seconds) * 1000 - systemNowMs();
}
std::string formEncode(std::string const& value) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string encoded;
    for (unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += static_cast<char>(c);
        } else {
            encoded += '%'; encoded += hex[c >> 4]; encoded += hex[c & 15];
        }
    }
    return encoded;
}
Json timestamp() { auto value = Json::object(); value[".sv"] = "timestamp"; return value; }
std::string stringAt(Json const& value, char const* key, std::string fallback = {}) {
    return value[key].asString().unwrapOr(std::move(fallback));
}
int64_t intAt(Json const& value, char const* key, int64_t fallback = 0) {
    return value[key].asInt().unwrapOr(fallback);
}
bool boolAt(Json const& value, char const* key) { return value[key].asBool().unwrapOr(false); }
int pack(ccColor3B color) { return (color.r << 16) | (color.g << 8) | color.b; }
void refreshLocalProfile() {
    auto& profile = state().profile;
    auto* manager = GameManager::sharedState();
    auto* account = GJAccountManager::sharedState();
    profile.name = account && !account->m_username.empty() ? std::string(account->m_username) : "Player";
    profile.accountId = account ? account->m_accountID : 0;
    profile.icon = manager ? std::max(1, manager->getPlayerFrame()) : 1;
    profile.color1 = manager ? pack(manager->colorForIdx(manager->getPlayerColor())) : 0x007BFF;
    profile.color2 = manager ? pack(manager->colorForIdx(manager->getPlayerColor2())) : 0xFFFFFF;
}
std::string configuredApiKey() {
    auto key = Mod::get()->getSettingValue<std::string>("firebase-api-key");
    return key.empty() ? firebase_config::WEB_API_KEY : key;
}
Json profileJson(PlayerProfile const& profile) {
    auto value = Json::object();
    value["uid"] = profile.uid; value["name"] = profile.name;
    value["accountId"] = profile.accountId; value["icon"] = profile.icon;
    value["color1"] = profile.color1; value["color2"] = profile.color2;
    value["winRate"] = profile.winRate; value["recentGames"] = profile.recentGames;
    return value;
}
PlayerProfile parseProfile(Json const& value) {
    PlayerProfile result;
    result.uid = stringAt(value, "uid"); result.name = stringAt(value, "name", "Player");
    result.accountId = static_cast<int>(intAt(value, "accountId"));
    result.icon = static_cast<int>(intAt(value, "icon", 1));
    result.color1 = static_cast<int>(intAt(value, "color1", 0x007BFF));
    result.color2 = static_cast<int>(intAt(value, "color2", 0xFFFFFF));
    result.winRate = value["winRate"].asDouble().unwrapOr(0.);
    result.recentGames = static_cast<int>(intAt(value, "recentGames"));
    return result;
}
GameRules parseRules(Json const& value) {
    GameRules result;
    result.mode = static_cast<int>(intAt(value, "mode"));
    result.attempts = static_cast<int>(intAt(value, "attempts", 3));
    result.targetPercent = static_cast<int>(intAt(value, "targetPercent", 40));
    result.sequence = boolAt(value, "sequence");
    result.practice = boolAt(value, "practice");
    return result;
}
Json rulesJson(GameRules const& rules) {
    auto value = Json::object();
    value["mode"] = rules.mode;
    value["attempts"] = rules.attempts;
    value["targetPercent"] = rules.targetPercent;
    value["sequence"] = rules.sequence;
    value["practice"] = rules.practice;
    return value;
}
BattlePlayerState parseBattlePlayer(Json const& value) {
    BattlePlayerState result;
    result.attemptsUsed = static_cast<int>(intAt(value, "attemptsUsed"));
    result.bestPercent = static_cast<int>(intAt(value, "bestPercent"));
    result.currentPercent = static_cast<int>(intAt(value, "currentPercent"));
    result.runNumber = static_cast<int>(intAt(value, "runNumber"));
    result.inAttempt = boolAt(value, "inAttempt");
    result.cleared = boolAt(value, "cleared");
    result.forfeited = boolAt(value, "forfeited");
    result.spectating = boolAt(value, "spectating");
    result.paused = boolAt(value, "paused");
    result.pausedAt = intAt(value, "pausedAt");
    result.x = value["x"].asDouble().unwrapOr(0.);
    result.y = value["y"].asDouble().unwrapOr(0.);
    result.cameraX = value["cameraX"].asDouble().unwrapOr(0.);
    result.cameraY = value["cameraY"].asDouble().unwrapOr(0.);
    result.updatedAt = intAt(value, "updatedAt");
    return result;
}
Json battlePlayerJson(BattlePlayerState const& player) {
    auto value = Json::object();
    value["attemptsUsed"] = player.attemptsUsed;
    value["bestPercent"] = player.bestPercent;
    value["currentPercent"] = player.currentPercent;
    value["runNumber"] = player.runNumber;
    value["inAttempt"] = player.inAttempt;
    value["cleared"] = player.cleared;
    value["forfeited"] = player.forfeited;
    value["spectating"] = player.spectating;
    value["paused"] = player.paused;
    value["pausedAt"] = player.pausedAt;
    value["x"] = player.x;
    value["y"] = player.y;
    value["cameraX"] = player.cameraX;
    value["cameraY"] = player.cameraY;
    value["updatedAt"] = player.updatedAt;
    return value;
}
Json initialBattlePlayer(bool playing, bool spectator) {
    BattlePlayerState player;
    player.runNumber = playing ? 1 : 0;
    player.inAttempt = playing;
    player.spectating = spectator;
    return battlePlayerJson(player);
}
bool battleTerminal(Json const& player, GameRules const& rules) {
    if (boolAt(player, "forfeited") || boolAt(player, "cleared")) return true;
    return rules.mode == 0 && !rules.practice && intAt(player, "attemptsUsed") >= rules.attempts;
}
void resolveBattle(Json& body, std::string const& actorUid) {
    auto& battle = body["battle"];
    if (!battle.isObject() || intAt(battle, "finishedAt") > 0) return;
    auto const rules = parseRules(std::as_const(body)["rules"]);
    auto const hostUid = stringAt(std::as_const(body)["host"], "uid");
    auto const guestUid = stringAt(std::as_const(body)["guest"], "uid");
    auto const host = battle["host"];
    auto const guest = battle["guest"];
    std::string winner;
    bool draw = false;
    if (boolAt(host, "forfeited")) winner = guestUid;
    else if (boolAt(guest, "forfeited")) winner = hostUid;
    else if (intAt(host, "pausedAt") > 0 && nowMs() - intAt(host, "pausedAt") >= 30000) winner = guestUid;
    else if (intAt(guest, "pausedAt") > 0 && nowMs() - intAt(guest, "pausedAt") >= 30000) winner = hostUid;
    else if (rules.mode == 1) {
        auto const threshold = rules.targetPercent;
        if (stringAt(battle, "firstReachedUid").empty()) {
            auto const& actor = actorUid == hostUid ? host : guest;
            auto const& other = actorUid == hostUid ? guest : host;
            if (intAt(actor, "currentPercent") >= threshold) {
                battle["firstReachedUid"] = actorUid;
                battle["opponentRunAtFirst"] = boolAt(other, "inAttempt") ? intAt(other, "runNumber") : 0;
                if (!boolAt(other, "inAttempt")) winner = actorUid;
            }
        }
        auto const first = stringAt(battle, "firstReachedUid");
        if (!first.empty() && winner.empty()) {
            auto const& other = first == hostUid ? guest : host;
            auto const replyRun = intAt(battle, "opponentRunAtFirst");
            if (replyRun > 0 && intAt(other, "runNumber") == replyRun &&
                intAt(other, "currentPercent") >= threshold) draw = true;
            else if (replyRun == 0 || !boolAt(other, "inAttempt") ||
                intAt(other, "runNumber") > replyRun) winner = first;
        }
    }
    else {
        bool const hostDone = battleTerminal(host, rules);
        bool const guestDone = battleTerminal(guest, rules);
        if (rules.sequence && !rules.practice &&
            stringAt(battle, "activeUid") == stringAt(battle, "firstUid")) {
            auto const& first = stringAt(battle, "firstUid") == hostUid ? host : guest;
            if (battleTerminal(first, rules)) battle["activeUid"] = stringAt(battle, "firstUid") == hostUid ? guestUid : hostUid;
        }
        if ((rules.practice && boolAt(host, "cleared") && boolAt(guest, "cleared")) ||
            (!rules.practice && hostDone && guestDone)) {
            int const hostScore = rules.practice ? -static_cast<int>(intAt(host, "attemptsUsed"))
                : static_cast<int>(intAt(host, "bestPercent"));
            int const guestScore = rules.practice ? -static_cast<int>(intAt(guest, "attemptsUsed"))
                : static_cast<int>(intAt(guest, "bestPercent"));
            if (hostScore == guestScore) draw = true;
            else winner = hostScore > guestScore ? hostUid : guestUid;
        }
    }
    if (draw || !winner.empty()) {
        battle["draw"] = draw;
        battle["winnerUid"] = winner;
        battle["finishedAt"] = timestamp();
    }
}
RoomInfo parseRoom(std::string id, Json const& value) {
    RoomInfo result;
    result.id = std::move(id); result.name = stringAt(value, "name");
    result.privateRoom = boolAt(value, "privateRoom");
    result.host = parseProfile(value["host"]);
    if (hasRoomGuest(value)) result.guest = parseProfile(value["guest"]);
    result.level.id = intAt(value["level"], "id");
    result.level.name = stringAt(value["level"], "name");
    result.level.difficulty = static_cast<int>(intAt(value["level"], "difficulty"));
    result.level.stars = static_cast<int>(intAt(value["level"], "stars"));
    result.level.demon = boolAt(value["level"], "demon");
    result.level.autoLevel = boolAt(value["level"], "autoLevel");
    result.rules = parseRules(value["rules"]);
    result.hostReady = boolAt(value, "hostReady");
    result.guestReady = boolAt(value, "guestReady");
    result.hostEmoteAt = intAt(value, "hostEmoteAt");
    result.guestEmoteAt = intAt(value, "guestEmoteAt");
    if (value["emote"].isObject()) {
        EmoteInfo emote;
        emote.uid = stringAt(value["emote"], "uid");
        emote.kind = stringAt(value["emote"], "kind");
        emote.at = intAt(value["emote"], "at");
        emote.nonce = stringAt(value["emote"], "nonce");
        result.emote = std::move(emote);
    }
    result.started = boolAt(value, "started"); result.updatedAt = intAt(value, "updatedAt");
    if (value["launch"].isObject()) {
        LaunchInfo launch;
        launch.id = stringAt(value["launch"], "id");
        launch.requestedAt = intAt(value["launch"], "requestedAt");
        launch.releasedAt = intAt(value["launch"], "releasedAt");
        launch.hostLoaded = boolAt(value["launch"], "hostLoaded");
        launch.guestLoaded = boolAt(value["launch"], "guestLoaded");
        result.launch = std::move(launch);
    }
    if (value["battle"].isObject()) {
        BattleInfo battle;
        auto const& source = value["battle"];
        battle.id = stringAt(source, "id");
        battle.firstUid = stringAt(source, "firstUid");
        battle.activeUid = stringAt(source, "activeUid");
        battle.firstReachedUid = stringAt(source, "firstReachedUid");
        battle.opponentRunAtFirst = static_cast<int>(intAt(source, "opponentRunAtFirst"));
        battle.winnerUid = stringAt(source, "winnerUid");
        battle.draw = boolAt(source, "draw");
        battle.finishedAt = intAt(source, "finishedAt");
        battle.host = parseBattlePlayer(source["host"]);
        battle.guest = parseBattlePlayer(source["guest"]);
        result.battle = std::move(battle);
    }
    return result;
}
bool validPin(std::string const& pin) {
    return pin.size() == 4 && std::all_of(pin.begin(), pin.end(), [](char c) { return c >= '0' && c <= '9'; });
}
std::string randomId() {
    std::random_device random;
    std::string result;
    constexpr char digits[] = "0123456789abcdef";
    for (int index = 0; index < 32; ++index) result += digits[random() & 15];
    return result;
}
std::string endpoint(std::string const& path) {
    return fmt::format("{}/{}/{}.json", firebase_config::DATABASE_URL, firebase_config::ROOT, path);
}
std::string networkError(web::WebResponse const& response) {
    if (response.code() == 401 || response.code() == 403)
        return "Access denied. Check Firebase Authentication and database rules.";
    if (response.code() == 412) return "Room changed. Please try again.";
    return "Unable to reach Versus. Check your connection and retry.";
}
using Response = std::function<void(web::WebResponse)>;
void request(std::string method, std::string path, Json body, Response callback,
    std::string etag = {}, bool wantEtag = false, bool roomList = false, bool history = false) {
    auto request = web::WebRequest();
    request.timeout(std::chrono::seconds(8));
    request.header("Cache-Control", "no-cache");
    request.param("auth", state().token);
    if (wantEtag) request.header("X-Firebase-ETag", "true");
    if (!etag.empty()) request.header("if-match", std::move(etag));
    if (roomList) {
        request.param("orderBy", "\"updatedAt\"");
        request.param("limitToLast", firebase_config::ROOM_LIMIT);
        request.param("startAt", nowMs() - firebase_config::PRESENCE_TIMEOUT_MS);
    }
    if (history) { request.param("orderBy", "\"playedAt\""); request.param("limitToLast", 10); }
    if (method != "GET") {
        request.header("Content-Type", "application/json");
        request.bodyString(body.dump());
    }
    bool const clockSample = method == "PUT" && path.starts_with("rooms/") && body.isObject();
    auto const sentAt = Clock::now();
    async::spawn(request.send(std::move(method), endpoint(path)),
        [callback = std::move(callback), clockSample, sentAt](web::WebResponse response) mutable {
            syncServerClock(response);
            // Our successful room writes resolve updatedAt on the server. Use
            // millisecond timestamps and half the RTT for the shared countdown;
            // the HTTP Date header only has one-second precision.
            if (clockSample && response.ok()) {
                auto parsed = response.json();
                auto const serverStamp = parsed ? intAt(parsed.unwrap(), "updatedAt") : 0;
                if (serverStamp > 0) {
                    auto const roundTrip = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - sentAt).count();
                    auto& s = state();
                    if (roundTrip <= s.bestClockRoundTrip || Clock::now() - s.clockMeasuredAt > std::chrono::minutes(5)) {
                        s.serverOffset = serverStamp + roundTrip / 2 - systemNowMs();
                        s.preciseServerClock = true;
                        s.bestClockRoundTrip = roundTrip;
                        s.clockMeasuredAt = Clock::now();
                    }
                }
            }
            callback(std::move(response));
        });
}
void disconnected(std::string notice) {
    auto& s = state();
    ++s.epoch; s.room.reset(); s.polling = false; s.writing = false; s.maintenanceWriting = false;
    s.notice = std::move(notice);
}
void adoptRoom(std::string const& id, Json const& value) {
    auto& s = state();
    s.room = parseRoom(id, value);
    s.lastContact = Clock::now();
}

void deferRoomAction(std::function<void(Done)> action, Done callback) {
    auto const epoch = state().epoch;
    state().pendingActions.push_back([epoch, action = std::move(action), callback = std::move(callback)]() mutable {
        if (epoch != state().epoch) { callback(false, "Room session changed."); return; }
        action(std::move(callback));
    });
}

// A timed-out write may already have reserved our seat. Read it back before
// reporting failure so a lost response cannot strand the player in the lobby.
void recoverMembership(std::string id, uint64_t epoch, bool host, std::string error, Done callback) {
    request("GET", "rooms/" + id, {},
        [id, epoch, host, error = std::move(error), callback = std::move(callback)](web::WebResponse response) mutable {
            auto& s = state();
            if (epoch != s.epoch) { callback(false, "Room session changed."); return; }
            auto parsed = response.json();
            if (response.ok() && parsed && parsed.unwrap().isObject()) {
                auto const& body = parsed.unwrap();
                if (stringAt(std::as_const(body)[host ? "host" : "guest"], "uid") == s.profile.uid &&
                    nowMs() - intAt(body, "hostSeen") <= firebase_config::PRESENCE_TIMEOUT_MS) {
                    adoptRoom(id, body);
                    callback(true, {});
                    return;
                }
            }
            callback(false, std::move(error));
        });
}

// Each seat mutation is a compare-and-swap of one room. Two simultaneous joins
// cannot both succeed, and a heartbeat cannot resurrect a deleted room.
using Mutator = std::function<std::string(Json&)>;
void mutateRoom(std::string id, uint64_t epoch, Mutator change, Done callback, int attempts = 3) {
    ++state().revision;
    request("GET", "rooms/" + id, {},
        [id, epoch, change = std::move(change), callback = std::move(callback), attempts](web::WebResponse response) mutable {
            auto& s = state();
            if (epoch != s.epoch) { callback(false, "Room session changed."); return; }
            auto parsed = response.json(); auto etag = response.header("ETag");
            if (!response.ok() || !parsed || !etag) { callback(false, networkError(response)); return; }
            auto body = parsed.unwrap();
            if (!body.isObject()) { callback(false, "The host left the room."); return; }
            auto error = change(body);
            if (!error.empty()) { callback(false, std::move(error)); return; }
            if (!body.isNull()) body["updatedAt"] = timestamp();
            request("PUT", "rooms/" + id, body,
                [id, epoch, change = std::move(change), callback = std::move(callback), attempts](web::WebResponse result) mutable {
                    if (epoch != state().epoch) { callback(false, "Room session changed."); return; }
                    if (result.code() == 412 && attempts > 1) {
                        mutateRoom(id, epoch, std::move(change), std::move(callback), attempts - 1); return;
                    }
                    if (!result.ok()) { callback(false, networkError(result)); return; }
                    auto parsed = result.json();
                    if (!parsed) { callback(false, "Invalid room response. Please retry."); return; }
                    if (parsed.unwrap().isObject()) adoptRoom(id, parsed.unwrap());
                    callback(true, {});
                }, std::string(*etag));
        }, {}, true);
}
void pollRoom() {
    auto& s = state();
    if (!s.room || s.polling || s.writing) return;
    s.polling = true; s.pollTime = 0;
    auto const id = s.room->id; auto const epoch = s.epoch;
    auto const revision = s.revision;
    request("GET", "rooms/" + id, {}, [id, epoch, revision](web::WebResponse response) {
        auto& s = state();
        if (epoch != s.epoch) return;
        s.polling = false;
        if (s.writing || revision != s.revision) return;
        auto parsed = response.json();
        // A network/permission error is NOT evidence that the host deleted a room.
        if (!response.ok() || !parsed) return;
        auto const body = parsed.unwrap();
        if (body.isNull()) { disconnected("The host left the room."); return; }
        if (!body.isObject()) return;
        if (stringAt(std::as_const(body)["host"], "uid") != s.profile.uid) {
            if (nowMs() - intAt(body, "hostSeen") > firebase_config::PRESENCE_TIMEOUT_MS) {
                disconnected("The host disconnected."); return;
            }
            if (stringAt(std::as_const(body)["guest"], "uid") != s.profile.uid) {
                disconnected("Your room connection expired."); return;
            }
        }
        adoptRoom(id, body);
    });
}
void heartbeat() {
    auto& s = state();
    if (!s.room || s.writing || s.polling) return;
    s.writing = true; s.maintenanceWriting = true; s.heartbeatTime = 0;
    auto const id = s.room->id; auto const epoch = s.epoch;
    mutateRoom(id, epoch, [](Json& body) -> std::string {
        auto const uid = state().profile.uid;
        if (stringAt(std::as_const(body)["host"], "uid") == uid) {
            body["hostSeen"] = timestamp();
            if (roomGuestExpired(body, nowMs(), firebase_config::PRESENCE_TIMEOUT_MS)) {
                body["guest"] = nullptr; body["guestSeen"] = nullptr; body["started"] = false;
                body["hostReady"] = false; body["guestReady"] = false;
                body["launch"] = nullptr; body["battle"] = nullptr;
            }
        } else if (stringAt(std::as_const(body)["guest"], "uid") == uid) {
            if (nowMs() - intAt(body, "hostSeen") > firebase_config::PRESENCE_TIMEOUT_MS)
                return "The host disconnected.";
            body["guestSeen"] = timestamp();
        }
        else return "Your room connection expired.";
        return {};
    }, [epoch](bool success, std::string error) {
        if (epoch != state().epoch) return;
        state().writing = false;
        state().maintenanceWriting = false;
        if (!success && (error == "The host left the room." || error == "The host disconnected." ||
            error == "Your room connection expired.")) disconnected(std::move(error));
    });
}
class ServiceTicker final : public CCNode {
public:
    void update(float dt) override { Service::get().tick(dt); }
};
void finishAuthentication(bool success, std::string error) {
    auto& s = state(); s.authenticating = false;
    auto callbacks = std::move(s.connectCallbacks); s.connectCallbacks.clear();
    for (auto& callback : callbacks) if (callback) callback(success, error);
}
void authenticate(std::string key, bool refresh) {
    auto request = web::WebRequest();
    request.timeout(std::chrono::seconds(10)); request.param("key", std::move(key));
    auto body = Json::object();
    std::string url;
    if (refresh) {
        url = "https://securetoken.googleapis.com/v1/token";
        request.header("Content-Type", "application/x-www-form-urlencoded");
        request.bodyString("grant_type=refresh_token&refresh_token=" + formEncode(state().refreshToken));
    } else {
        url = "https://identitytoolkit.googleapis.com/v1/accounts:signUp";
        body["returnSecureToken"] = true;
        request.header("Content-Type", "application/json"); request.bodyString(body.dump());
    }
    async::spawn(request.post(url), [refresh](web::WebResponse response) {
        syncServerClock(response);
        auto parsed = response.json();
        if (!response.ok() || !parsed) {
            finishAuthentication(false, "Firebase sign-in failed. Check the Web API key and enable Anonymous authentication."); return;
        }
        auto const body = parsed.unwrap(); auto& s = state();
        auto const uid = stringAt(body, refresh ? "user_id" : "localId");
        auto const token = stringAt(body, refresh ? "id_token" : "idToken");
        auto const refreshToken = stringAt(body, refresh ? "refresh_token" : "refreshToken");
        if (uid.empty() || token.empty() || refreshToken.empty()) {
            finishAuthentication(false, "Invalid Firebase sign-in response."); return;
        }
        s.profile.uid = uid; s.token = token; s.refreshToken = refreshToken;
        s.expires = Clock::now() + std::chrono::minutes(50);
        Mod::get()->setSavedValue("firebase-refresh-token", refreshToken);
        Mod::get()->setSavedValue("firebase-uid", uid);
        (void)Mod::get()->saveData();
        finishAuthentication(true, {});
    });
}
}

Service& Service::get() { static Service instance; return instance; }
void Service::initialize() {
    auto& s = state(); if (s.initialized) return; s.initialized = true;
    s.profile.uid = Mod::get()->getSavedValue<std::string>("firebase-uid", "");
    s.refreshToken = Mod::get()->getSavedValue<std::string>("firebase-refresh-token", "");
    s.apiKey = configuredApiKey();
    refreshLocalProfile();
    auto ticker = new ServiceTicker();
    if (ticker->init()) {
        CCDirector::sharedDirector()->getScheduler()->scheduleUpdateForTarget(ticker, 0, false);
        ticker->release(); // The scheduler retains its target for the service lifetime.
    }
    else delete ticker;
}
void Service::connect(Done callback) {
    initialize(); auto& s = state();
    if (!s.room) refreshLocalProfile();
    auto key = configuredApiKey();
    if (key != s.apiKey && !s.authenticating && !s.room) {
        s.apiKey = key;
        s.token.clear();
    }
    if (connected()) { callback(true, {}); return; }
    if (key.empty()) { callback(false, "Firebase setup required: enter this project's Web API key in the Versus settings."); return; }
    s.connectCallbacks.push_back(std::move(callback));
    if (s.authenticating) return;
    s.authenticating = true; authenticate(std::move(key), !s.refreshToken.empty());
}
bool Service::connected() const { return !state().token.empty() && Clock::now() < state().expires; }
PlayerProfile const& Service::profile() const { return state().profile; }
std::optional<RoomInfo> const& Service::room() const { return state().room; }
bool Service::isHost() const { return state().room && state().room->host.uid == state().profile.uid; }
bool Service::busy() const {
    return (state().writing && !state().maintenanceWriting) ||
        (!state().pendingActions.empty() && !state().dispatchingAction) || state().authenticating;
}
int64_t Service::serverNow() const { return nowMs(); }
void Service::fetchRooms(RoomListCallback callback) {
    connect([callback = std::move(callback)](bool ok, std::string error) mutable {
        if (!ok) { callback({}, std::move(error)); return; }
        request("GET", "rooms", {}, [callback = std::move(callback)](web::WebResponse response) mutable {
            auto parsed = response.json();
            if (!response.ok() || !parsed) { callback({}, networkError(response)); return; }
            std::vector<RoomInfo> rooms;
            if (parsed.unwrap().isObject()) for (auto const& entry : parsed.unwrap()) {
                if (!entry.isObject() || nowMs() - intAt(entry, "hostSeen") > firebase_config::PRESENCE_TIMEOUT_MS) continue;
                auto room = parseRoom(entry.getKey().value_or(""), entry);
                if (room.id.empty() || room.host.uid.empty()) continue;
                rooms.push_back(std::move(room));
            }
            std::sort(rooms.begin(), rooms.end(), [](auto const& a, auto const& b) { return a.updatedAt > b.updatedAt; });
            callback(std::move(rooms), {});
        }, {}, false, true);
    });
}
void Service::fetchHistory(HistoryCallback callback) {
    connect([callback = std::move(callback)](bool ok, std::string error) mutable {
        if (!ok) { callback({}, std::move(error)); return; }
        request("GET", "history/" + state().profile.uid, {}, [callback = std::move(callback)](web::WebResponse response) mutable {
            auto parsed = response.json();
            if (!response.ok() || !parsed) { callback({}, networkError(response)); return; }
            std::vector<MatchRecord> records;
            if (parsed.unwrap().isObject()) for (auto const& entry : parsed.unwrap()) {
                MatchRecord record;
                record.id = entry.getKey().value_or(""); record.opponentName = stringAt(entry, "opponentName");
                record.levelName = stringAt(entry, "levelName"); record.winnerName = stringAt(entry, "winnerName");
                record.result = stringAt(entry, "result"); record.playedAt = intAt(entry, "playedAt");
                records.push_back(std::move(record));
            }
            std::sort(records.begin(), records.end(), [](auto const& a, auto const& b) { return a.playedAt > b.playedAt; });
            if (records.size() > 10) records.resize(10);
            auto& profile = state().profile; profile.recentGames = static_cast<int>(records.size());
            auto wins = std::count_if(records.begin(), records.end(), [](auto const& r) { return r.result == "win"; });
            profile.winRate = records.empty() ? 0. : 100. * wins / records.size();
            callback(std::move(records), {});
        }, {}, false, false, true);
    });
}
void Service::createRoom(std::string name, std::string pin, Done callback) {
    if (busy() || state().room) { callback(false, "Leave your current room first."); return; }
    auto first = name.find_first_not_of(" \t\r\n");
    auto last = name.find_last_not_of(" \t\r\n");
    name = first == std::string::npos ? "" : name.substr(first, last - first + 1);
    if (name.empty() || name.size() > 48) { callback(false, "Use a room name with 1 to 48 characters."); return; }
    if (!pin.empty() && !validPin(pin)) { callback(false, "Use a four-digit password."); return; }
    connect([name = std::move(name), pin = std::move(pin), callback = std::move(callback)](bool ok, std::string error) mutable {
        if (!ok) { callback(false, std::move(error)); return; }
        auto& s = state(); if (s.writing || s.room) { callback(false, "Already joining a room."); return; }
        s.writing = true; auto const id = randomId(); auto const epoch = ++s.epoch;
        auto secret = Json::object(); secret["hostUid"] = s.profile.uid; secret["pin"] = pin;
        request("PUT", "roomSecrets/" + id, secret,
            [id, epoch, name = std::move(name), privateRoom = !pin.empty(), callback = std::move(callback)](web::WebResponse response) mutable {
                if (!response.ok()) { state().writing = false; callback(false, networkError(response)); return; }
                auto body = Json::object(); body["name"] = name; body["privateRoom"] = privateRoom;
                body["host"] = profileJson(state().profile); body["hostSeen"] = timestamp();
                body["updatedAt"] = timestamp(); body["started"] = false;
                body["rules"] = rulesJson({});
                body["hostReady"] = false; body["guestReady"] = false;
                body["hostEmoteAt"] = 0; body["guestEmoteAt"] = 0;
                request("PUT", "rooms/" + id, body, [id, epoch, callback = std::move(callback)](web::WebResponse result) mutable {
                    auto& s = state(); if (epoch != s.epoch) { callback(false, "Room session changed."); return; }
                    auto finish = [id, epoch, callback = std::move(callback)](bool success, std::string error) mutable {
                        if (epoch != state().epoch) { callback(false, "Room session changed."); return; }
                        state().writing = false; state().pollTime = 0; state().heartbeatTime = 0;
                        if (!success) request("DELETE", "roomSecrets/" + id, {}, [](auto) {});
                        callback(success, std::move(error));
                    };
                    auto parsed = result.json();
                    if (!result.ok() || !parsed || !parsed.unwrap().isObject()) {
                        recoverMembership(id, epoch, true, networkError(result), std::move(finish)); return;
                    }
                    adoptRoom(id, parsed.unwrap());
                    finish(true, {});
                }, "null_etag");
            }, "null_etag");
    });
}
void Service::joinRoom(RoomInfo room, std::string pin, Done callback) {
    if (busy() || state().room) { callback(false, "Leave your current room first."); return; }
    if (room.privateRoom && !validPin(pin)) { callback(false, "Enter the four-digit password."); return; }
    connect([room = std::move(room), pin = std::move(pin), callback = std::move(callback)](bool ok, std::string error) mutable {
        if (!ok) { callback(false, std::move(error)); return; }
        auto& s = state(); if (s.writing || s.room) { callback(false, "Already joining a room."); return; }
        s.writing = true; auto const epoch = ++s.epoch;
        auto join = [id = room.id, epoch, callback = std::move(callback)](bool admitted, std::string error) mutable {
            if (!admitted) { state().writing = false; callback(false, std::move(error)); return; }
            mutateRoom(id, epoch, [](Json& body) -> std::string {
                auto const uid = state().profile.uid;
                if (auto error = roomJoinError(body, uid, nowMs(), firebase_config::PRESENCE_TIMEOUT_MS); !error.empty()) return error;
                if (stringAt(std::as_const(body)["guest"], "uid") == uid) { body["guestSeen"] = timestamp(); return {}; }
                body["guest"] = profileJson(state().profile); body["guestSeen"] = timestamp();
                body["guestReady"] = false; body["guestEmoteAt"] = 0; return {};
            }, [id, epoch, callback = std::move(callback)](bool success, std::string error) mutable {
                auto finish = [epoch, callback = std::move(callback)](bool recovered, std::string error) mutable {
                    if (epoch != state().epoch) { callback(false, "Room session changed."); return; }
                    state().writing = false; state().pollTime = 0; state().heartbeatTime = 0;
                    callback(recovered, std::move(error));
                };
                if (!success && epoch == state().epoch) {
                    recoverMembership(id, epoch, false, std::move(error), std::move(finish)); return;
                }
                finish(success, std::move(error));
            });
        };
        if (!room.privateRoom) { join(true, {}); return; }
        auto proof = Json::object(); proof["pin"] = pin;
        request("PUT", "admissions/" + room.id + "/" + s.profile.uid, proof,
            [join = std::move(join)](web::WebResponse response) mutable {
                join(response.ok(), response.code() == 401 || response.code() == 403 ? "Incorrect password or room closed." : networkError(response));
            });
    });
}
void Service::leaveRoom(Done callback) {
    auto& s = state();
    if (s.maintenanceWriting) {
        deferRoomAction([](Done done) { Service::get().leaveRoom(std::move(done)); }, std::move(callback));
        return;
    }
    if (!s.room) { callback(true, {}); return; }
    if (s.writing) { callback(false, "Please wait for the room update."); return; }
    auto const id = s.room->id; auto const host = isHost(); auto const epoch = s.epoch;
    s.writing = true;
    mutateRoom(id, epoch, [host](Json& body) -> std::string {
        auto const uid = state().profile.uid;
        if (host && stringAt(std::as_const(body)["host"], "uid") == uid) body = nullptr;
        else if (!host && stringAt(std::as_const(body)["guest"], "uid") == uid) {
            body["guest"] = nullptr; body["guestSeen"] = nullptr; body["started"] = false;
            body["hostReady"] = false; body["guestReady"] = false;
            body["launch"] = nullptr; body["battle"] = nullptr;
        }
        else return "Your room connection expired.";
        return {};
    }, [id, epoch, host, callback = std::move(callback)](bool success, std::string error) mutable {
        if (epoch != state().epoch) { callback(false, "Room session changed."); return; }
        state().writing = false;
        if (!success && error != "The host left the room." && error != "Your room connection expired.") { callback(false, std::move(error)); return; }
        disconnected({});
        if (host) {
            request("DELETE", "admissions/" + id, {}, [id](auto) {
                request("DELETE", "roomSecrets/" + id, {}, [](auto) {});
            });
        }
        callback(true, {});
    });
}
void Service::selectLevel(LevelInfo level, Done callback) {
    if (state().maintenanceWriting) {
        deferRoomAction([level = std::move(level)](Done done) mutable {
            Service::get().selectLevel(std::move(level), std::move(done));
        }, std::move(callback));
        return;
    }
    if (!isHost() || busy() || level.id <= 0) { callback(false, "Only the host can select a level."); return; }
    auto const epoch = state().epoch; state().writing = true;
    mutateRoom(state().room->id, epoch, [level = std::move(level)](Json& body) -> std::string {
        if (stringAt(std::as_const(body)["host"], "uid") != state().profile.uid) return "Only the host can select a level.";
        if (boolAt(body, "started")) return "The match has already started.";
        auto value = Json::object(); value["id"] = level.id; value["name"] = level.name;
        value["difficulty"] = level.autoLevel ? -1 : level.difficulty; value["stars"] = level.stars;
        value["demon"] = level.demon; value["autoLevel"] = level.autoLevel;
        if (std::as_const(body)["level"] != value) { body["hostReady"] = false; body["guestReady"] = false; }
        body["level"] = value; return {};
    }, [epoch, callback = std::move(callback)](bool ok, std::string error) mutable {
        if (epoch == state().epoch) state().writing = false;
        callback(ok, std::move(error));
    });
}
void Service::configureRules(GameRules rules, Done callback) {
    if (state().writing || state().authenticating) {
        deferRoomAction([rules](Done done) { Service::get().configureRules(rules, std::move(done)); }, std::move(callback));
        return;
    }
    if (!isHost() || busy()) { callback(false, "Only the host can change game rules."); return; }
    if ((rules.mode != 0 && rules.mode != 1) || rules.attempts < 1 || rules.attempts > 99 ||
        rules.targetPercent < 1 || rules.targetPercent > 100 ||
        (rules.practice && rules.sequence) || (rules.mode == 1 && rules.sequence)) {
        callback(false, "Invalid game rules."); return;
    }
    auto const epoch = state().epoch;
    state().writing = true;
    mutateRoom(state().room->id, epoch, [rules](Json& body) -> std::string {
        if (stringAt(std::as_const(body)["host"], "uid") != state().profile.uid) return "Only the host can change game rules.";
        if (boolAt(body, "started")) return "The match has already started.";
        auto const newRules = rulesJson(rules);
        if (std::as_const(body)["rules"] != newRules) {
            body["rules"] = newRules;
            body["hostReady"] = false;
            body["guestReady"] = false;
        }
        body["hostSeen"] = timestamp();
        return {};
    }, [epoch, callback = std::move(callback)](bool ok, std::string error) mutable {
        if (epoch == state().epoch) state().writing = false;
        callback(ok, std::move(error));
    });
}
void Service::setReady(bool ready, Done callback) {
    if (state().writing || state().authenticating) {
        auto const expectedLevel = state().room ? state().room->level : LevelInfo{};
        auto const expectedRules = state().room ? state().room->rules : GameRules{};
        deferRoomAction([ready, expectedLevel, expectedRules](Done done) {
            auto const& current = state().room;
            if (ready && (!current || current->level != expectedLevel || current->rules != expectedRules)) {
                done(false, "The map or rules changed. Check them and ready again."); return;
            }
            Service::get().setReady(ready, std::move(done));
        }, std::move(callback));
        return;
    }
    if (!state().room || busy()) { callback(false, "Room is unavailable."); return; }
    auto const epoch = state().epoch;
    auto const expectedLevel = state().room->level;
    auto const expectedRules = state().room->rules;
    state().writing = true;
    mutateRoom(state().room->id, epoch, [ready, expectedLevel, expectedRules](Json& body) -> std::string {
        auto const uid = state().profile.uid;
        bool const host = stringAt(std::as_const(body)["host"], "uid") == uid;
        if (!host && stringAt(std::as_const(body)["guest"], "uid") != uid) return "Your room connection expired.";
        if (boolAt(body, "started")) return "The match has already started.";
        if (ready && (intAt(std::as_const(body)["level"], "id") <= 0 ||
            intAt(std::as_const(body)["level"], "id") != expectedLevel.id ||
            stringAt(std::as_const(body)["level"], "name") != expectedLevel.name ||
            intAt(std::as_const(body)["level"], "stars") != expectedLevel.stars ||
            intAt(std::as_const(body)["level"], "difficulty") != expectedLevel.difficulty ||
            boolAt(std::as_const(body)["level"], "demon") != expectedLevel.demon ||
            boolAt(std::as_const(body)["level"], "autoLevel") != expectedLevel.autoLevel ||
            parseRules(std::as_const(body)["rules"]) != expectedRules))
            return "The map or rules changed. Check them and ready again.";
        body[host ? "hostReady" : "guestReady"] = ready;
        body[host ? "hostSeen" : "guestSeen"] = timestamp();
        return {};
    }, [epoch, callback = std::move(callback)](bool ok, std::string error) mutable {
        if (epoch == state().epoch) state().writing = false;
        callback(ok, std::move(error));
    });
}
void Service::sendEmote(std::string kind, Done callback) {
    if (kind != "like" && kind != "smile" && kind != "angry" && kind != "fire") {
        callback(false, "Unknown emote."); return;
    }
    if (state().writing || state().authenticating) {
        deferRoomAction([kind = std::move(kind)](Done done) mutable {
            Service::get().sendEmote(std::move(kind), std::move(done));
        }, std::move(callback));
        return;
    }
    if (!state().room || !state().room->guest || busy()) {
        callback(false, "Wait for the other player."); return;
    }
    auto const epoch = state().epoch;
    state().writing = true;
    mutateRoom(state().room->id, epoch, [kind = std::move(kind), nonce = randomId()](Json& body) -> std::string {
        auto const uid = state().profile.uid;
        bool const host = stringAt(std::as_const(body)["host"], "uid") == uid;
        if (!std::as_const(body)["guest"].isObject() || (!host && stringAt(std::as_const(body)["guest"], "uid") != uid))
            return "Wait for the other player.";
        auto const timeKey = host ? "hostEmoteAt" : "guestEmoteAt";
        if (nowMs() - intAt(body, timeKey) < 1000) return "Wait one second between emotes.";
        auto emote = Json::object();
        emote["uid"] = uid; emote["kind"] = kind;
        emote["at"] = timestamp(); emote["nonce"] = nonce;
        body["emote"] = std::move(emote);
        body[timeKey] = timestamp();
        return {};
    }, [epoch, callback = std::move(callback)](bool ok, std::string error) mutable {
        if (epoch == state().epoch) state().writing = false;
        callback(ok, std::move(error));
    });
}
void Service::startMatch(Done callback) {
    if (state().maintenanceWriting) {
        deferRoomAction([](Done done) { Service::get().startMatch(std::move(done)); }, std::move(callback));
        return;
    }
    if (!isHost() || busy()) { callback(false, "Only the host can start the match."); return; }
    auto const epoch = state().epoch; state().writing = true;
    mutateRoom(state().room->id, epoch, [launchId = randomId(), firstHost = (std::random_device{}() & 1) == 0](Json& body) -> std::string {
        if (stringAt(std::as_const(body)["host"], "uid") != state().profile.uid) return "Only the host can start the match.";
        if (boolAt(body, "started")) return "The match has already started.";
        if (!std::as_const(body)["guest"].isObject() || nowMs() - intAt(body, "guestSeen") > firebase_config::PRESENCE_TIMEOUT_MS) return "Wait for another player.";
        if (!boolAt(body, "hostReady") || !boolAt(body, "guestReady")) return "Both players must be ready.";
        if (intAt(std::as_const(body)["level"], "id") <= 0) return "Choose a level first.";
        auto const rules = parseRules(std::as_const(body)["rules"]);
        if ((rules.mode != 0 && rules.mode != 1) || rules.attempts < 1 || rules.attempts > 99 ||
            rules.targetPercent < 1 || rules.targetPercent > 100 ||
            (rules.practice && rules.sequence) || (rules.mode == 1 && rules.sequence)) return "Invalid game rules.";
        auto launch = Json::object();
        launch["id"] = launchId; launch["requestedAt"] = timestamp();
        launch["hostLoaded"] = false; launch["guestLoaded"] = false; launch["releasedAt"] = 0;
        auto battle = Json::object();
        auto const firstUid = stringAt(std::as_const(body)[firstHost ? "host" : "guest"], "uid");
        bool const sequential = rules.mode == 0 && rules.sequence && !rules.practice;
        battle["id"] = launchId;
        battle["firstUid"] = firstUid;
        battle["activeUid"] = firstUid;
        battle["firstReachedUid"] = "";
        battle["opponentRunAtFirst"] = 0;
        battle["winnerUid"] = "";
        battle["draw"] = false;
        battle["finishedAt"] = 0;
        battle["host"] = initialBattlePlayer(!sequential || firstHost, sequential && !firstHost);
        battle["guest"] = initialBattlePlayer(!sequential || !firstHost, sequential && firstHost);
        body["battle"] = std::move(battle);
        body["launch"] = std::move(launch); body["started"] = true; body["hostSeen"] = timestamp();
        return {};
    }, [epoch, callback = std::move(callback)](bool ok, std::string error) mutable {
        if (epoch == state().epoch) { state().writing = false; state().pollTime = 2.f; }
        callback(ok, std::move(error));
    });
}
void Service::markLoaded(std::string launchId, Done callback) {
    if (state().writing || state().authenticating) {
        deferRoomAction([launchId = std::move(launchId)](Done done) mutable {
            Service::get().markLoaded(std::move(launchId), std::move(done));
        }, std::move(callback));
        return;
    }
    if (!state().room || busy()) { callback(false, "Your room connection expired."); return; }
    auto const epoch = state().epoch; state().writing = true;
    mutateRoom(state().room->id, epoch, [launchId = std::move(launchId)](Json& body) -> std::string {
        auto& launch = body["launch"];
        if (!boolAt(body, "started") || stringAt(launch, "id") != launchId ||
            stringAt(std::as_const(body)["battle"], "id") != launchId) return "The match was cancelled.";
        auto const uid = state().profile.uid;
        bool const host = stringAt(std::as_const(body)["host"], "uid") == uid;
        if (!host && stringAt(std::as_const(body)["guest"], "uid") != uid) return "Your room connection expired.";
        auto const key = host ? "hostLoaded" : "guestLoaded";
        if (intAt(launch, "releasedAt") > 0) return boolAt(launch, key) ? "" : "The match has already started.";
        if (nowMs() >= intAt(launch, "requestedAt") + 60000) return "Loading timed out after 60 seconds.";
        launch[key] = true;
        if (boolAt(launch, "hostLoaded") && boolAt(launch, "guestLoaded")) launch["releasedAt"] = timestamp();
        body[host ? "hostSeen" : "guestSeen"] = timestamp();
        return {};
    }, [epoch, callback = std::move(callback)](bool ok, std::string error) mutable {
        if (epoch == state().epoch) { state().writing = false; state().pollTime = 2.f; }
        callback(ok, std::move(error));
    });
}
void Service::cancelLaunch(std::string launchId, Done callback) {
    if (state().writing || state().authenticating) {
        deferRoomAction([launchId = std::move(launchId)](Done done) mutable {
            Service::get().cancelLaunch(std::move(launchId), std::move(done));
        }, std::move(callback));
        return;
    }
    if (!state().room || !state().room->launch) { callback(true, {}); return; }
    if (state().room->launch->id != launchId) { callback(false, "Match session changed."); return; }
    auto const epoch = state().epoch; state().writing = true;
    mutateRoom(state().room->id, epoch, [launchId = std::move(launchId)](Json& body) -> std::string {
        auto const uid = state().profile.uid;
        if (stringAt(std::as_const(body)["host"], "uid") != uid && stringAt(std::as_const(body)["guest"], "uid") != uid)
            return "Your room connection expired.";
        if (std::as_const(body)["launch"].isObject() && stringAt(std::as_const(body)["launch"], "id") != launchId) return "Match session changed.";
        body["started"] = false;
        body["hostReady"] = false; body["guestReady"] = false;
        body["launch"] = nullptr; body["battle"] = nullptr;
        return {};
    }, [epoch, callback = std::move(callback)](bool ok, std::string error) mutable {
        if (epoch == state().epoch) { state().writing = false; state().pollTime = 2.f; }
        callback(ok, std::move(error));
    });
}
void Service::reportBattle(BattlePlayerState progress, bool sendPosition, Done callback) {
    if (!state().room || !state().room->battle || !state().room->launch) {
        callback(false, "The battle is no longer active."); return;
    }
    if (state().writing || state().authenticating || busy()) {
        callback(false, "Room update in progress."); return;
    }
    auto const epoch = state().epoch;
    auto const matchId = state().room->battle->id;
    state().writing = true;
    mutateRoom(state().room->id, epoch,
        [progress, sendPosition, matchId](Json& body) -> std::string {
            auto const uid = state().profile.uid;
            bool const host = stringAt(std::as_const(body)["host"], "uid") == uid;
            if (!host && stringAt(std::as_const(body)["guest"], "uid") != uid) return "Your room connection expired.";
            if (!boolAt(body, "started") || stringAt(std::as_const(body)["launch"], "id") != matchId ||
                stringAt(std::as_const(body)["battle"], "id") != matchId || intAt(std::as_const(body)["launch"], "releasedAt") <= 0)
                return "The battle is no longer active.";
            auto& battle = body["battle"];
            if (intAt(battle, "finishedAt") > 0) return "The battle has finished.";
            bool const otherSpectating = boolAt(battle[host ? "guest" : "host"], "spectating");
            auto& own = battle[host ? "host" : "guest"];
            auto const rules = parseRules(std::as_const(body)["rules"]);
            if (progress.attemptsUsed < intAt(own, "attemptsUsed") ||
                progress.runNumber < intAt(own, "runNumber") ||
                progress.attemptsUsed < 0 || progress.runNumber < 0 ||
                progress.attemptsUsed > (rules.mode == 0 && !rules.practice ? rules.attempts : 1000000) ||
                progress.runNumber > 1000000 || progress.currentPercent < 0 || progress.currentPercent > 100 ||
                progress.bestPercent < 0 || progress.bestPercent > 100 ||
                (progress.cleared && progress.bestPercent < 100))
                return "Invalid battle progress.";
            if (rules.mode == 0 && rules.sequence && !rules.practice &&
                !progress.spectating && stringAt(battle, "activeUid") != uid)
                return "Wait for your turn.";
            own["attemptsUsed"] = progress.attemptsUsed;
            own["runNumber"] = progress.runNumber;
            own["currentPercent"] = progress.currentPercent;
            own["bestPercent"] = std::max<int64_t>(progress.bestPercent, intAt(own, "bestPercent"));
            own["inAttempt"] = progress.inAttempt;
            own["cleared"] = boolAt(own, "cleared") || progress.cleared;
            own["forfeited"] = boolAt(own, "forfeited") || progress.forfeited;
            own["spectating"] = progress.spectating;
            auto const oldPausedAt = intAt(own, "pausedAt");
            own["paused"] = progress.paused;
            own["pausedAt"] = progress.paused ? (oldPausedAt > 0 ? Json(oldPausedAt) : timestamp()) : Json(0);
            if (sendPosition && !progress.spectating && otherSpectating) {
                auto const finiteBounded = [](double value) {
                    return std::isfinite(value) ? std::clamp(value, -100000000., 100000000.) : 0.;
                };
                own["x"] = finiteBounded(progress.x);
                own["y"] = finiteBounded(progress.y);
                own["cameraX"] = finiteBounded(progress.cameraX);
                own["cameraY"] = finiteBounded(progress.cameraY);
            }
            own["updatedAt"] = timestamp();
            body[host ? "hostSeen" : "guestSeen"] = timestamp();
            resolveBattle(body, uid);
            return {};
        }, [epoch, callback = std::move(callback)](bool ok, std::string error) mutable {
            if (epoch == state().epoch) {
                state().writing = false;
                state().pollTime = 2.f;
            }
            callback(ok, std::move(error));
        });
}
void Service::tick(float dt) {
    auto& s = state();
    if (!s.writing && !s.authenticating && !s.pendingActions.empty()) {
        auto next = std::move(s.pendingActions.front());
        s.pendingActions.pop_front();
        s.dispatchingAction = true;
        next();
        s.dispatchingAction = false;
    }
    if (!s.room) return;
    dt = std::clamp(dt, 0.f, 1.f); s.pollTime += dt; s.heartbeatTime += dt;
    if (Clock::now() - s.lastContact > std::chrono::seconds(60)) {
        disconnected("Connection lost. Please rejoin the room."); return;
    }
    if (!connected()) {
        if (!s.authenticating && s.pollTime >= 2.f) { s.pollTime = 0; connect([](bool, std::string) {}); }
        return;
    }
    if (s.room->battle && s.room->battle->finishedAt > 0 &&
        !s.historyWriting && !s.recordedMatches.contains(s.room->battle->id) &&
        Clock::now() >= s.nextHistoryAttempt) {
        auto const match = *s.room->battle;
        auto const room = *s.room;
        auto const host = isHost();
        auto history = Json::object();
        history["roomId"] = room.id;
        history["opponentName"] = host && room.guest ? room.guest->name : room.host.name;
        history["levelName"] = room.level.name;
        history["winnerName"] = match.draw ? "Draw" :
            match.winnerUid == room.host.uid ? room.host.name :
            room.guest ? room.guest->name : "Player";
        history["result"] = match.draw ? "draw" : match.winnerUid == s.profile.uid ? "win" : "loss";
        history["playedAt"] = timestamp();
        s.historyWriting = true;
        request("PUT", "history/" + s.profile.uid + "/" + match.id, history,
            [id = match.id](web::WebResponse response) {
                auto& s = state();
                s.historyWriting = false;
                s.nextHistoryAttempt = Clock::now() + std::chrono::seconds(4);
                if (response.ok()) s.recordedMatches.insert(id);
            });
    }
    if (!s.writing && !s.polling && s.room->battle && s.room->battle->finishedAt == 0 &&
        s.room->launch && s.room->launch->releasedAt > 0) {
        auto const& other = isHost() ? s.room->battle->guest : s.room->battle->host;
        if (other.pausedAt > 0 && nowMs() - other.pausedAt >= 30000) {
            auto own = isHost() ? s.room->battle->host : s.room->battle->guest;
            reportBattle(own, false, [](bool, std::string) {});
        }
    }
    if (s.heartbeatTime >= 10.f) heartbeat();
    else if (s.pollTime >= (s.room->started ? .5f : 2.f)) pollRoom();
}
std::string Service::takeNotice() { return std::exchange(state().notice, {}); }
}
