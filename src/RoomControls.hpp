#pragma once

namespace versus {
enum class ReadyAction { None, ChooseMap, Download, Ready, Unready };
struct ReadyControl {
    char const* caption;
    ReadyAction action;
    bool enabled() const { return action != ReadyAction::None; }
};

inline ReadyControl readyControl(bool started, bool pending, bool host,
    bool hasMap, bool cached, bool downloading, bool ready) {
    if (started) return {"Preparing", ReadyAction::None};
    if (pending) return {"Updating...", ReadyAction::None};
    if (!hasMap) return {host ? "Choose Map" : "Wait for Map", host ? ReadyAction::ChooseMap : ReadyAction::None};
    if (ready) return {"Unready", ReadyAction::Unready};
    if (cached) return {"Ready", ReadyAction::Ready};
    return {downloading ? "Downloading..." : "Download", ReadyAction::Download};
}
}
