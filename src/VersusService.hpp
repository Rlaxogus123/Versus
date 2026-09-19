#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace versus {
struct PlayerProfile {
    std::string uid;
    std::string name;
    int accountId = 0;
    int icon = 1;
    int color1 = 0x007BFF;
    int color2 = 0xFFFFFF;
    double winRate = 0;
    int recentGames = 0;
};
struct LevelInfo {
    int64_t id = 0;
    std::string name;
    int difficulty = 0;
    int stars = 0;
    bool demon = false;
    bool autoLevel = false;
    bool operator==(LevelInfo const&) const = default;
};
struct LaunchInfo {
    std::string id;
    int64_t requestedAt = 0;
    int64_t releasedAt = 0;
    bool hostLoaded = false;
    bool guestLoaded = false;
};
struct GameRules {
    int mode = 0; // 0: attempts, 1: percent
    int attempts = 3;
    int targetPercent = 40;
    bool sequence = false;
    bool practice = false;
    bool operator==(GameRules const&) const = default;
};
struct EmoteInfo {
    std::string uid;
    std::string kind;
    int64_t at = 0;
    std::string nonce;
};
struct BattlePlayerState {
    int attemptsUsed = 0;
    int bestPercent = 0;
    int currentPercent = 0;
    int runNumber = 0;
    bool inAttempt = false;
    bool cleared = false;
    bool forfeited = false;
    bool spectating = false;
    bool paused = false;
    int64_t pausedAt = 0;
    double x = 0;
    double y = 0;
    double cameraX = 0;
    double cameraY = 0;
    int64_t updatedAt = 0;
};
struct MatchRecord {
    std::string id, opponentName, levelName, winnerName, result;
    int64_t playedAt = 0, levelId = 0;
    PlayerProfile self, opponent;
    GameRules rules;
    BattlePlayerState selfStats, opponentStats;
    bool detailed = false;
};
struct AttemptRecord { int run = 0; int percent = 0; };
using AttemptsCallback = std::function<void(std::vector<AttemptRecord>, std::vector<AttemptRecord>, std::string)>;
struct BattleInfo {
    std::string id;
    std::string firstUid;
    std::string activeUid;
    std::string firstReachedUid;
    int opponentRunAtFirst = 0;
    std::string winnerUid;
    bool draw = false;
    bool hostReturned = false, guestReturned = false;
    int64_t finishedAt = 0;
    BattlePlayerState host;
    BattlePlayerState guest;
};
struct RoomInfo {
    std::string id;
    std::string name;
    bool privateRoom = false;
    PlayerProfile host;
    std::optional<PlayerProfile> guest;
    LevelInfo level;
    GameRules rules;
    bool hostReady = false;
    bool guestReady = false;
    std::optional<EmoteInfo> emote;
    int64_t hostEmoteAt = 0;
    int64_t guestEmoteAt = 0;
    bool started = false;
    std::optional<LaunchInfo> launch;
    std::optional<BattleInfo> battle;
    int64_t updatedAt = 0;
};
using Done = std::function<void(bool, std::string)>;
using RoomListCallback = std::function<void(std::vector<RoomInfo>, std::string)>;
using HistoryCallback = std::function<void(std::vector<MatchRecord>, std::string)>;

class Service {
public:
    static Service& get();
    void initialize();
    void connect(Done callback);
    bool connected() const;
    PlayerProfile const& profile() const;
    std::optional<RoomInfo> const& room() const;
    bool isHost() const;
    bool busy() const;
    void fetchRooms(RoomListCallback callback);
    void fetchHistory(HistoryCallback callback);
    void fetchAttempts(MatchRecord match, int firstRun, AttemptsCallback callback);
    void recordAttempt(int run, int percent);
    void acknowledgeResult(Done callback);
    void createRoom(std::string name, std::string pin, Done callback);
    void joinRoom(RoomInfo room, std::string pin, Done callback);
    void leaveRoom(Done callback);
    void selectLevel(LevelInfo level, Done callback);
    void configureRules(GameRules rules, Done callback);
    void setReady(bool ready, Done callback);
    void sendEmote(std::string kind, Done callback);
    void startMatch(Done callback);
    void markLoaded(std::string launchId, Done callback);
    void cancelLaunch(std::string launchId, Done callback);
    void reportBattle(BattlePlayerState progress, bool sendPosition, Done callback);
    int64_t serverNow() const;
    // Persistent service polling continues while the native map picker is open.
    void tick(float dt);
    std::string takeNotice();
};
}
