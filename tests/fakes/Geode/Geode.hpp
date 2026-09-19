#pragma once
#include <map>
#include <set>
#include <string>
#include <vector>
#include <algorithm>
#include <format>
#include <stdexcept>
namespace gd { using string = std::string; }
namespace fmt { using std::format; }
struct CCNode { bool init() { return true; } void release() {} virtual void update(float) {} };
struct Scheduler { void scheduleUpdateForTarget(CCNode*, int, bool) {} };
struct CCDirector { static CCDirector* sharedDirector() { static CCDirector d; return &d; } Scheduler* getScheduler() { static Scheduler s; return &s; } };
struct CCLabelBMFont { static void create(char const*, char const*) {} };
struct CCSprite { static void createWithSpriteFrameName(char const*) {} };
namespace geode::prelude {
template<class T> struct Ref { T* p = nullptr; Ref(T* v = nullptr):p(v){} T* data() const { return p; } };
namespace log { template<class... T> void warn(char const*, T&&...) {} }
}
struct ID { int n; int value() const { return n; } };
struct GJGameLevel { ID m_levelID; std::string m_levelString; int m_songID = 0; std::string m_songIDs, m_sfxIDs; };
struct SongInfoObject { int m_songID; std::string m_songUrl; bool m_unloaded = false; };
enum class GJSongError { Failed = 1 };
struct LevelDownloadDelegate { virtual void levelDownloadFinished(GJGameLevel*) {} virtual void levelDownloadFailed(int) {} };
struct MusicDownloadDelegate {
 virtual void loadSongInfoFinished(SongInfoObject*) {} virtual void loadSongInfoFailed(int,GJSongError) {}
 virtual void downloadSongFailed(int,GJSongError) {} virtual void downloadSFXFailed(int,GJSongError) {}
};
struct GameLevelManager {
 LevelDownloadDelegate* m_levelDownloadDelegate = nullptr;
 std::map<int,GJGameLevel*> saved; std::set<int> active; std::vector<int> requests;
 static GameLevelManager* sharedState() { static GameLevelManager g; return &g; }
 GJGameLevel* getSavedLevel(int id) { return saved.contains(id) ? saved.at(id) : nullptr; }
 std::string getLevelDownloadKey(int id,bool,int) { return std::to_string(id); }
 bool isDLActive(std::string key) { return active.contains(std::stoi(key)); }
 void downloadLevel(int id,bool,int) { requests.push_back(id); active.insert(id); }
 void completed(GJGameLevel* v) { active.erase(v->m_levelID.n); if(m_levelDownloadDelegate) m_levelDownloadDelegate->levelDownloadFinished(v); }
 void failed(int id) { active.erase(id); if(m_levelDownloadDelegate) m_levelDownloadDelegate->levelDownloadFailed(id); }
};
struct MusicDownloadManager {
 std::vector<MusicDownloadDelegate*> delegates;
 std::set<int> songs,sounds,activeSongs,activeSounds,activeInfo;
 std::map<int,SongInfoObject> info;
 std::vector<int> songRequests,soundRequests,infoRequests;
 std::string m_customContentURL;
 bool enumerating = false;
 static MusicDownloadManager* sharedState() { static MusicDownloadManager m; return &m; }
 bool isSongDownloaded(int id) { return songs.contains(id); }
 bool isSFXDownloaded(int id) { return sounds.contains(id); }
 void tryLoadLibraries() {}
 void addMusicDownloadDelegate(MusicDownloadDelegate* p) { delegates.push_back(p); }
 void removeMusicDownloadDelegate(MusicDownloadDelegate* p) { if(enumerating) throw std::runtime_error("removed delegate during native callback"); std::erase(delegates,p); }
 void getCustomContentURL() { m_customContentURL="https://content.invalid/"; }
 bool isRunningActionForSongID(int id) { return activeSongs.contains(id)||activeInfo.contains(id); }
 bool isDLActive(std::string key) { return activeSounds.contains(std::stoi(key)); }
 std::string getSFXDownloadKey(int id) { return std::to_string(id); }
 SongInfoObject* getSongInfoObject(int id) { return info.contains(id) ? &info.at(id) : nullptr; }
 void getSongInfo(int id,bool) { infoRequests.push_back(id); activeInfo.insert(id); }
 void downloadSong(int id) { if(id<=10000000 && (!info.contains(id)||info.at(id).m_songUrl.empty())) throw std::runtime_error("missing song URL"); songRequests.push_back(id); activeSongs.insert(id); }
 void downloadSFX(int id) { soundRequests.push_back(id); activeSounds.insert(id); }
 void metadata(int id) { info[id]={id,"https://audio.invalid/song.mp3"}; activeInfo.erase(id); enumerating=true; for(auto* d:delegates)d->loadSongInfoFinished(&info.at(id)); enumerating=false; }
 void failSong(int id) { activeSongs.erase(id); enumerating=true; for(auto* d:delegates)d->downloadSongFailed(id,GJSongError::Failed); enumerating=false; }
};
