#include "VersusService.hpp"
#include "FirebaseConfig.hpp"
#include "RoomMembership.hpp"
#include "BattleProgress.hpp"
#include "RandomMapPolicy.hpp"
#include "RandomMapSearch.hpp"
#include "RoomScore.hpp"
#include "MapCache.hpp"
#include "Edition.hpp"

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
struct PendingAttempt {
    std::string match, room, uid;
    int run, percent;
    Clock::time_point queued = Clock::now();
};
struct State {
    bool initialized = false;
    bool authenticating = false;
    bool writing = false;
    bool maintenanceWriting = false;
    bool searchingMap = false;
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
    bool historyWriting = false, attemptWriting = false;
    std::string historyError;
    std::string returningMatch;
    Clock::time_point returnRequestedAt, nextReturnAttempt;
    Clock::time_point nextAttemptWrite;
    Clock::time_point nextDrawAttempt;
    Clock::time_point nextDownloadReport;
    std::deque<PendingAttempt> attempts;
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
    profile.membership = isMembershipEdition();
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
    if (profile.membership) value["membership"] = true;
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
    result.membership = boolAt(value, "membership");
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
        auto const hostState = parseBattlePlayer(host);
        auto const guestState = parseBattlePlayer(guest);
        if (battle::earlyAttemptWin(hostState, guestState, rules)) winner = hostUid;
        else if (battle::earlyAttemptWin(guestState, hostState, rules)) winner = guestUid;
        else if ((rules.practice && boolAt(host, "cleared") && boolAt(guest, "cleared")) ||
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
LevelInfo parseLevel(Json const& value) {
    return {intAt(value, "id"), stringAt(value, "name"), static_cast<int>(intAt(value, "difficulty")),
        static_cast<int>(intAt(value, "stars")), boolAt(value, "demon"), boolAt(value, "autoLevel"), boolAt(value, "platformer"), stringAt(value, "creator"),
        static_cast<int>(std::clamp<int64_t>(intAt(value, "featureState"), 0, 4))};
}
Json levelJson(LevelInfo const& level) {
    auto value = Json::object(); value["id"] = level.id; value["name"] = level.name;
    value["difficulty"] = level.autoLevel ? -1 : level.difficulty; value["stars"] = level.stars;
    value["demon"] = level.demon; value["autoLevel"] = level.autoLevel; value["platformer"] = level.platformer;
    value["creator"] = level.creator;
    value["featureState"] = level.featureState;
    return value;
}
MapSelection parseMapSelection(Json const& value) {
    return {boolAt(value, "random"), value.contains("mask") ? static_cast<int>(intAt(value, "mask")) :
        legacySelectionMask(static_cast<int>(intAt(value, "difficulty"))), boolAt(value, "platformer")};
}
RoomInfo parseRoom(std::string id, Json const& value) {
    RoomInfo result;
    result.id = std::move(id); result.name = stringAt(value, "name");
    result.privateRoom = boolAt(value, "privateRoom");
    result.host = parseProfile(value["host"]);
    if (hasRoomGuest(value)) result.guest = parseProfile(value["guest"]);
    result.level = parseLevel(value["level"]);
    auto download = [](Json const& item) {
        RoomInfo::DownloadInfo result;
        result.uid = stringAt(item, "uid"); result.levelId = intAt(item, "levelId");
        result.updatedAt = intAt(item, "updatedAt");
        result.percent = static_cast<int>(intAt(item, "percent")); result.mapReady = boolAt(item, "mapReady");
        result.songsDone = static_cast<int>(intAt(item, "songsDone"));
        result.songsTotal = static_cast<int>(intAt(item, "songsTotal"));
        result.soundsDone = static_cast<int>(intAt(item, "soundsDone"));
        result.soundsTotal = static_cast<int>(intAt(item, "soundsTotal"));
        return result;
    };
    result.hostDownload = download(value["downloads"]["host"]);
    result.guestDownload = download(value["downloads"]["guest"]);
    result.mapSelection = parseMapSelection(value["mapSelection"]);
    result.hostWins = static_cast<int>(intAt(value["score"], "host"));
    result.guestWins = static_cast<int>(intAt(value["score"], "guest"));
    result.scoredMatch = stringAt(value["score"], "lastMatch");
    if (value["mapDraw"].isObject()) {
        auto const& source = value["mapDraw"];
        MapDraw draw;
        draw.id = stringAt(source, "id"); draw.at = intAt(source, "at");
        auto const selected = stringAt(source, "selected", "0");
        draw.selected = selected.size() == 1 && selected[0] >= '0' && selected[0] <= '9' ? selected[0] - '0' : -1;
        draw.settled = boolAt(source, "settled");
        auto const& levels = source["levels"];
        // Firebase may represent contiguous numeric keys as an array or object.
        if (levels.isArray()) for (auto const& level : levels.asArray().unwrap()) draw.levels.push_back(parseLevel(level));
        else if (levels.isObject()) for (int i = 0; i < 10; ++i) {
            auto const key = std::to_string(i);
            if (!levels.contains(key)) break;
            draw.levels.push_back(parseLevel(levels[key]));
        }
        if (draw.levels.size() >= 2 && draw.levels.size() <= 10 && draw.selected >= 0 &&
            draw.selected < static_cast<int>(draw.levels.size()) && !draw.id.empty()) result.mapDraw = std::move(draw);
    }
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
        battle.hostReturned = boolAt(source, "hostReturned"); battle.guestReturned = boolAt(source, "guestReturned");
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
    if (response.code() == 401 || response.code() == 403) {
        auto body = response.json();
        if (body && stringAt(body.unwrap(), "error").starts_with("Permission denied"))
            return "Firebase database rules denied this request.";
        return "Firebase authentication failed. Reconnect to Versus.";
    }
    if (response.code() == 412) return "Room changed. Please try again.";
    return "Unable to reach Versus. Check your connection and retry.";
}
using Response = std::function<void(web::WebResponse)>;
void request(std::string method, std::string path, Json body, Response callback,
    std::string etag = {}, bool wantEtag = false, bool roomList = false, bool history = false, std::string firstRun = {}) {
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
    if (!firstRun.empty()) {
        request.param("orderBy", "\"run\"");
        request.param("startAt", std::stoi(firstRun));
        request.param("endAt", std::stoi(firstRun) + 9);
        request.param("limitToFirst", 10);
    }
    if (method != "GET") {
        request.header("Content-Type", "application/json");
        request.bodyString(body.dump());
    }
    bool const clockSample = method == "PUT" && path.starts_with("rooms/") && body.isObject();
    auto const sentAt = Clock::now();
    auto const url = endpoint(path);
    async::spawn(request.send(std::move(method), url),
        [callback = std::move(callback), clockSample, sentAt, path = std::move(path)](web::WebResponse response) mutable {
            syncServerClock(response);
            if (response.code() == 401 || response.code() == 403)
                log::warn("Versus Firebase request denied at {} (HTTP {}): {}", path, response.code(), networkError(response));
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
    s.searchingMap = false;
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
// A peer may already have reset this match and readied the next one. Adopt
// that read without issuing a write that would clear the new ready state.
constexpr char const* ROOM_UNCHANGED = "\x1froom-unchanged";
void mutateRoom(std::string id, uint64_t epoch, Mutator change, Done callback, int attempts = 3) {
    if (!Service::get().connected()) {
        Service::get().connect([id = std::move(id), epoch, change = std::move(change),
                                callback = std::move(callback), attempts](bool ok, std::string error) mutable {
            if (epoch != state().epoch) { callback(false, "Room session changed."); return; }
            if (!ok) { callback(false, std::move(error)); return; }
            mutateRoom(std::move(id), epoch, std::move(change), std::move(callback), attempts);
        });
        return;
    }
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
            if (error == ROOM_UNCHANGED) {
                adoptRoom(id, body); callback(true, {}); return;
            }
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
        // Give pending progress writes a dispatch window after slow reads.
        s.pollTime = 0.f;
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
                disconnected(stringAt(body, "lastKickUid") == s.profile.uid ?
                    "You were removed from the room by the host." : "Your room connection expired."); return;
            }
        }
        adoptRoom(id, body);
    });
}
void reportDownload() {
    auto& s = state();
    if (!s.room || s.writing || s.polling || s.authenticating ||
        (s.room->started && (!s.room->launch || s.room->launch->releasedAt > 0)) ||
        s.room->level.id <= 0 || Clock::now() < s.nextDownloadReport) return;
    s.nextDownloadReport = Clock::now() + std::chrono::seconds(2);
    auto const levelID = s.room->level.id;
    auto const progress = mapDownloadProgress(levelID);
    bool const host = s.room->host.uid == s.profile.uid;
    auto const& own = host ? s.room->hostDownload : s.room->guestDownload;
    if (own.uid == s.profile.uid && own.levelId == levelID && own.percent == progress.percent &&
        own.mapReady == progress.mapReady && own.songsDone == progress.songsDone &&
        own.songsTotal == progress.songsTotal && own.soundsDone == progress.soundsDone &&
        own.soundsTotal == progress.soundsTotal && (progress.complete() || nowMs() - own.updatedAt < 10000)) return;
    auto const epoch = s.epoch;
    s.writing = s.maintenanceWriting = true;
    mutateRoom(s.room->id, epoch, [levelID, progress](Json& body) -> std::string {
        auto const uid = state().profile.uid;
        bool const host = stringAt(std::as_const(body)["host"], "uid") == uid;
        if (!host && stringAt(std::as_const(body)["guest"], "uid") != uid) return "Your room connection expired.";
        if ((boolAt(body, "started") && intAt(std::as_const(body)["launch"], "releasedAt") > 0) ||
            intAt(std::as_const(body)["level"], "id") != levelID) return ROOM_UNCHANGED;
        auto value = Json::object();
        value["uid"] = uid; value["levelId"] = levelID; value["percent"] = progress.percent;
        value["mapReady"] = progress.mapReady;
        value["songsDone"] = progress.songsDone; value["songsTotal"] = progress.songsTotal;
        value["soundsDone"] = progress.soundsDone; value["soundsTotal"] = progress.soundsTotal;
        value["updatedAt"] = timestamp();
        body["downloads"][host ? "host" : "guest"] = value;
        return {};
    }, [epoch](bool ok, std::string detail) {
        if (epoch != state().epoch) return;
        state().writing = state().maintenanceWriting = false;
        if (!ok) {
            state().nextDownloadReport = Clock::now() + std::chrono::seconds(10);
            log::warn("Versus download progress sync: {}", detail);
        }
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
                resetRoomScore(body);
                body["hostReady"] = false; body["guestReady"] = false;
                body["launch"] = nullptr; body["battle"] = nullptr;
                body.erase("mapDraw");
                if (boolAt(std::as_const(body)["mapSelection"], "random")) body.erase("level");
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
    return state().searchingMap || (state().writing && !state().maintenanceWriting) ||
        (!state().pendingActions.empty() && !state().dispatchingAction) || state().authenticating;
}
int64_t Service::serverNow() const { return nowMs(); }
bool Service::battleReportAvailable() const { return !state().writing && !state().polling && !busy(); }
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
    fetchPlayerHistory({}, std::move(callback));
}
void Service::fetchPlayerHistory(std::string uid, HistoryCallback callback) {
    connect([uid = std::move(uid), callback = std::move(callback)](bool ok, std::string error) mutable {
        if (!ok) { callback({}, std::move(error)); return; }
        if (uid.empty()) uid = state().profile.uid;
        bool const own = uid == state().profile.uid;
        auto const& current = state().room;
        if (!own && (!current || !current->guest ||
            (uid != current->host.uid && uid != current->guest->uid))) {
            callback({}, "This player is no longer in your room."); return;
        }
        auto read = [uid, own, callback = std::move(callback)](bool allowed, std::string error) mutable {
        if (!allowed) { callback({}, std::move(error)); return; }
        request("GET", "history/" + uid, {}, [own, callback = std::move(callback)](web::WebResponse response) mutable {
            auto parsed = response.json();
            if (!response.ok() || !parsed) { callback({}, networkError(response)); return; }
            std::vector<MatchRecord> records;
            if (parsed.unwrap().isObject()) for (auto const& entry : parsed.unwrap()) {
                MatchRecord record;
                record.id = entry.getKey().value_or(""); record.opponentName = stringAt(entry, "opponentName");
                record.levelName = stringAt(entry, "levelName"); record.winnerName = stringAt(entry, "winnerName");
                record.result = stringAt(entry, "result"); record.playedAt = intAt(entry, "playedAt");
                record.levelId = intAt(entry, "levelId");
                record.detailed = entry["self"].isObject() && entry["opponent"].isObject();
                if (record.detailed) {
                    record.self = parseProfile(entry["self"]); record.opponent = parseProfile(entry["opponent"]);
                    record.selfStats = parseBattlePlayer(entry["selfStats"]); record.opponentStats = parseBattlePlayer(entry["opponentStats"]);
                    record.rules = parseRules(entry["rules"]);
                }
                records.push_back(std::move(record));
            }
            std::sort(records.begin(), records.end(), [](auto const& a, auto const& b) { return a.playedAt > b.playedAt; });
            if (records.size() > 10) records.resize(10);
            if (own) {
                auto& profile = state().profile; profile.recentGames = static_cast<int>(records.size());
                auto wins = std::count_if(records.begin(), records.end(), [](auto const& r) { return r.result == "win"; });
                profile.winRate = records.empty() ? 0. : 100. * wins / records.size();
            }
            callback(std::move(records), {});
        }, {}, false, false, true);
        };
        if (own) { read(true, {}); return; }
        // A single bounded grant per viewer. Rules re-check live membership on
        // every history read, so leaving/replacing a seat revokes access.
        auto grant = Json::object();
        grant["roomId"] = current->id; grant["targetUid"] = uid;
        request("PUT", "historyAccess/" + state().profile.uid, std::move(grant),
            [read = std::move(read)](web::WebResponse response) mutable {
                read(response.ok(), response.ok() ? "" : networkError(response));
            });
    });
}
void Service::fetchAttempts(MatchRecord match, int firstRun, AttemptsCallback callback) {
    struct Result { std::vector<AttemptRecord> own, other; std::string error; int pending = 2; AttemptsCallback done; };
    auto result = std::make_shared<Result>(); result->done = std::move(callback);
    for (bool own : {true, false}) {
        auto uid = own ? match.self.uid : match.opponent.uid;
        request("GET", "matchAttempts/" + match.id + "/" + uid, {}, [result, own](web::WebResponse response) {
            auto parsed = response.json();
            if (!response.ok() || !parsed) result->error = networkError(response);
            else if (parsed.unwrap().isObject()) for (auto const& value : parsed.unwrap()) {
                (own ? result->own : result->other).push_back({static_cast<int>(intAt(value, "run")), static_cast<int>(intAt(value, "percent"))});
            }
            if (--result->pending == 0) result->done(std::move(result->own), std::move(result->other), std::move(result->error));
        }, {}, false, false, false, fmt::format("{}", std::clamp(firstRun, 1, 1000000)));
    }
}
void Service::recordAttempt(int run, int percent) {
    auto& s = state();
    if (!s.room || !s.room->battle || run <= 0 || run > 1000000) return;
    auto const& id = s.room->battle->id;
    for (auto const& entry : s.attempts) if (entry.match == id && entry.run == run) return;
    s.attempts.push_back({id, s.room->id, s.profile.uid, run, std::clamp(percent, 0, 100)});
}
void Service::acknowledgeResult(Done callback) {
    auto& s = state();
    if (!s.room || !s.room->battle) { callback(true, {}); return; }
    if (s.room->battle->finishedAt <= 0) { callback(false, "Match is still active."); return; }
    if (s.returningMatch != s.room->battle->id) {
        s.returningMatch = s.room->battle->id;
        s.returnRequestedAt = Clock::now();
        s.nextReturnAttempt = Clock::now();
    }
    // Returning to the room never waits on archival IO or CAS retries. The
    // service survives scene changes and finishes the return handshake there.
    callback(true, {});
}
void Service::createRoom(std::string, std::string pin, Done callback) {
    if (busy() || state().room) { callback(false, "Leave your current room first."); return; }
    if (!pin.empty() && !validPin(pin)) { callback(false, "Use a four-digit password."); return; }
    connect([pin = std::move(pin), callback = std::move(callback)](bool ok, std::string error) mutable {
        if (!ok) { callback(false, std::move(error)); return; }
        auto& s = state(); if (s.writing || s.room) { callback(false, "Already joining a room."); return; }
        auto name = s.profile.name + "'s Room";
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
                resetRoomScore(body);
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
                resetRoomScore(body);
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
            resetRoomScore(body);
            body["hostReady"] = false; body["guestReady"] = false;
            body["launch"] = nullptr; body["battle"] = nullptr;
            body.erase("mapDraw");
            if (boolAt(std::as_const(body)["mapSelection"], "random")) body.erase("level");
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
void Service::kickGuest(std::string uid, Done callback) {
    if (!isHost() || busy() || !state().room->guest || state().room->started) {
        callback(false, "Only the host can remove a challenger in the waiting room."); return;
    }
    auto const epoch = state().epoch; state().writing = true;
    mutateRoom(state().room->id, epoch, [uid](Json& body) -> std::string {
        if (stringAt(std::as_const(body)["host"], "uid") != state().profile.uid || boolAt(body, "started"))
            return "The room has changed. Try again in the waiting room.";
        if (stringAt(std::as_const(body)["guest"], "uid") != uid) return "The challenger has changed.";
        body["lastKickUid"] = uid;
        body.erase("guest"); body.erase("guestSeen");
        body["hostReady"] = false; body["guestReady"] = false;
        body.erase("mapDraw");
        if (boolAt(std::as_const(body)["mapSelection"], "random")) body.erase("level");
        resetRoomScore(body);
        return {};
    }, [epoch, callback = std::move(callback)](bool ok, std::string detail) mutable {
        if (epoch == state().epoch) state().writing = false;
        callback(ok, std::move(detail));
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
        if (boolAt(std::as_const(body)["mapSelection"], "random")) return "Switch to Map Select first.";
        if (level.platformer && parseRules(std::as_const(body)["rules"]).mode == 1)
            return "Platformer maps do not support Percent mode. Choose Attempts mode first.";
        auto value = levelJson(level);
        if (std::as_const(body)["level"] != value) { body["hostReady"] = false; body["guestReady"] = false; }
        body["level"] = value; return {};
    }, [epoch, callback = std::move(callback)](bool ok, std::string error) mutable {
        if (epoch == state().epoch) state().writing = false;
        callback(ok, std::move(error));
    });
}
void Service::configureMapSelection(MapSelection selection, Done callback) {
    if (state().maintenanceWriting) {
        deferRoomAction([selection](Done done) { Service::get().configureMapSelection(selection, std::move(done)); }, std::move(callback));
        return;
    }
    if (busy() || !isHost() || !validSelection(selection)) { callback(false, "Only the host can change map selection."); return; }
    auto const epoch = state().epoch; state().writing = true;
    mutateRoom(state().room->id, epoch, [selection](Json& body) -> std::string {
        if (stringAt(std::as_const(body)["host"], "uid") != state().profile.uid || boolAt(body, "started")) return "Room changed.";
        if (std::as_const(body)["mapDraw"].isObject() && !boolAt(std::as_const(body)["mapDraw"], "settled")) return "Wait for the roulette.";
        if (parseMapSelection(std::as_const(body)["mapSelection"]) == selection) return ROOM_UNCHANGED;
        if (selection.random && selection.platformer && parseRules(std::as_const(body)["rules"]).mode == 1)
            return "Platformer maps do not support Percent mode. Choose Attempts mode first.";
        auto config = Json::object(); config["random"] = selection.random;
        config["mask"] = selection.mask; config["platformer"] = selection.platformer;
        body["mapSelection"] = std::move(config);
        body.erase("mapDraw"); body.erase("level");
        body["hostReady"] = false; body["guestReady"] = false;
        return {};
    }, [epoch, callback = std::move(callback)](bool ok, std::string detail) mutable {
        if (epoch == state().epoch) state().writing = false;
        callback(ok, std::move(detail));
    });
}
void Service::finishMapDraw(std::string id, Done callback) {
    if (state().writing || busy() || !isHost()) { callback(false, "Room is busy."); return; }
    auto const epoch = state().epoch; state().writing = true;
    mutateRoom(state().room->id, epoch, [id](Json& body) -> std::string {
        auto room = parseRoom("", body);
        if (!room.mapDraw || room.mapDraw->id != id || boolAt(body, "started")) return "Roulette changed.";
        if (room.mapDraw->settled) return ROOM_UNCHANGED;
        if (nowMs() < room.mapDraw->at + DRAW_END_MS) return "Roulette is still spinning.";
        auto const& level = room.mapDraw->levels[room.mapDraw->selected];
        if (!room.guest || !matchesRandomMap(level, room.mapSelection)) return "Room/filter changed.";
        body["level"] = levelJson(level); body["mapDraw"]["settled"] = true;
        body["hostReady"] = false; body["guestReady"] = false;
        return {};
    }, [epoch, callback = std::move(callback)](bool ok, std::string detail) mutable {
        if (epoch == state().epoch) state().writing = false;
        callback(ok, std::move(detail));
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
        rules.sequence) {
        callback(false, "Invalid game rules."); return;
    }
    auto const epoch = state().epoch;
    state().writing = true;
    mutateRoom(state().room->id, epoch, [rules](Json& body) -> std::string {
        if (stringAt(std::as_const(body)["host"], "uid") != state().profile.uid) return "Only the host can change game rules.";
        if (boolAt(body, "started")) return "The match has already started.";
        if (std::as_const(body)["mapDraw"].isObject() && !boolAt(std::as_const(body)["mapDraw"], "settled")) return "Wait for the roulette.";
        auto const newRules = rulesJson(rules);
        auto const selection = parseMapSelection(std::as_const(body)["mapSelection"]);
        if (rules.mode == 1 && (boolAt(std::as_const(body)["level"], "platformer") ||
            (selection.random && selection.platformer)))
            return "Platformer maps do not support Percent mode. Choose Attempts mode first.";
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
        auto const expectedSelection = state().room ? state().room->mapSelection : MapSelection{};
        deferRoomAction([ready, expectedLevel, expectedRules, expectedSelection](Done done) {
            auto const& current = state().room;
            if (ready && (!current || current->level != expectedLevel || current->rules != expectedRules || current->mapSelection != expectedSelection)) {
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
    auto const expectedSelection = state().room->mapSelection;
    state().writing = true;
    mutateRoom(state().room->id, epoch, [ready, expectedLevel, expectedRules, expectedSelection](Json& body) -> std::string {
        auto const uid = state().profile.uid;
        bool const host = stringAt(std::as_const(body)["host"], "uid") == uid;
        if (!host && stringAt(std::as_const(body)["guest"], "uid") != uid) return "Your room connection expired.";
        if (boolAt(body, "started")) return "The match has already started.";
        auto const current = parseRoom("", body);
        if (drawingMap(current)) return "Wait for the roulette.";
        if (ready && ((!needsRandomDraw(current) && intAt(std::as_const(body)["level"], "id") <= 0) ||
            current.mapSelection != expectedSelection || current.level.platformer != expectedLevel.platformer ||
            current.level.creator != expectedLevel.creator ||
            current.level.featureState != expectedLevel.featureState ||
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
    if (kind != "like" && kind != "smile" && kind != "angry" && kind != "fire" && kind != "money") {
        callback(false, "Unknown emote."); return;
    }
    if (kind == "money" && !isMembershipEdition()) {
        callback(false, "This emote requires the membership edition."); return;
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
    if (needsRandomDraw(*state().room)) {
        auto const expected = *state().room;
        if (drawingMap(expected) || !expected.guest || !expected.hostReady || !expected.guestReady) {
            callback(false, "Both players must be ready for the roulette."); return;
        }
        // Native search can take several requests. Keep Firebase polling and
        // heartbeats alive so neither peer expires during the search.
        auto const epoch = state().epoch; state().searchingMap = true;
        findRandomMaps(expected.mapSelection, [epoch, expected, callback = std::move(callback)]
            (std::vector<LevelInfo> levels, std::string error) mutable {
            if (epoch != state().epoch) { callback(false, "Room changed."); return; }
            state().searchingMap = false;
            if (!error.empty() || levels.size() < 2) {
                callback(false, error.empty() ? "No rated maps found." : std::move(error)); return;
            }
            std::mt19937 random {std::random_device{}()};
            auto const selected = std::uniform_int_distribution<int>(0, static_cast<int>(levels.size()) - 1)(random);
            auto draw = Json::object(); draw["id"] = randomId(); draw["at"] = timestamp();
            draw["selected"] = std::to_string(selected); draw["settled"] = false; draw["levels"] = Json::array();
            for (auto const& level : levels) draw["levels"].push(levelJson(level));
            // Serialize publication with any heartbeat already in flight.
            deferRoomAction([expected, draw, epoch](Done done) {
                state().writing = true;
                mutateRoom(expected.id, epoch, [expected, draw](Json& body) -> std::string {
                    auto current = parseRoom(expected.id, body);
                    if (current.started || current.host.uid != state().profile.uid || !current.guest ||
                        current.guest->uid != expected.guest->uid || current.mapSelection != expected.mapSelection ||
                        current.rules != expected.rules || current.mapDraw ||
                        nowMs() - intAt(body, "guestSeen") > firebase_config::PRESENCE_TIMEOUT_MS ||
                        !current.hostReady || !current.guestReady) return "Room, readiness or filters changed. Try again.";
                    body["mapDraw"] = draw; body.erase("level");
                    body["hostReady"] = false; body["guestReady"] = false; body["hostSeen"] = timestamp();
                    return {};
                }, [epoch, done = std::move(done)](bool ok, std::string detail) mutable {
                    if (epoch == state().epoch) { state().writing = false; state().pollTime = 2.f; }
                    done(ok, std::move(detail));
                });
            }, std::move(callback));
        });
        return;
    }
    auto const epoch = state().epoch; state().writing = true;
    mutateRoom(state().room->id, epoch, [launchId = randomId(), firstHost = (std::random_device{}() & 1) == 0](Json& body) -> std::string {
        if (stringAt(std::as_const(body)["host"], "uid") != state().profile.uid) return "Only the host can start the match.";
        if (boolAt(body, "started")) return "The match has already started.";
        if (!std::as_const(body)["guest"].isObject() || nowMs() - intAt(body, "guestSeen") > firebase_config::PRESENCE_TIMEOUT_MS) return "Wait for another player.";
        if (!boolAt(body, "hostReady") || !boolAt(body, "guestReady")) return "Both players must be ready.";
        if (intAt(std::as_const(body)["level"], "id") <= 0) return "Choose a level first.";
        auto const room = parseRoom("", body);
        if (needsRandomDraw(room)) return "Complete the roulette first.";
        auto const rules = parseRules(std::as_const(body)["rules"]);
        if ((rules.mode != 0 && rules.mode != 1) || rules.attempts < 1 || rules.attempts > 99 ||
            rules.targetPercent < 1 || rules.targetPercent > 100 ||
            rules.sequence) return "Sequence mode is no longer available. Apply the game rules again.";
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
        if (!boolAt(body, "started") && !std::as_const(body)["launch"].isObject() && !std::as_const(body)["battle"].isObject())
            return ROOM_UNCHANGED;
        if (stringAt(std::as_const(body)["launch"], "id") != launchId ||
            stringAt(std::as_const(body)["battle"], "id") != launchId) return "Match session changed.";
        bool const finished = intAt(std::as_const(body)["battle"], "finishedAt") > 0;
        if (finished) settleRoomScore(body);
        body["started"] = false;
        body["hostReady"] = false; body["guestReady"] = false;
        body["launch"] = nullptr; body["battle"] = nullptr;
        if (finished && boolAt(std::as_const(body)["mapSelection"], "random")) {
            body.erase("mapDraw"); body.erase("level");
        }
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
    if (state().writing || state().polling || state().authenticating || busy()) {
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
            // The final snapshot must be adopted even when this client's last
            // progress report races with its opponent's timeout/forfeit.
            if (battle::adoptBattleReport(boolAt(body, "started"),
                    stringAt(std::as_const(body)["launch"], "id"),
                    stringAt(std::as_const(body)["battle"], "id"),
                    intAt(std::as_const(body)["launch"], "releasedAt"),
                    intAt(std::as_const(body)["battle"], "finishedAt"), matchId))
                return ROOM_UNCHANGED;
            auto& battle = body["battle"];
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
                // The CAS response is already a fresh room snapshot.
                state().pollTime = 0.f;
                if (ok) state().heartbeatTime = 0.f;
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
    if (!s.attemptWriting && !s.attempts.empty() && connected() && Clock::now() >= s.nextAttemptWrite) {
        auto entry = s.attempts.front();
        if (Clock::now() - entry.queued > std::chrono::minutes(5)) s.attempts.pop_front();
        else {
            s.attemptWriting = true;
            auto data = Json::object(); data["roomId"] = entry.room;
            data["run"] = entry.run; data["percent"] = entry.percent; data["endedAt"] = timestamp();
            request("PUT", "matchAttempts/" + entry.match + "/" + entry.uid + fmt::format("/r{}", entry.run), data,
                [](web::WebResponse response) {
                    auto& s = state(); s.attemptWriting = false;
                    if (response.ok() && !s.attempts.empty()) s.attempts.pop_front();
                    s.nextAttemptWrite = Clock::now() + (response.ok() ? std::chrono::milliseconds(0) : std::chrono::milliseconds(1500));
                });
        }
    }
    if (!s.room || !s.room->battle || s.room->battle->id != s.returningMatch) s.returningMatch.clear();
    if (!s.room) return;
    dt = std::clamp(dt, 0.f, 1.f); s.pollTime += dt; s.heartbeatTime += dt;
    if (Clock::now() - s.lastContact > std::chrono::seconds(60)) {
        disconnected("Connection lost. Please rejoin the room."); return;
    }
    if (!connected()) {
        if (!s.authenticating && s.pollTime >= 2.f) { s.pollTime = 0; connect([](bool, std::string) {}); }
        return;
    }
    if (isHost() && drawingMap(*s.room) && !s.writing && !busy() && !s.polling &&
        nowMs() >= s.room->mapDraw->at + DRAW_END_MS && Clock::now() >= s.nextDrawAttempt) {
        s.nextDrawAttempt = Clock::now() + std::chrono::seconds(2);
        finishMapDraw(s.room->mapDraw->id, [](bool ok, std::string detail) {
            if (!ok) log::warn("Versus roulette finalization: {}", detail);
        });
        return;
    }
    if (s.room->guest && s.room->battle && s.room->battle->finishedAt > 0 &&
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
        history["levelId"] = room.level.id;
        history["self"] = profileJson(host ? room.host : *room.guest);
        history["opponent"] = profileJson(host ? *room.guest : room.host);
        history["rules"] = rulesJson(room.rules);
        history["selfStats"] = battlePlayerJson(host ? match.host : match.guest);
        history["opponentStats"] = battlePlayerJson(host ? match.guest : match.host);
        s.historyWriting = true;
        request("PUT", "history/" + s.profile.uid + "/" + match.id, history,
            [id = match.id, roomId = room.id, uid = s.profile.uid](web::WebResponse response) {
                auto& s = state();
                s.historyWriting = false;
                s.nextHistoryAttempt = Clock::now() + std::chrono::seconds(4);
                if (response.ok()) { s.recordedMatches.insert(id); s.historyError.clear(); }
                else s.historyError = response.code() == 403 || response.code() == 401 ? "Update Firebase rules to save match details." : "Retrying match history save...";
                if (!response.ok() && response.code() == 412) {
                    // A lost success response must not strand the player on
                    // the result screen. Verify our immutable archive receipt.
                    s.historyWriting = true;
                    request("GET", "history/" + uid + "/" + id, {}, [id, roomId](web::WebResponse check) {
                        auto parsed = check.json(); auto& s = state(); s.historyWriting = false;
                        if (check.ok() && parsed && stringAt(parsed.unwrap(), "roomId") == roomId && intAt(parsed.unwrap(), "playedAt") > 0)
                            { s.recordedMatches.insert(id); s.historyError.clear(); }
                    });
                }
            }, "null_etag");
    }
    if (!s.returningMatch.empty() && !s.writing && !s.polling && !busy() &&
        Clock::now() >= s.nextReturnAttempt &&
        (s.recordedMatches.contains(s.returningMatch) || Clock::now() - s.returnRequestedAt >= std::chrono::seconds(8))) {
        auto const epoch = s.epoch;
        auto const match = s.returningMatch;
        s.writing = true;
        mutateRoom(s.room->id, epoch, [match](Json& body) -> std::string {
            auto& battle = body["battle"];
            if (!boolAt(body, "started") && !battle.isObject()) return ROOM_UNCHANGED;
            if (stringAt(battle, "id") != match) return "Match session changed.";
            if (intAt(battle, "finishedAt") <= 0) return "Match is still active.";
            auto const uid = state().profile.uid;
            bool host = stringAt(std::as_const(body)["host"], "uid") == uid;
            if (!host && stringAt(std::as_const(body)["guest"], "uid") != uid) return "Your room connection expired.";
            auto const flag = host ? "hostReturned" : "guestReturned";
            if (boolAt(battle, flag)) return ROOM_UNCHANGED;
            battle[flag] = true;
            body[host ? "hostSeen" : "guestSeen"] = timestamp();
            return {};
        }, [epoch, match](bool ok, std::string error) {
            auto& s = state();
            if (epoch != s.epoch) return;
            s.writing = false;
            s.nextReturnAttempt = Clock::now() + std::chrono::seconds(1);
            if (ok && s.returningMatch == match) s.returningMatch.clear();
            if (!ok) log::warn("Versus result return will retry: {}", error);
        });
    }
    if (!s.writing && !s.polling && !busy() && s.room->battle && s.room->battle->finishedAt > 0 &&
        s.room->battle->hostReturned && s.room->battle->guestReturned) {
        cancelLaunch(s.room->battle->id, [](bool, std::string) {});
    }
    if (!s.writing && !s.polling && s.pollTime < .5f && s.heartbeatTime < 10.f &&
        s.room->battle && s.room->battle->finishedAt == 0 &&
        s.room->launch && s.room->launch->releasedAt > 0) {
        auto const& other = isHost() ? s.room->battle->guest : s.room->battle->host;
        if (other.pausedAt > 0 && nowMs() - other.pausedAt >= 30000) {
            auto own = isHost() ? s.room->battle->host : s.room->battle->guest;
            reportBattle(own, false, [](bool, std::string) {});
        }
    }
    if (s.heartbeatTime >= 10.f) heartbeat();
    else if (s.pollTime >= (s.room->started ? .25f : 2.f)) pollRoom();
    else reportDownload();
}
std::string Service::takeNotice() { return std::exchange(state().notice, {}); }
}
