#pragma once
#include <string>
namespace cocos2d { class CCScene; }
namespace versus {
cocos2d::CCScene* lobbyScene(std::string notice = {});
cocos2d::CCScene* roomScene();
void showLobby(std::string notice = {});
void showRoom();
}
