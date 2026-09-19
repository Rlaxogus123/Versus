#include "../src/MapCache.cpp"
#include <iostream>
#include <thread>
namespace versus { void preloadRunnerAssets() {} }
void check(bool value,char const* message) { if(!value) throw std::runtime_error(message); std::cout<<"PASS "<<message<<'\n'; }
int main() {
 auto& cache=Cache::get(); auto* gm=GameLevelManager::sharedState(); auto* music=MusicDownloadManager::sharedState();
 int callbacks=0; bool success=false;
 auto done=[&](bool ok,std::string){++callbacks;success=ok;};
 versus::ensureMapCached(100,done); versus::ensureMapCached(100,done); cache.update(.2f);
 check(gm->requests.size()==1,"duplicate callers share map download");
 GJGameLevel map{{100},"level-data",123,"123,456,10000001","7,8"}; gm->completed(&map);
 cache.update(.2f); cache.update(.2f);
 check(music->infoRequests.size()==2,"fetch main and additional song metadata");
 check(music->songRequests==std::vector<int>{10000001},"library song uses content URL");
 check(!callbacks && !versus::mapCached(100),"map alone is not ready");
 music->metadata(123);music->metadata(456);cache.update(.2f);
 check(music->songRequests.size()==3,"download every song after URL lookup");
 music->failSong(123);cache.update(.2f);
 check(callbacks==0,"transient audio failure does not finish job");
 music->songs={456,10000001};music->activeSongs.clear();cache.update(.2f);
 music->sounds={7};cache.update(.2f);
 check(!callbacks,"missing sound effect blocks readiness");
 std::this_thread::sleep_for(std::chrono::milliseconds(2100));cache.update(.2f);
 check(std::count(music->songRequests.begin(),music->songRequests.end(),123)==2,"failed audio retries after backoff");
 music->songs.insert(123);music->sounds.insert(8);cache.update(.2f);
 check(callbacks==2 && success && versus::mapCached(100),"all map/audio assets release both callbacks");
 check(music->delegates.empty(),"music delegate detached after callback iteration");
 versus::ensureMapCached(100,done);check(callbacks==3,"cached map completes immediately");
 versus::ensureMapCached(200,done);cache.update(.2f);gm->failed(999);cache.update(.2f);
 check(callbacks==3 && gm->m_levelDownloadDelegate,"unrelated map failure ignored");
 gm->failed(200);cache.update(.2f);check(callbacks==3,"transient map failure retries");
 std::this_thread::sleep_for(std::chrono::milliseconds(2100));cache.update(.2f);
 check(std::count(gm->requests.begin(),gm->requests.end(),200)==2,"map request retried");
 GJGameLevel builtIn{{200},"level-data"};gm->completed(&builtIn);cache.update(.2f);
 check(callbacks==4 && success,"built-in audio needs no custom song download");
 // Exhaust a song's retry budget. Native delegate iteration must remain valid
 // even on the terminal error (the previous implementation erased it here).
 GJGameLevel unavailable{{300},"level-data",999};gm->saved[300]=&unavailable;
 music->info[999]={999,"https://audio.invalid/unavailable.mp3"};
 versus::ensureMapCached(300,done);cache.update(.2f);
 for(int attempt=1;attempt<=3;++attempt) {
  music->failSong(999);check(callbacks==4,"audio callback defers terminal completion");
  cache.update(.2f);
  if(attempt<3) { std::this_thread::sleep_for(std::chrono::milliseconds(attempt*2000+100)); cache.update(.2f); }
 }
 check(callbacks==5 && !success && music->delegates.empty(),"terminal failure safely finishes after three tries");
}
