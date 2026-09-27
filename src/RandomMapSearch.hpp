#pragma once
#include "VersusService.hpp"
namespace versus {
using RandomMapsCallback = std::function<void(std::vector<LevelInfo>, std::string)>;
void findRandomMaps(MapSelection filter, RandomMapsCallback callback);
}
