#pragma once
#include "VersusService.hpp"
#include <functional>
namespace versus {
void openLevelSearch(std::function<void(LevelInfo)> onSelected);
bool isLevelSearchActive();
void cancelLevelSearch();
}
