#include "BattleHUD.hpp"
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <algorithm>
#include <array>
#include <cmath>

using namespace geode::prelude;
namespace versus {
namespace {
ccColor3B rgb(int value) { return ccc3((value >> 16) & 255, (value >> 8) & 255, value & 255); }

// Match the vector heart used by Life Calculator's challenge HUD.
CCNodeRGBA* lifeHeart(float size) {
    auto* holder = CCNodeRGBA::create();
    auto* shape = CCDrawNode::create();
    holder->setCascadeOpacityEnabled(true);
    holder->setContentSize({size, size});
    holder->setAnchorPoint({.5f, .5f});
    holder->ignoreAnchorPointForPosition(false);
    constexpr size_t pointsCount = 64;
    constexpr float pi = 3.14159265358979323846f;
    std::array<CCPoint, pointsCount> points;
    float const scale = size / 36.f;
    for (size_t index = 0; index < pointsCount; ++index) {
        float angle = 2.f * pi * static_cast<float>(index) / static_cast<float>(pointsCount);
        float sine = std::sin(angle);
        float x = 16.f * sine * sine * sine;
        float y = 13.f * std::cos(angle) - 5.f * std::cos(2.f * angle) -
            2.f * std::cos(3.f * angle) - std::cos(4.f * angle);
        points[index] = CCPoint{size * .5f + x * scale, size * .5f + (y + 2.5f) * scale};
    }
    shape->drawPolygon(points.data(), static_cast<unsigned>(points.size()),
        {1.f, .12f, .2f, 1.f}, 1.25f, {.48f, .02f, .06f, 1.f});
    holder->addChild(shape);
    return holder;
}

void text(CCLabelBMFont* label, std::string const& value, float width, float scale) {
    if (value == label->getString()) return;
    label->setString(value.c_str());
    label->limitLabelWidth(width, scale, .14f);
}

struct Card {
    CCLabelBMFont* progress = nullptr;
    CCLabelBMFont* detail = nullptr;
    CCNodeRGBA* heart = nullptr;
    float textWidth = 0.f;

    void create(CCNode* parent, PlayerProfile const& player, GameRules const& rules,
        CCPoint position, float width, bool mirrored) {
        constexpr float height = 54.f;
        auto* root = CCNode::create();
        root->setPosition(position);
        root->setContentSize({width, height});
        parent->addChild(root);

        // Fade from the outer/top corner into gameplay, like GD's chest glow.
        auto* background = CCLayerGradient::create(
            ccc4(255, 255, 255, 100), ccc4(255, 255, 255, 0),
            {mirrored ? -1.f : 1.f, -1.f});
        background->setContentSize({width, height});
        background->setID("status-glow"_spr);
        root->addChild(background);

        float const iconX = mirrored ? width - 25.f : 25.f;

        auto* manager = GameManager::sharedState();
        int const count = manager ? std::max(1, manager->countForType(IconType::Cube)) : 1;
        auto* icon = SimplePlayer::create(std::clamp(player.icon, 1, count));
        icon->setColors(rgb(player.color1), rgb(player.color2));
        icon->setGlowOutline(ccWHITE);
        icon->setScale(.82f);
        icon->setFlipX(mirrored);
        icon->setPosition({iconX, 27.f});
        root->addChild(icon);

        float const anchor = mirrored ? 1.f : 0.f;
        float const edge = mirrored ? width - 49.f : 49.f;
        textWidth = width - 58.f;
        auto makeLabel = [root, anchor](char const* value, CCPoint at, ccColor3B color) {
            auto* label = CCLabelBMFont::create(value, "bigFont.fnt");
            label->setAnchorPoint({anchor, .5f});
            label->setPosition(at);
            label->setColor(color);
            root->addChild(label);
            return label;
        };
        auto* name = makeLabel(player.name.c_str(), {edge, 41.f}, ccWHITE);
        name->limitLabelWidth(textWidth, .32f, .14f);

        bool const lives = rules.mode == 0 && !rules.practice;
        heart = lifeHeart(16.f);
        heart->setPosition({mirrored ? width - 57.f : 57.f, 24.f});
        heart->setVisible(lives);
        root->addChild(heart);
        progress = makeLabel("", {edge + (lives ? (mirrored ? -22.f : 22.f) : 0.f), 24.f},
            ccc3(181, 235, 255));
        detail = makeLabel("", {edge, 8.f}, ccc3(132, 200, 235));
    }

    void update(BattlePlayerState const& state, GameRules const& rules) {
        bool const lives = rules.mode == 0 && !rules.practice;
        int const remaining = std::max(0, rules.attempts - state.attemptsUsed);
        std::string value;
        if (lives) value = std::to_string(remaining);
        else if (rules.mode == 1) value = fmt::format("{}%", state.bestPercent);
        else value = fmt::format("ATT {}", std::max(1, state.attemptsUsed + (state.inAttempt ? 1 : 0)));
        text(progress, value, textWidth - (lives ? 20.f : 0.f), .43f);
        progress->setColor(lives && remaining <= 1 ? ccc3(255, 163, 178) : ccc3(183, 238, 255));
        heart->setOpacity(lives && remaining == 0 ? 90 : 255);

        std::string info;
        ccColor3B tint = ccc3(136, 207, 243);
        if (state.forfeited) { info = "FORFEITED"; tint = ccc3(255, 146, 162); }
        else if (state.paused) { info = "PAUSED"; tint = ccc3(255, 216, 135); }
        else if (state.cleared) { info = "CLEAR!"; tint = ccc3(138, 255, 193); }
        else if (state.spectating) info = fmt::format("WAIT | BEST {}%", state.bestPercent);
        else if (rules.mode == 1) info = fmt::format("TARGET {}%", rules.targetPercent);
        else info = fmt::format("BEST {}%", state.bestPercent);
        text(detail, info, textWidth, .22f);
        detail->setColor(tint);
    }
};
}

struct BattleHUD::Impl {
    GameRules rules;
    Card host, guest;
};

BattleHUD::BattleHUD() : m(std::make_unique<Impl>()) {}
BattleHUD::~BattleHUD() = default;

BattleHUD* BattleHUD::create(PlayerProfile const& host, PlayerProfile const& guest, GameRules const& rules) {
    auto* node = new BattleHUD;
    if (node->init(host, guest, rules)) { node->autorelease(); return node; }
    delete node;
    return nullptr;
}

bool BattleHUD::init(PlayerProfile const& host, PlayerProfile const& guest, GameRules const& rules) {
    if (!CCNode::init()) return false;
    m->rules = rules;
    auto const window = CCDirector::sharedDirector()->getWinSize();
    setContentSize(window);
    setID("player-cards"_spr);
    float const inset = 4.f;
    float const width = std::min(205.f, (window.width - 116.f) / 2.f);
    float const y = window.height - 58.f;
    m->host.create(this, host, rules, {inset, y}, width, false);
    m->guest.create(this, guest, rules, {window.width - inset - width, y}, width, true);
    updatePlayers({}, {});
    return true;
}

void BattleHUD::updatePlayers(BattlePlayerState const& host, BattlePlayerState const& guest) {
    m->host.update(host, m->rules);
    m->guest.update(guest, m->rules);
}
}
