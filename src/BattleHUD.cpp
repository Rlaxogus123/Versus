#include "BattleHUD.hpp"
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <algorithm>

using namespace geode::prelude;
namespace versus {
namespace {
ccColor3B rgb(int value) { return ccc3((value >> 16) & 255, (value >> 8) & 255, value & 255); }

// A small native vector heart keeps the HUD crisp on every texture quality.
CCDrawNode* lifeHeart() {
    auto* node = CCDrawNode::create();
    auto shape = [node](float scale, ccColor4F color) {
        node->drawDot({-3.4f * scale, 2.3f * scale}, 4.2f * scale, color);
        node->drawDot({3.4f * scale, 2.3f * scale}, 4.2f * scale, color);
        CCPoint lower[] = {{-7.2f * scale, 1.5f * scale}, {7.2f * scale, 1.5f * scale}, {0.f, -7.f * scale}};
        node->drawPolygon(lower, 3, color, 0.f, color);
    };
    shape(1.f, {.13f, .03f, .12f, 1.f});
    shape(.79f, {1.f, .26f, .43f, 1.f});
    node->drawSegment({-4.f, 3.f}, {-2.8f, 4.f}, .8f, {1.f, .82f, .88f, 1.f});
    return node;
}

void text(CCLabelBMFont* label, std::string const& value, float width, float scale) {
    if (value == label->getString()) return;
    label->setString(value.c_str());
    label->limitLabelWidth(width, scale, .14f);
}

struct Card {
    CCLabelBMFont* progress = nullptr;
    CCLabelBMFont* detail = nullptr;
    CCDrawNode* heart = nullptr;
    float textWidth = 0.f;

    void create(CCNode* parent, PlayerProfile const& player, GameRules const& rules,
        CCPoint position, float width, bool mirrored) {
        constexpr float height = 46.f;
        auto* root = CCNode::create();
        root->setPosition(position);
        root->setContentSize({width, height});
        parent->addChild(root);

        auto* background = CCScale9Sprite::create("square02b_001.png");
        background->setContentSize({width, height});
        background->setPosition({width / 2.f, height / 2.f});
        background->setColor(ccc3(5, 19, 45));
        background->setOpacity(205);
        root->addChild(background);
        auto* gradient = CCLayerGradient::create(ccc4(35, 141, 212, 170), ccc4(7, 36, 78, 55),
            {mirrored ? -1.f : 1.f, -.4f});
        gradient->setContentSize({width - 6.f, height - 6.f});
        gradient->setPosition({3.f, 3.f});
        root->addChild(gradient);

        float const iconX = mirrored ? width - 23.f : 23.f;
        // Soft layered halo: no texture lookups or per-frame drawing.
        auto* accent = CCDrawNode::create();
        for (int i = 6; i >= 1; --i) {
            accent->drawDot({iconX, 23.f}, 12.f + i * 1.5f, {.42f, .8f, 1.f, .018f * (7 - i)});
        }
        accent->drawSegment({9.f, height - 2.f}, {width - 9.f, height - 2.f}, .45f, {.62f, .89f, 1.f, .52f});
        root->addChild(accent);

        auto* manager = GameManager::sharedState();
        int const count = manager ? std::max(1, manager->countForType(IconType::Cube)) : 1;
        auto* icon = SimplePlayer::create(std::clamp(player.icon, 1, count));
        icon->setColors(rgb(player.color1), rgb(player.color2));
        icon->setGlowOutline(ccWHITE);
        icon->setScale(.69f);
        icon->setFlipX(mirrored);
        icon->setPosition({iconX, 23.f});
        root->addChild(icon);

        float const anchor = mirrored ? 1.f : 0.f;
        float const edge = mirrored ? width - 45.f : 45.f;
        textWidth = width - 53.f;
        auto makeLabel = [root, anchor](char const* value, CCPoint at, ccColor3B color) {
            auto* label = CCLabelBMFont::create(value, "bigFont.fnt");
            label->setAnchorPoint({anchor, .5f});
            label->setPosition(at);
            label->setColor(color);
            root->addChild(label);
            return label;
        };
        auto* name = makeLabel(player.name.c_str(), {edge, 35.f}, ccWHITE);
        name->limitLabelWidth(textWidth, .32f, .14f);

        bool const lives = rules.mode == 0 && !rules.practice;
        heart = lifeHeart();
        heart->setPosition({mirrored ? width - 52.f : 52.f, 20.f});
        heart->setVisible(lives);
        root->addChild(heart);
        progress = makeLabel("", {edge + (lives ? (mirrored ? -20.f : 20.f) : 0.f), 20.f},
            ccc3(181, 235, 255));
        detail = makeLabel("", {edge, 7.f}, ccc3(132, 200, 235));
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
    float const inset = 38.f; // Keep the native pause control unobstructed.
    float const width = std::min(176.f, (window.width - inset * 2.f - 48.f) / 2.f);
    float const y = window.height - 74.f;
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
