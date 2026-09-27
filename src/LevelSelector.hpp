#pragma once
#include "VersusService.hpp"
#include <functional>
class GJGameLevel;
namespace versus {
LevelInfo describeLevel(GJGameLevel* level);
void openLevelSearch(std::function<void(LevelInfo)> onSelected);
bool isLevelSearchActive();
void cancelLevelSearch();
}
