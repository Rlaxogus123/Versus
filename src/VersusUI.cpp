#include "VersusUI.hpp"
#include "VersusService.hpp"
#include "LevelSelector.hpp"
#include "MatchLaunch.hpp"
#include "MapCache.hpp"
#include "RoomControls.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/CreatorLayer.hpp>
#include <Geode/binding/LevelInfoLayer.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GJDifficultySprite.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <Geode/ui/General.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <limits>
#include <utility>

using namespace geode::prelude;

namespace versus {
namespace {

constexpr int kRoomPageSize = 4;
constexpr int kHistoryPageSize = 5;
constexpr ccColor3B kIce = {155, 225, 255};
constexpr ccColor3B kMuted = {150, 185, 225};

ccColor3B unpackColor(int color) {
    return ccc3((color >> 16) & 255, (color >> 8) & 255, color & 255);
}

CCLabelBMFont* label(CCNode* parent, std::string const& text, CCPoint position,
    float scale = .45f, float maxWidth = 0.f, ccColor3B color = {255, 255, 255},
    bool left = false, char const* font = "bigFont.fnt") {
    auto* value = CCLabelBMFont::create(text.c_str(), font);
    value->setPosition(position);
    value->setScale(scale);
    value->setColor(color);
    if (left) value->setAnchorPoint({0.f, .5f});
    if (maxWidth > 0.f && value->getContentSize().width * scale > maxWidth) {
        value->setScale(maxWidth / value->getContentSize().width);
    }
    parent->addChild(value);
    return value;
}

CCNode* panel(CCNode* parent, CCPoint position, CCSize size, bool decorated = true) {
    auto* node = CCNode::create();
    node->setPosition(position);
    node->setContentSize(size);
    auto* bg = NineSlice::create(decorated ? "GJ_square02.png" : "square02b_001.png");
    bg->setContentSize(size);
    bg->setPosition(size / 2.f);
    if (!decorated) {
        bg->setColor(ccc3(6, 30, 80));
        bg->setOpacity(170);
    }
    node->addChild(bg, -1);
    parent->addChild(node);
    if (decorated) geode::addSideArt(node, SideArt::All, SideArtStyle::PopupBlue);
    return node;
}

CCMenu* menu(CCNode* parent) {
    auto* value = CCMenu::create();
    value->setPosition({0.f, 0.f});
    parent->addChild(value, 5);
    return value;
}

CCMenuItemSpriteExtra* button(CCMenu* parent, CCObject* target, SEL_MenuHandler action,
    std::string const& text, CCPoint position, float scale = .58f,
    char const* background = "GJ_button_01.png") {
    auto* sprite = ButtonSprite::create(text.c_str(), "goldFont.fnt", background);
    sprite->setScale(scale);
    auto* item = CCMenuItemSpriteExtra::create(sprite, target, action);
    item->setPosition(position);
    parent->addChild(item);
    return item;
}

void cascadeOpacity(CCNode* node) {
    if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(node)) rgba->setCascadeOpacityEnabled(true);
    for (auto* child : CCArrayExt<CCNode*>(node->getChildren())) cascadeOpacity(child);
}

void enabled(CCMenuItemSpriteExtra* item, bool value) {
    item->setEnabled(value);
    cascadeOpacity(item);
    item->setOpacity(value ? 255 : 95);
}

void styleInput(TextInput* input) {
    input->getBGSprite()->setColor(ccc3(7, 31, 86));
    input->getBGSprite()->setOpacity(230);
}

SimplePlayer* player(CCNode* parent, PlayerProfile const& profile, CCPoint point, float scale) {
    auto* manager = GameManager::sharedState();
    int const count = manager ? std::max(1, manager->countForType(IconType::Cube)) : 1;
    auto* icon = SimplePlayer::create(std::clamp(profile.icon, 1, count));
    icon->setColors(unpackColor(profile.color1), unpackColor(profile.color2));
    icon->disableGlowOutline();
    icon->setPosition(point);
    icon->setScale(scale);
    parent->addChild(icon);
    return icon;
}

std::string rate(PlayerProfile const& profile) {
    if (!profile.recentGames) return "--";
    return fmt::format("{:.0f}%", profile.winRate);
}

void error(std::string const& message) {
    FLAlertLayer::create("Versus", message.empty() ? "Please try again." : message.c_str(), "OK")->show();
}

bool validPin(std::string const& pin) {
    return pin.size() == 4 && std::all_of(pin.begin(), pin.end(), [](unsigned char c) {
        return c >= '0' && c <= '9';
    });
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string rulesText(GameRules const& rules) {
    std::string result = rules.mode == 0
        ? (rules.practice ? "Fewest Attempts" : fmt::format("{} Attempts", rules.attempts))
        : fmt::format("First to {}%", rules.targetPercent);
    if (rules.practice) result += " / Practice";
    else if (rules.sequence && rules.mode == 0) result += " / Sequence";
    return result;
}

void readyBadge(CCNode* parent, CCPoint position, bool ready) {
    auto* badge = CCNode::create();
    badge->setPosition(position);
    parent->addChild(badge);
    auto* text = label(badge, ready ? "READY" : "NOT READY", {ready ? 8.f : 0.f, 0.f}, .27f, 100.f,
        ready ? ccc3(150, 255, 175) : kIce);
    if (ready) {
        auto* check = CCDrawNode::create();
        check->drawSegment({-32.f, -1.f}, {-28.f, -5.f}, 1.7f, {0.55f, 1.f, .68f, 1.f});
        check->drawSegment({-28.f, -5.f}, {-20.f, 5.f}, 1.7f, {0.55f, 1.f, .68f, 1.f});
        badge->addChild(check);
    } else {
        text->runAction(CCRepeatForever::create(CCSequence::create(
            CCTintTo::create(.55f, 255, 145, 165), CCTintTo::create(.55f, 255, 225, 135),
            CCTintTo::create(.55f, 135, 255, 180), CCTintTo::create(.55f, 130, 210, 255),
            CCTintTo::create(.55f, 205, 155, 255), nullptr)));
        badge->runAction(CCRepeatForever::create(CCSequence::create(
            CCEaseSineInOut::create(CCScaleTo::create(.8f, 1.06f)),
            CCEaseSineInOut::create(CCScaleTo::create(.8f, 1.f)), nullptr)));
    }
}

// Vector emotes remain legible on every texture quality and never load missing frames.
CCNode* emoteArt(std::string const& kind) {
    auto* art = CCDrawNode::create();
    ccColor4F const ink = {.12f, .18f, .27f, 1.f};
    if (kind == "fire") {
        CCPoint flame[] = {{0, 14}, {4, 5}, {8, 9}, {12, -2}, {8, -11}, {0, -14}, {-9, -10}, {-12, -1}, {-4, 8}};
        art->drawPolygon(flame, 9, {1.f, .39f, .16f, 1.f}, 1.f, ink);
        CCPoint center[] = {{0, 4}, {5, -4}, {2, -10}, {-4, -9}, {-5, -4}};
        art->drawPolygon(center, 5, {1.f, .88f, .27f, 1.f}, 0.f, ink);
    } else if (kind == "like") {
        CCPoint thumb[] = {{-4,-10},{8,-10},{12,2},{7,6},{1,6},{2,14},{-2,15},{-6,4}};
        art->drawPolygon(thumb, 8, {1.f, .83f, .42f, 1.f}, 1.f, ink);
        CCPoint cuff[] = {{-13,-11},{-6,-11},{-6,4},{-13,4}};
        art->drawPolygon(cuff, 4, {.25f,.58f,1.f,1.f}, 1.f, ink);
    } else {
        bool const angry = kind == "angry";
        art->drawDot({0,0}, 14.f, ink);
        art->drawDot({0,0}, 12.8f, angry ? ccColor4F{1.f,.44f,.32f,1.f} : ccColor4F{1.f,.84f,.29f,1.f});
        art->drawDot({-4.5f,3.f}, 1.6f, ink);
        art->drawDot({4.5f,3.f}, 1.6f, ink);
        if (angry) {
            art->drawSegment({-9,9},{-3,6},1.1f,ink);
            art->drawSegment({3,6},{9,9},1.1f,ink);
            art->drawSegment({-5,-6},{0,-4},1.2f,ink);
            art->drawSegment({0,-4},{5,-6},1.2f,ink);
        } else {
            art->drawSegment({-6,-3},{-3,-6},1.2f,ink);
            art->drawSegment({-3,-6},{3,-6},1.2f,ink);
            art->drawSegment({3,-6},{6,-3},1.2f,ink);
        }
    }
    return art;
}

// A small fixed number of tiles moves independently of network/UI refreshes.
class MovingBackground : public CCNode {
    CCNode* m_tiles = nullptr;
    float m_tileWidth = 0.f;
public:
    bool init() override {
        if (!CCNode::init()) return false;
        auto const window = CCDirector::sharedDirector()->getWinSize();
        auto* bg = geode::createLayerBG();
        bg->setColor(ccc3(0, 56, 145));
        addChild(bg);
        auto* first = CCSprite::create("game_bg_02_001.png");
        if (!first || first->getContentSize().height <= 0.f) return true;
        m_tiles = CCNode::create();
        addChild(m_tiles);
        float const scale = window.height / first->getContentSize().height;
        m_tileWidth = first->getContentSize().width * scale;
        if (m_tileWidth <= 0.f) return true;
        int const count = static_cast<int>(std::ceil(window.width / m_tileWidth)) + 2;
        for (int i = 0; i < count; ++i) {
            auto* tile = i ? CCSprite::create("game_bg_02_001.png") : first;
            if (!tile) continue;
            tile->setScale(scale);
            tile->setColor(ccc3(65, 125, 255));
            tile->setOpacity(85);
            tile->setPosition({(i + .5f) * m_tileWidth, window.height / 2.f});
            m_tiles->addChild(tile);
        }
        scheduleUpdate();
        return true;
    }
    void update(float dt) override {
        if (!m_tiles || m_tileWidth <= 0.f) return;
        float x = m_tiles->getPositionX() - 18.f * std::clamp(dt, 0.f, .1f);
        if (x <= -m_tileWidth) x += m_tileWidth;
        m_tiles->setPositionX(x);
    }
    CREATE_FUNC(MovingBackground);
};

class SceneLayer : public CCLayer {
protected:
    CCSize m_window;
    bool m_transitioning = false;
    CCMenuItemSpriteExtra* m_back = nullptr;

    bool initScene(std::string const& title) {
        if (!CCLayer::init()) return false;
        m_window = CCDirector::sharedDirector()->getWinSize();
        addChild(MovingBackground::create(), -10);
        auto* nav = menu(this);
        auto* backSprite = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
        backSprite->setScale(.72f);
        m_back = CCMenuItemSpriteExtra::create(backSprite, this, menu_selector(SceneLayer::onBack));
        m_back->setPosition({23.f, m_window.height - 24.f});
        nav->addChild(m_back);
        label(this, title, {m_window.width / 2.f, m_window.height - 25.f}, .8f, m_window.width - 100.f);
        setKeypadEnabled(true);
        return true;
    }
    virtual void goBack() = 0;
    void onBack(CCObject*) { goBack(); }
public:
    void keyBackClicked() override { goBack(); }
    void onEnter() override {
        CCLayer::onEnter();
        scheduleUpdate();
    }
    void onExit() override {
        unscheduleUpdate();
        CCLayer::onExit();
    }
};

class CreateRoomPopup : public Popup {
    TextInput* m_name = nullptr;
    TextInput* m_pin = nullptr;
    CCLabelBMFont* m_privacy = nullptr;
    CCLabelBMFont* m_status = nullptr;
    CCSprite* m_lockSprite = nullptr;
    CCMenuItemSpriteExtra* m_submit = nullptr;
    CCMenuItemSpriteExtra* m_lock = nullptr;
    bool m_private = false;
    bool m_pending = false;

    bool init() {
        if (!Popup::init(310.f, 260.f, "GJ_square02.png")) return false;
        geode::addSideArt(m_mainLayer, SideArt::All, SideArtStyle::PopupBlue);
        setTitle("Create Room", "bigFont.fnt", .65f);
        label(m_mainLayer, "Room name", {155.f, 207.f}, .36f, 250.f, kIce);
        m_name = TextInput::create(245.f, "Room name");
        m_name->setMaxCharCount(32);
        m_name->setCommonFilter(CommonFilter::Name);
        m_name->setString(Service::get().profile().name + "'s Room");
        m_name->setPosition({155.f, 181.f});
        styleInput(m_name);
        m_mainLayer->addChild(m_name);

        m_lockSprite = CCSprite::createWithSpriteFrameName("GJ_lockGray_001.png");
        m_lockSprite->setScale(.7f);
        m_lock = CCMenuItemSpriteExtra::create(m_lockSprite, this, menu_selector(CreateRoomPopup::onPrivacy));
        m_lock->setPosition({68.f, 137.f});
        m_buttonMenu->addChild(m_lock);
        m_privacy = label(m_mainLayer, "Public room", {100.f, 144.f}, .4f, 175.f, kIce, true);
        label(m_mainLayer, "Tap lock to make private", {100.f, 129.f}, .45f, 175.f, kMuted, true, "chatFont.fnt");
        m_pin = TextInput::create(180.f, "4-digit PIN");
        m_pin->setCommonFilter(CommonFilter::Uint);
        m_pin->setMaxCharCount(4);
        m_pin->setPasswordMode(true);
        m_pin->setPosition({155.f, 96.f});
        styleInput(m_pin);
        m_mainLayer->addChild(m_pin);
        m_pin->setVisible(false);
        m_pin->setEnabled(false);
        m_status = label(m_mainLayer, "2 players  /  1 versus 1", {155.f, 64.f}, .35f, 255.f, kIce);
        m_submit = button(m_buttonMenu, this, menu_selector(CreateRoomPopup::onCreate),
            "Create Room", {155.f, 31.f}, .62f);
        m_lockSprite->setOpacity(110);
        return true;
    }
    void onPrivacy(CCObject*) {
        if (m_pending) return;
        m_private = !m_private;
        m_privacy->setString(m_private ? "Private room" : "Public room");
        m_lockSprite->setOpacity(m_private ? 255 : 110);
        m_lockSprite->setColor(m_private ? kIce : ccc3(255, 255, 255));
        m_pin->setVisible(m_private);
        m_pin->setEnabled(m_private);
        if (!m_private) m_pin->defocus();
    }
    void onCreate(CCObject*) {
        if (m_pending) return;
        std::string const pin = m_private ? std::string(m_pin->getString()) : std::string();
        if (m_private && !validPin(pin)) {
            error("Enter exactly <cy>4 digits</c> for the room PIN.");
            return;
        }
        m_name->defocus();
        m_pin->defocus();
        m_pending = true;
        enabled(m_submit, false);
        enabled(m_lock, false);
        m_closeBtn->setEnabled(false);
        m_status->setString("Creating room...");
        auto name = std::string(m_name->getString());
        if (name.empty()) name = Service::get().profile().name + "'s Room";
        Service::get().createRoom(std::move(name), pin,
            [self = WeakRef<CreateRoomPopup>(this)](bool success, std::string detail) {
                auto owner = self.lock();
                if (!owner) return;
                owner->m_pending = false;
                if (success) {
                    owner->onClose(nullptr);
                    showRoom();
                    return;
                }
                enabled(owner->m_submit, true);
                enabled(owner->m_lock, true);
                owner->m_closeBtn->setEnabled(true);
                owner->m_status->setString("Creation failed. Try again.");
                error(detail);
            });
    }
    void onClose(CCObject* sender) override {
        if (!m_pending) Popup::onClose(sender);
    }
public:
    static CreateRoomPopup* create() {
        auto* result = new CreateRoomPopup;
        if (result->init()) { result->autorelease(); return result; }
        delete result;
        return nullptr;
    }
};

class JoinRoomPopup : public Popup {
    RoomInfo m_room;
    TextInput* m_pin = nullptr;
    CCLabelBMFont* m_status = nullptr;
    CCMenuItemSpriteExtra* m_join = nullptr;
    bool m_pending = false;
    bool init(RoomInfo room) {
        if (!Popup::init(290.f, 200.f, "GJ_square02.png")) return false;
        m_room = std::move(room);
        geode::addSideArt(m_mainLayer, SideArt::All, SideArtStyle::PopupBlue);
        setTitle("Private Room", "bigFont.fnt", .65f);
        label(m_mainLayer, m_room.name, {145.f, 144.f}, .45f, 240.f, kIce);
        m_pin = TextInput::create(205.f, "4-digit PIN");
        m_pin->setCommonFilter(CommonFilter::Uint);
        m_pin->setMaxCharCount(4);
        m_pin->setPasswordMode(true);
        m_pin->setPosition({145.f, 112.f});
        styleInput(m_pin);
        m_mainLayer->addChild(m_pin);
        m_status = label(m_mainLayer, "Enter the room's PIN", {145.f, 77.f}, .33f, 240.f, kMuted);
        m_join = button(m_buttonMenu, this, menu_selector(JoinRoomPopup::onJoin), "Join", {145.f, 35.f}, .65f);
        return true;
    }
    void onJoin(CCObject*) {
        if (m_pending) return;
        auto const pin = std::string(m_pin->getString());
        if (!validPin(pin)) {
            error("Enter exactly <cy>4 digits</c>.");
            return;
        }
        m_pin->defocus();
        m_pending = true;
        enabled(m_join, false);
        m_closeBtn->setEnabled(false);
        m_status->setString("Joining...");
        Service::get().joinRoom(m_room, pin,
            [self = WeakRef<JoinRoomPopup>(this)](bool success, std::string detail) {
                auto owner = self.lock();
                if (!owner) return;
                owner->m_pending = false;
                if (success) {
                    owner->onClose(nullptr);
                    showRoom();
                    return;
                }
                enabled(owner->m_join, true);
                owner->m_closeBtn->setEnabled(true);
                owner->m_status->setString("Unable to join. Try again.");
                error(detail);
            });
    }
    void onClose(CCObject* sender) override {
        if (!m_pending) Popup::onClose(sender);
    }
public:
    static JoinRoomPopup* create(RoomInfo room) {
        auto* result = new JoinRoomPopup;
        if (result->init(std::move(room))) { result->autorelease(); return result; }
        delete result;
        return nullptr;
    }
};

class MatchDetailPopup : public Popup {
    MatchRecord m_match;
    CCNode* m_rows = nullptr;
    CCLabelBMFont* m_pageLabel = nullptr;
    CCMenuItemSpriteExtra *m_previous = nullptr, *m_next = nullptr;
    int m_page = 0, m_generation = 0;
    bool init(MatchRecord match) {
        if (!Popup::init(420.f, 294.f, "GJ_square02.png")) return false;
        m_match = std::move(match);
        setTitle("Match Details", "bigFont.fnt", .6f);
        label(m_mainLayer, m_match.levelName, {210.f, 249.f}, .44f, 270.f, kIce);
        auto* view = button(m_buttonMenu, this, menu_selector(MatchDetailPopup::onView), "View Map", {119.f, 224.f}, .4f);
        auto* copy = button(m_buttonMenu, this, menu_selector(MatchDetailPopup::onCopy), "Copy ID", {299.f, 224.f}, .4f);
        enabled(view, m_match.levelId > 0); enabled(copy, m_match.levelId > 0);
        label(m_mainLayer, m_match.detailed ? rulesText(m_match.rules) : "Details unavailable for this older match",
            {210.f, 203.f}, .55f, 380.f, kIce, false, "chatFont.fnt");
        if (m_match.detailed) {
            player(m_mainLayer, m_match.self, {88.f, 174.f}, .55f);
            player(m_mainLayer, m_match.opponent, {285.f, 174.f}, .55f);
            label(m_mainLayer, m_match.self.name, {107.f, 180.f}, .3f, 110.f, ccWHITE, true);
            label(m_mainLayer, m_match.opponent.name, {304.f, 180.f}, .3f, 100.f, ccWHITE, true);
            label(m_mainLayer, fmt::format("Best {}%", m_match.selfStats.bestPercent), {107.f, 167.f}, .5f, 110.f, kIce, true, "chatFont.fnt");
            label(m_mainLayer, fmt::format("Best {}%", m_match.opponentStats.bestPercent), {304.f, 167.f}, .5f, 100.f, kIce, true, "chatFont.fnt");
        }
        label(m_mainLayer, m_match.result == "draw" ? "DRAW" : m_match.winnerName + " wins", {210.f, 149.f}, .45f, 370.f, kIce, false, "chatFont.fnt");
        m_rows = CCNode::create(); m_mainLayer->addChild(m_rows);
        m_previous = button(m_buttonMenu, this, menu_selector(MatchDetailPopup::onPrevious), "<", {153.f, 21.f}, .4f);
        m_next = button(m_buttonMenu, this, menu_selector(MatchDetailPopup::onNext), ">", {267.f, 21.f}, .4f);
        m_pageLabel = label(m_mainLayer, "", {210.f, 21.f}, .3f, 75.f, kIce);
        fetch(); return true;
    }
    void fetch() {
        m_rows->removeAllChildrenWithCleanup(true);
        int count = std::max(m_match.selfStats.runNumber, m_match.opponentStats.runNumber);
        int pages = std::max(1, (count + 9) / 10); m_page = std::clamp(m_page, 0, pages-1);
        m_pageLabel->setString(fmt::format("{} / {}", m_page+1, pages).c_str());
        enabled(m_previous, m_match.detailed && m_page > 0); enabled(m_next, m_match.detailed && m_page+1 < pages);
        if (!m_match.detailed) return;
        label(m_rows, "Loading attempts...", {210.f, 92.f}, .5f, 360.f, kIce, false, "chatFont.fnt");
        int generation = ++m_generation;
        Service::get().fetchAttempts(m_match, m_page*10+1,
            [self=WeakRef<MatchDetailPopup>(this), generation](std::vector<AttemptRecord> own, std::vector<AttemptRecord> other, std::string detail) {
                auto owner=self.lock(); if(!owner || owner->m_generation!=generation)return;
                owner->m_rows->removeAllChildrenWithCleanup(true);
                if(!detail.empty()) { label(owner->m_rows, "Records unavailable. Reopen to retry.", {210.f,92.f},.48f,365.f,kIce,false,"chatFont.fnt");return; }
                auto lookup=[](auto const& list,int run)->std::string {for(auto const& a:list)if(a.run==run)return fmt::format("{}%",a.percent);return "--";};
                int maxRun=std::max(owner->m_match.selfStats.runNumber,owner->m_match.opponentStats.runNumber);
                for(int row=0;row<10;++row) {
                    int run=owner->m_page*10+row+1; if(run>maxRun)break;
                    float y=135.f-row*10.5f;
                    auto* bg=panel(owner->m_rows,{20.f,y-5.f},{380.f,10.f},false);
                    label(bg,fmt::format("Attempt {}",run),{7.f,5.f},.42f,95.f,kMuted,true,"chatFont.fnt");
                    label(bg,lookup(own,run),{133.f,5.f},.46f,75.f,ccWHITE,false,"chatFont.fnt");
                    label(bg,lookup(other,run),{330.f,5.f},.46f,75.f,ccWHITE,false,"chatFont.fnt");
                }
            });
    }
    void onPrevious(CCObject*) { if(m_page>0){--m_page;fetch();} }
    void onNext(CCObject*) { ++m_page;fetch(); }
    void onCopy(CCObject*) { clipboard::write(fmt::format("{}",m_match.levelId)); Notification::create("Level ID copied",NotificationIcon::Success)->show(); }
    void onView(CCObject*) {
        if(m_match.levelId<=0 || m_match.levelId>std::numeric_limits<int>::max())return;
        auto* level=GJGameLevel::create(); level->m_levelID=static_cast<int>(m_match.levelId);
        level->m_levelName=m_match.levelName;level->m_levelType=GJLevelType::Saved;
        auto* scene=LevelInfoLayer::scene(level,false);
        if(scene) CCDirector::sharedDirector()->pushScene(CCTransitionFade::create(.2f,scene));
    }
public:
    static MatchDetailPopup* create(MatchRecord match) {
        auto* result=new MatchDetailPopup;
        if(result->init(std::move(match))){result->autorelease();return result;}delete result;return nullptr;
    }
};

class HistoryPopup : public Popup {
    CCNode* m_rows = nullptr;
    CCLabelBMFont* m_status = nullptr;
    CCLabelBMFont* m_pageLabel = nullptr;
    CCMenuItemSpriteExtra* m_previous = nullptr;
    CCMenuItemSpriteExtra* m_next = nullptr;
    std::vector<MatchRecord> m_matches;
    int m_page = 0;
    bool init() {
        if (!Popup::init(390.f, 270.f, "GJ_square02.png")) return false;
        geode::addSideArt(m_mainLayer, SideArt::All, SideArtStyle::PopupBlue);
        setTitle("Recent Matches", "bigFont.fnt", .65f);
        label(m_mainLayer, "YOUR LAST 10 GAMES", {195.f, 224.f}, .3f, 300.f, kIce);
        m_rows = CCNode::create();
        m_mainLayer->addChild(m_rows);
        m_status = label(m_mainLayer, "Loading history...", {195.f, 134.f}, .45f, 330.f, kIce);
        m_pageLabel = label(m_mainLayer, "1 / 1", {195.f, 29.f}, .33f, 90.f, kIce);
        m_previous = button(m_buttonMenu, this, menu_selector(HistoryPopup::onPrevious), "<", {139.f, 29.f}, .45f);
        m_next = button(m_buttonMenu, this, menu_selector(HistoryPopup::onNext), ">", {251.f, 29.f}, .45f);
        enabled(m_previous, false);
        enabled(m_next, false);
        Service::get().fetchHistory([self = WeakRef<HistoryPopup>(this)](std::vector<MatchRecord> rows, std::string detail) {
            auto owner = self.lock();
            if (!owner) return;
            if (!detail.empty()) {
                owner->m_status->setString("History unavailable");
                error(detail);
                return;
            }
            owner->m_matches = std::move(rows);
            if (owner->m_matches.size() > 10) owner->m_matches.resize(10);
            owner->render();
        });
        return true;
    }
    void render() {
        m_rows->removeAllChildrenWithCleanup(true);
        int const pages = std::max(1, (static_cast<int>(m_matches.size()) + kHistoryPageSize - 1) / kHistoryPageSize);
        m_page = std::clamp(m_page, 0, pages - 1);
        m_pageLabel->setString(fmt::format("{} / {}", m_page + 1, pages).c_str());
        enabled(m_previous, m_page > 0);
        enabled(m_next, m_page + 1 < pages);
        m_status->setVisible(m_matches.empty());
        m_status->setString("No matches yet");
        auto* rowsMenu = menu(m_rows);
        for (int row = 0; row < kHistoryPageSize; ++row) {
            size_t const index = static_cast<size_t>(m_page * kHistoryPageSize + row);
            if (index >= m_matches.size()) break;
            auto const& match = m_matches[index];
            auto* cell = CCSprite::create(); cell->setContentSize({352.f, 29.f});
            panel(cell, {0.f,0.f}, {352.f,29.f}, false);
            auto* item = CCMenuItemSpriteExtra::create(cell, this, menu_selector(HistoryPopup::onDetails));
            item->setPosition({195.f,192.5f-row*32.f});item->setTag(static_cast<int>(index));rowsMenu->addChild(item);
            auto result = match.result;
            std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return std::toupper(c); });
            auto const resultColor = result == "WIN" ? ccc3(130, 255, 165) : result == "LOSS" ? ccc3(255, 160, 160) : kIce;
            label(cell, result.empty() ? "PLAYED" : result, {8.f, 15.f}, .29f, 49.f, resultColor, true);
            label(cell, "vs " + match.opponentName, {64.f, 20.f}, .32f, 134.f, {255, 255, 255}, true);
            if (match.detailed) player(cell, match.opponent, {219.f, 15.f}, .5f);
            label(cell, match.levelName, {64.f, 8.f}, .42f, 165.f, kIce, true, "chatFont.fnt");
            label(cell, "Winner", {287.f, 21.f}, .32f, 100.f, kMuted, false, "chatFont.fnt");
            label(cell, match.winnerName.empty() ? "Draw" : match.winnerName,
                {287.f, 10.f}, .27f, 102.f);
        }
    }
    void onDetails(CCObject* sender) {
        int index=sender->getTag();
        if(index>=0 && index<static_cast<int>(m_matches.size())) if(auto* popup=MatchDetailPopup::create(m_matches[index]))popup->show();
    }
    void onPrevious(CCObject*) { if (m_page > 0) { --m_page; render(); } }
    void onNext(CCObject*) { ++m_page; render(); }
public:
    static HistoryPopup* create() {
        auto* result = new HistoryPopup;
        if (result->init()) { result->autorelease(); return result; }
        delete result;
        return nullptr;
    }
};

class GameRulesPopup : public Popup {
    GameRules m_rules;
    std::string m_roomID;
    TextInput* m_value = nullptr;
    CCLabelBMFont* m_valueTitle = nullptr;
    CCLabelBMFont* m_hint = nullptr;
    CCMenuItemSpriteExtra* m_mode = nullptr;
    CCMenuItemSpriteExtra* m_practice = nullptr;
    CCMenuItemSpriteExtra* m_sequence = nullptr;
    CCMenuItemSpriteExtra* m_less = nullptr;
    CCMenuItemSpriteExtra* m_more = nullptr;
    CCMenuItemSpriteExtra* m_save = nullptr;
    bool m_pending = false;

    void caption(CCMenuItemSpriteExtra* item, std::string const& text, float scale = .5f) {
        auto* sprite = ButtonSprite::create(text.c_str(), "goldFont.fnt", "GJ_button_04.png");
        sprite->setScale(scale);
        item->setSprite(sprite);
    }
    bool init(RoomInfo const& room) {
        if (!Popup::init(330.f, 282.f, "GJ_square02.png")) return false;
        m_rules = room.rules;
        m_roomID = room.id;
        geode::addSideArt(m_mainLayer, SideArt::All, SideArtStyle::PopupBlue);
        setTitle("Game Rules", "bigFont.fnt", .65f);
        m_mode = button(m_buttonMenu, this, menu_selector(GameRulesPopup::onMode), "Attempts", {165.f, 229.f}, .55f);
        m_valueTitle = label(m_mainLayer, "Attempt limit", {165.f, 201.f}, .34f, 250.f, kIce);
        m_value = TextInput::create(96.f, "3");
        m_value->setCommonFilter(CommonFilter::Uint);
        m_value->setMaxCharCount(3);
        m_value->setPosition({165.f, 177.f});
        styleInput(m_value);
        m_mainLayer->addChild(m_value);
        m_less = button(m_buttonMenu, this, menu_selector(GameRulesPopup::onLess), "-", {95.f, 177.f}, .46f);
        m_more = button(m_buttonMenu, this, menu_selector(GameRulesPopup::onMore), "+", {235.f, 177.f}, .46f);
        m_practice = button(m_buttonMenu, this, menu_selector(GameRulesPopup::onPractice), "Practice: OFF", {165.f, 134.f}, .5f);
        m_sequence = button(m_buttonMenu, this, menu_selector(GameRulesPopup::onSequence), "Sequence: OFF", {165.f, 99.f}, .5f);
        m_hint = label(m_mainLayer, "", {165.f, 70.f}, .42f, 275.f, kMuted, false, "chatFont.fnt");
        label(m_mainLayer, "Changing rules resets both players' Ready.", {165.f, 52.f}, .36f, 285.f, kIce, false, "chatFont.fnt");
        m_save = button(m_buttonMenu, this, menu_selector(GameRulesPopup::onSave), "Apply", {165.f, 25.f}, .56f);
        refresh();
        return true;
    }
    bool readValue(bool showError = false) {
        auto const text = std::string(m_value->getString());
        int value = 0;
        auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        int const maximum = m_rules.mode == 0 ? 99 : 100;
        if (parsed.ec != std::errc() || parsed.ptr != text.data() + text.size() || value < 1 || value > maximum) {
            if (showError) error(fmt::format("Enter a number from 1 to {}.", maximum));
            return false;
        }
        if (m_rules.mode == 0) m_rules.attempts = value;
        else m_rules.targetPercent = value;
        return true;
    }
    void refresh() {
        if (m_rules.mode != 0 || m_rules.practice) m_rules.sequence = false;
        caption(m_mode, m_rules.mode == 0 ? "Mode: Attempts" : "Mode: Percent", .55f);
        m_valueTitle->setString(m_rules.mode == 0 ? "Attempt limit (1-99)" : "Target percent (1-100)");
        m_value->setString(std::to_string(m_rules.mode == 0 ? m_rules.attempts : m_rules.targetPercent));
        caption(m_practice, m_rules.practice ? "Practice: ON" : "Practice: OFF");
        caption(m_sequence, m_rules.sequence ? "Sequence: ON" : "Sequence: OFF");
        bool const valueEnabled = !(m_rules.mode == 0 && m_rules.practice) && !m_pending;
        m_value->setEnabled(valueEnabled);
        m_value->getBGSprite()->setOpacity(valueEnabled ? 230 : 105);
        enabled(m_less, valueEnabled);
        enabled(m_more, valueEnabled);
        m_sequence->setVisible(m_rules.mode == 0);
        enabled(m_sequence, !m_rules.practice && m_rules.mode == 0 && !m_pending);
        enabled(m_mode, !m_pending);
        enabled(m_practice, !m_pending);
        m_hint->setString(m_rules.mode != 0 ? "Reach the target in your current attempt." :
            m_rules.practice ? "Clear with the fewest practice attempts." :
            m_rules.sequence ? "Players take turns; the other watches." : "Players play at the same time.");
    }
    void onMode(CCObject*) {
        if (!m_pending) {
            readValue();
            m_rules.mode = 1 - m_rules.mode;
            if (m_rules.mode != 0) m_rules.sequence = false;
            refresh();
        }
    }
    void onPractice(CCObject*) {
        if (m_pending) return;
        readValue();
        m_rules.practice = !m_rules.practice;
        if (m_rules.practice) m_rules.sequence = false;
        refresh();
    }
    void onSequence(CCObject*) { if (!m_pending && !m_rules.practice && m_rules.mode == 0) { readValue(); m_rules.sequence = !m_rules.sequence; refresh(); } }
    void adjust(int amount) {
        if (m_pending) return;
        readValue();
        if (m_rules.mode == 0) m_rules.attempts = std::clamp(m_rules.attempts + amount, 1, 99);
        else m_rules.targetPercent = std::clamp(m_rules.targetPercent + amount, 1, 100);
        refresh();
    }
    void onLess(CCObject*) { adjust(-1); }
    void onMore(CCObject*) { adjust(1); }
    void onSave(CCObject*) {
        if (m_pending || !readValue(true)) return;
        auto const& room = Service::get().room();
        if (!room || room->id != m_roomID || !Service::get().isHost() || room->started) {
            error("Only the host can edit rules before a match.");
            return;
        }
        m_value->defocus();
        m_pending = true;
        refresh();
        enabled(m_save, false);
        m_closeBtn->setEnabled(false);
        Service::get().configureRules(m_rules, [self = WeakRef<GameRulesPopup>(this)](bool success, std::string detail) {
            auto owner = self.lock();
            if (!owner) return;
            owner->m_pending = false;
            if (success) { owner->onClose(nullptr); return; }
            owner->refresh();
            enabled(owner->m_save, true);
            owner->m_closeBtn->setEnabled(true);
            error(detail);
        });
    }
    void onClose(CCObject* sender) override { if (!m_pending) Popup::onClose(sender); }
public:
    static GameRulesPopup* create(RoomInfo const& room) {
        auto* result = new GameRulesPopup;
        if (result->init(room)) { result->autorelease(); return result; }
        delete result;
        return nullptr;
    }
};

class LobbyLayer : public SceneLayer {
    CCNode* m_listPanel = nullptr;
    CCNode* m_statsPanel = nullptr;
    CCNode* m_rows = nullptr;
    CCNode* m_stats = nullptr;
    TextInput* m_search = nullptr;
    CCLabelBMFont* m_status = nullptr;
    CCLabelBMFont* m_pageLabel = nullptr;
    CCLabelBMFont* m_empty = nullptr;
    CCMenuItemSpriteExtra* m_previous = nullptr;
    CCMenuItemSpriteExtra* m_next = nullptr;
    CCMenuItemSpriteExtra* m_create = nullptr;
    CCMenuItemSpriteExtra* m_retry = nullptr;
    CCMenuItemSpriteExtra* m_history = nullptr;
    std::vector<RoomInfo> m_rooms;
    std::vector<size_t> m_filtered;
    int m_page = 0;
    float m_listWidth = 0.f;
    float m_height = 0.f;
    float m_refresh = 0.f;
    float m_filterDelay = -1.f;
    bool m_fetching = false;
    bool m_connecting = false;
    bool m_loadingStats = false;
    bool m_mutating = false;
    std::string m_notice;
    std::string m_lastError;
    int m_statsGames = -1;
    double m_statsRate = -1;

    bool init(std::string notice) {
        if (!initScene("Versus")) return false;
        setID("versus-lobby"_spr);
        m_notice = std::move(notice);
        float const totalWidth = std::min(550.f, m_window.width - 44.f);
        m_height = std::min(250.f, m_window.height - 64.f);
        m_listWidth = totalWidth * .70f - 5.f;
        float const statsWidth = totalWidth - m_listWidth - 12.f;
        float const originX = (m_window.width - totalWidth) / 2.f;
        float const originY = (m_window.height - m_height) / 2.f - 10.f;
        m_listPanel = panel(this, {originX, originY}, {m_listWidth, m_height});
        m_statsPanel = panel(this, {originX + m_listWidth + 12.f, originY}, {statsWidth, m_height});
        label(m_listPanel, "Room List", {m_listWidth / 2.f, m_height - 20.f}, .52f, m_listWidth - 30.f);
        m_search = TextInput::create(m_listWidth - 32.f, "Search room name");
        m_search->setCommonFilter(CommonFilter::Any);
        m_search->setMaxCharCount(32);
        m_search->setScale(.75f);
        m_search->setWidth((m_listWidth - 30.f) / .75f);
        m_search->setPosition({m_listWidth / 2.f, m_height - 48.f});
        styleInput(m_search);
        m_search->setCallback([self = WeakRef<LobbyLayer>(this)](std::string const&) {
            if (auto owner = self.lock()) owner->m_filterDelay = .12f;
        });
        m_listPanel->addChild(m_search);
        m_rows = CCNode::create();
        m_listPanel->addChild(m_rows);
        m_empty = label(m_listPanel, "Connecting...", {m_listWidth / 2.f, m_height / 2.f - 5.f},
            .43f, m_listWidth - 45.f, kIce);
        auto* actions = menu(m_listPanel);
        m_previous = button(actions, this, menu_selector(LobbyLayer::onPrevious), "<", {28.f, 22.f}, .4f);
        m_pageLabel = label(m_listPanel, "1 / 1", {72.f, 22.f}, .28f, 50.f, kIce);
        m_next = button(actions, this, menu_selector(LobbyLayer::onNext), ">", {115.f, 22.f}, .4f);
        m_create = button(actions, this, menu_selector(LobbyLayer::onCreate), "Create Room",
            {m_listWidth - 72.f, 22.f}, .51f);
        enabled(m_previous, false);
        enabled(m_next, false);
        enabled(m_create, false);
        m_status = label(this, "Connecting...", {m_window.width / 2.f, 9.f}, .25f,
            totalWidth - 30.f, kIce);
        label(m_statsPanel, "My Stats", {statsWidth / 2.f, m_height - 20.f}, .5f, statsWidth - 24.f);
        m_stats = CCNode::create();
        m_statsPanel->addChild(m_stats);
        auto* statsActions = menu(m_statsPanel);
        m_history = button(statsActions, this, menu_selector(LobbyLayer::onHistory), "Recent Matches",
            {statsWidth / 2.f, 48.f}, .40f);
        m_retry = button(statsActions, this, menu_selector(LobbyLayer::onRetry), "Refresh",
            {statsWidth / 2.f, 21.f}, .44f, "GJ_button_04.png");
        enabled(m_history, false);
        refreshStats();
        return true;
    }
    void connect() {
        if (m_connecting || m_fetching || m_transitioning) return;
        m_connecting = true;
        m_status->setString("Connecting...");
        m_empty->setString("Connecting...");
        enabled(m_retry, false);
        Service::get().connect([self = WeakRef<LobbyLayer>(this)](bool success, std::string detail) {
            auto owner = self.lock();
            if (!owner || owner->m_transitioning) return;
            owner->m_connecting = false;
            enabled(owner->m_retry, true);
            if (!success) {
                owner->m_lastError = std::move(detail);
                owner->m_status->setString("Connection unavailable. Press Refresh to retry.");
                owner->m_empty->setString("Unable to connect");
                error(owner->m_lastError);
                return;
            }
            owner->m_lastError.clear();
            enabled(owner->m_create, true);
            enabled(owner->m_history, true);
            owner->refreshStats();
            owner->fetchStats();
            owner->fetch();
        });
    }
    void fetchStats() {
        m_loadingStats = true;
        enabled(m_create, false);
        Service::get().fetchHistory([self = WeakRef<LobbyLayer>(this)](std::vector<MatchRecord>, std::string detail) {
            auto owner = self.lock();
            if (!owner || owner->m_transitioning) return;
            owner->m_loadingStats = false;
            enabled(owner->m_create, Service::get().connected() && !owner->m_mutating);
            if (detail.empty()) owner->refreshStats();
            else log::warn("Versus match history unavailable: {}", detail);
            if (!owner->m_rooms.empty()) owner->renderRooms();
        });
    }
    void fetch() {
        if (m_fetching || m_mutating || m_transitioning || !Service::get().connected()) return;
        m_fetching = true;
        m_refresh = 0.f;
        m_status->setString("Refreshing rooms...");
        enabled(m_retry, false);
        Service::get().fetchRooms([self = WeakRef<LobbyLayer>(this)](std::vector<RoomInfo> rooms, std::string detail) {
            auto owner = self.lock();
            if (!owner || owner->m_transitioning) return;
            owner->m_fetching = false;
            enabled(owner->m_retry, true);
            if (!detail.empty()) {
                owner->m_lastError = std::move(detail);
                owner->m_status->setString("Room refresh failed. Refresh to retry.");
                if (owner->m_rooms.empty()) owner->m_empty->setString("Rooms unavailable");
                return;
            }
            owner->m_lastError.clear();
            owner->m_rooms = std::move(rooms);
            std::sort(owner->m_rooms.begin(), owner->m_rooms.end(), [](RoomInfo const& a, RoomInfo const& b) {
                if (a.guest.has_value() != b.guest.has_value()) return !a.guest.has_value();
                if (a.updatedAt != b.updatedAt) return a.updatedAt > b.updatedAt;
                return a.id < b.id;
            });
            owner->renderRooms();
            owner->refreshStats();
        });
    }
    void renderRooms() {
        m_rows->removeAllChildrenWithCleanup(true);
        m_filtered.clear();
        std::string const query = lower(std::string(m_search->getString()));
        for (size_t i = 0; i < m_rooms.size(); ++i) {
            if (query.empty() || lower(m_rooms[i].name).find(query) != std::string::npos) m_filtered.push_back(i);
        }
        int const pages = std::max(1, (static_cast<int>(m_filtered.size()) + kRoomPageSize - 1) / kRoomPageSize);
        m_page = std::clamp(m_page, 0, pages - 1);
        m_pageLabel->setString(fmt::format("{} / {}", m_page + 1, pages).c_str());
        enabled(m_previous, m_page > 0 && !m_mutating);
        enabled(m_next, m_page + 1 < pages && !m_mutating);
        m_empty->setVisible(m_filtered.empty());
        m_empty->setString(query.empty() ? "No rooms yet. Create one!" : "No matching rooms");
        m_status->setString(fmt::format("{} room{}  /  Refreshes every 10 seconds",
            m_filtered.size(), m_filtered.size() == 1 ? "" : "s").c_str());
        auto* rowsMenu = menu(m_rows);
        float const rowStep = (m_height - 105.f) / kRoomPageSize;
        float const rowHeight = rowStep - 3.f;
        for (int row = 0; row < kRoomPageSize; ++row) {
            size_t const filteredIndex = static_cast<size_t>(m_page * kRoomPageSize + row);
            if (filteredIndex >= m_filtered.size()) break;
            auto const index = m_filtered[filteredIndex];
            auto const& room = m_rooms[index];
            bool const full = room.guest.has_value();
            auto const accent = room.rules.mode == 1 ? ccc3(163, 221, 255) : ccc3(80, 221, 255);
            auto const stateColor = room.started ? ccc3(255, 203, 117) : full ? kMuted : ccc3(117, 247, 184);
            auto* rowSprite = CCSprite::create();
            rowSprite->setContentSize({m_listWidth - 24.f, rowHeight});
            float const rowWidth = rowSprite->getContentSize().width;
            auto* bg = NineSlice::create("square02b_001.png");
            bg->setContentSize(rowSprite->getContentSize());
            bg->setColor(ccc3(8, 24, 59));
            bg->setOpacity(245);
            bg->setPosition(rowSprite->getContentSize() / 2.f);
            rowSprite->addChild(bg, -1);
            auto* gradient = CCLayerGradient::create(
                row % 2 ? ccc4(25, 109, 170, 220) : ccc4(17, 81, 151, 220),
                ccc4(10, 31, 70, 170), {1.f, -.3f});
            gradient->setContentSize({rowWidth - 8.f, rowHeight - 6.f});
            gradient->setPosition({4.f, 3.f});
            rowSprite->addChild(gradient, -1);
            auto* trim = CCDrawNode::create();
            trim->drawSegment({5.f, 7.f}, {5.f, rowHeight - 7.f}, 1.4f,
                {accent.r / 255.f, accent.g / 255.f, accent.b / 255.f, .95f});
            trim->drawSegment({10.f, rowHeight - 3.f}, {rowWidth - 10.f, rowHeight - 3.f}, .45f,
                {.65f, .86f, 1.f, .3f});
            trim->drawDot({34.f, rowHeight / 2.f}, rowHeight * .38f, {.12f, .24f, .48f, .9f});
            rowSprite->addChild(trim);
            if (room.privateRoom) {
                auto* lock = CCSprite::createWithSpriteFrameName("GJ_lockGray_001.png");
                lock->setScale(.23f);
                lock->setColor(ccc3(255, 217, 144));
                lock->setPosition({17.f, rowHeight * .28f});
                rowSprite->addChild(lock, 2);
            }
            player(rowSprite, room.host, {34.f, rowHeight / 2.f}, .60f);
            // Two text lines for names; rules occupy their own column.
            float const infoWidth = rowWidth - 122.f;
            float const nameWidth = infoWidth * .53f;
            float const badgeWidth = infoWidth - nameWidth - 7.f;
            float const badgeX = 57.f + nameWidth + 7.f;
            label(rowSprite, room.host.name, {57.f, rowHeight * .68f}, .32f,
                nameWidth, {255, 255, 255}, true);
            label(rowSprite, room.name, {57.f, rowHeight * .31f}, .39f,
                nameWidth, kIce, true, "chatFont.fnt");
            auto* modeBadge = NineSlice::create("square02b_001.png");
            modeBadge->setContentSize({badgeWidth, rowHeight - 10.f});
            modeBadge->setColor(room.rules.mode == 1 ? ccc3(23, 68, 119) : ccc3(7, 56, 85));
            modeBadge->setPosition({badgeX + badgeWidth / 2.f, rowHeight / 2.f});
            rowSprite->addChild(modeBadge);
            label(rowSprite, room.rules.mode == 1 ? "PERCENT" : "ATTEMPTS",
                {badgeX + badgeWidth / 2.f, rowHeight * .66f}, .23f, badgeWidth - 8.f, accent);
            label(rowSprite, rulesText(room.rules),
                {badgeX + badgeWidth / 2.f, rowHeight * .32f}, .33f,
                badgeWidth - 8.f, kIce, false, "chatFont.fnt");
            auto* statusBadge = NineSlice::create("square02b_001.png");
            statusBadge->setContentSize({48.f, rowHeight - 9.f});
            statusBadge->setColor(room.started ? ccc3(79, 56, 31) : full ? ccc3(24, 41, 64) : ccc3(10, 66, 61));
            statusBadge->setPosition({rowWidth - 29.f, rowHeight / 2.f});
            rowSprite->addChild(statusBadge);
            label(rowSprite, full ? "2 / 2" : "1 / 2", {rowWidth - 29.f, rowHeight * .65f},
                .29f, 43.f, stateColor);
            label(rowSprite, room.started ? (room.launch && !room.launch->releasedAt ? "LOADING" : "PLAYING") : full ? "FULL" : "JOIN",
                {rowWidth - 29.f, rowHeight * .28f}, .22f, 43.f, stateColor);
            auto* item = CCMenuItemSpriteExtra::create(rowSprite, this, menu_selector(LobbyLayer::onJoin));
            item->setPosition({m_listWidth / 2.f, m_height - 79.f - row * rowStep});
            item->setTag(static_cast<int>(index));
            rowsMenu->addChild(item);
            item->setEnabled(!full && !room.started && !m_mutating && !m_loadingStats);
        }
    }
    void refreshStats() {
        m_stats->removeAllChildrenWithCleanup(true);
        auto const& profile = Service::get().profile();
        m_statsGames = profile.recentGames;
        m_statsRate = profile.winRate;
        float const width = m_statsPanel->getContentSize().width;
        player(m_stats, profile, {width / 2.f, m_height - 68.f}, 1.4f);
        label(m_stats, profile.name.empty() ? "Player" : profile.name,
            {width / 2.f, m_height - 102.f}, .53f, width - 20.f);
        label(m_stats, "WIN RATE", {width / 2.f, m_height - 128.f}, .27f, width - 22.f, kIce);
        label(m_stats, rate(profile), {width / 2.f, m_height - 148.f}, .76f, width - 22.f);
        label(m_stats, fmt::format("Last {} / 10 matches", std::clamp(profile.recentGames, 0, 10)),
            {width / 2.f, m_height - 170.f}, .38f, width - 20.f, kMuted, false, "chatFont.fnt");
    }
    void onPrevious(CCObject*) { if (m_page > 0 && !m_mutating) { --m_page; renderRooms(); } }
    void onNext(CCObject*) { if (!m_mutating) { ++m_page; renderRooms(); } }
    void onCreate(CCObject*) {
        if (!m_mutating && !m_transitioning && !m_loadingStats && Service::get().connected()) {
            m_search->defocus();
            if (auto* popup = CreateRoomPopup::create()) popup->show();
        }
    }
    void onHistory(CCObject*) {
        if (!m_mutating && !m_transitioning) {
            m_search->defocus();
            if (auto* popup = HistoryPopup::create()) popup->show();
        }
    }
    void onRetry(CCObject*) {
        if (m_mutating || m_transitioning) return;
        if (!Service::get().connected()) connect();
        else {
            if (!m_lastError.empty()) error(m_lastError);
            fetch();
        }
    }
    void onJoin(CCObject* sender) {
        if (m_mutating || m_transitioning || m_loadingStats || Service::get().busy()) return;
        auto* item = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
        if (!item || item->getTag() < 0 || static_cast<size_t>(item->getTag()) >= m_rooms.size()) return;
        auto const room = m_rooms[static_cast<size_t>(item->getTag())];
        if (room.guest || room.started) return;
        m_search->defocus();
        if (room.privateRoom) {
            if (auto* popup = JoinRoomPopup::create(room)) popup->show();
            return;
        }
        m_mutating = true;
        enabled(m_create, false);
        enabled(m_back, false);
        m_status->setString("Joining room...");
        Service::get().joinRoom(room, {}, [self = WeakRef<LobbyLayer>(this)](bool success, std::string detail) {
            auto owner = self.lock();
            if (!owner || owner->m_transitioning) return;
            owner->m_mutating = false;
            if (success) {
                owner->m_transitioning = true;
                showRoom();
                return;
            }
            enabled(owner->m_create, true);
            enabled(owner->m_back, true);
            error(detail);
            owner->fetch();
        });
    }
    void goBack() override {
        if (m_mutating || m_transitioning || Service::get().busy()) return;
        m_transitioning = true;
        m_search->defocus();
        CCDirector::sharedDirector()->replaceScene(CCTransitionFade::create(.25f, CreatorLayer::scene()));
    }
public:
    static LobbyLayer* create(std::string notice) {
        auto* result = new LobbyLayer;
        if (result->init(std::move(notice))) { result->autorelease(); return result; }
        delete result;
        return nullptr;
    }
    void onEnter() override {
        SceneLayer::onEnter();
        geode::queueInMainThread([self = WeakRef<LobbyLayer>(this)] {
            auto owner = self.lock();
            if (!owner || !owner->isRunning() || owner->m_transitioning) return;
            if (!owner->m_notice.empty()) {
                auto message = std::exchange(owner->m_notice, {});
                FLAlertLayer::create("Room Closed", message.c_str(), "OK")->show();
            }
            if (Service::get().connected()) {
                enabled(owner->m_create, true);
                enabled(owner->m_history, true);
                owner->fetchStats();
                owner->fetch();
            } else owner->connect();
        });
    }
    void update(float dt) override {
        if (m_transitioning || m_mutating) return;
        if (m_filterDelay >= 0.f) {
            m_filterDelay -= dt;
            if (m_filterDelay < 0.f) {
                m_page = 0;
                renderRooms();
            }
        }
        if (Service::get().profile().recentGames != m_statsGames || Service::get().profile().winRate != m_statsRate) refreshStats();
        if (!Service::get().connected() || m_fetching || m_connecting) return;
        m_refresh += dt;
        if (m_refresh >= 10.f) fetch();
    }
};

class RoomLayer : public SceneLayer {
    CCNode* m_content = nullptr;
    CCNode* m_emotes = nullptr;
    CCLabelBMFont* m_status = nullptr;
    float m_elapsed = 1.f;
    float m_readyPhase = 0.f;
    NineSlice* m_readyGlow = nullptr;
    ButtonSprite* m_readySprite = nullptr;
    float m_emoteCooldown = 0.f;
    bool m_pending = false;
    bool m_sendingEmote = false;
    bool m_downloading = false;
    bool m_cached = false;
    bool m_renderQueued = false;
    uint64_t m_downloadGeneration = 0;
    int64_t m_mapID = 0;
    std::string m_cacheError;
    std::string m_seenEmote;
    std::string m_signature;
    std::string m_returnedBattle;

    void requestRender() {
        if (m_renderQueued || m_transitioning) return;
        m_renderQueued = true;
        // Never destroy a CCMenu or its selected item inside its touch callback.
        Loader::get()->queueInMainThread([self = WeakRef<RoomLayer>(this)] {
            if (auto owner = self.lock()) {
                owner->m_renderQueued = false;
                if (!owner->m_transitioning) owner->render();
            }
        });
    }

    void animateReady() {
        if (!m_readyGlow || !m_readySprite) return;
        auto channel = [this](float offset) {
            return static_cast<GLubyte>(155.f + 100.f * std::sin(m_readyPhase + offset));
        };
        auto color = ccc3(channel(0.f), channel(2.0944f), channel(4.1888f));
        m_readyGlow->setColor(color);
        m_readyGlow->setOpacity(static_cast<GLubyte>(130.f + 55.f * std::sin(m_readyPhase * 2.f)));
        m_readySprite->m_BGSprite->setColor(color);
    }

    void beginDownload() {
        auto const& room = Service::get().room();
        if (!room || room->level.id <= 0 || m_downloading || mapCached(room->level.id)) {
            if (room && room->level.id > 0) m_cached = mapCached(room->level.id);
            return;
        }
        m_downloading = true;
        m_cacheError.clear();
        auto const levelID = room->level.id;
        auto const generation = ++m_downloadGeneration;
        ensureMapCached(levelID, [self = WeakRef<RoomLayer>(this), levelID, generation](bool success, std::string detail) {
            auto owner = self.lock();
            if (!owner || owner->m_mapID != levelID || owner->m_downloadGeneration != generation) return;
            owner->m_downloading = false;
            owner->m_cached = success && mapCached(levelID);
            if (owner->m_cached) preloadBattleAssets();
            owner->m_cacheError = owner->m_cached ? std::string() : std::move(detail);
            owner->requestRender();
        });
    }

    void showRoomEmote(RoomInfo const& room) {
        if (!room.emote || room.emote->nonce.empty() || room.emote->nonce == m_seenEmote) return;
        m_seenEmote = room.emote->nonce;
        bool const fromHost = room.emote->uid == room.host.uid;
        bool const mineIsHost = Service::get().isHost();
        bool const onLeft = fromHost == mineIsHost;
        float const width = std::min(550.f, m_window.width - 44.f);
        float const height = std::min(250.f, m_window.height - 65.f);
        float const frameX = (m_window.width - width) / 2.f;
        float const frameY = (m_window.height - height) / 2.f - 8.f;
        float const cardWidth = width * .245f;
        auto* bubble = CCNode::create();
        bubble->setPosition({frameX + (onLeft ? 12.f + cardWidth - 3.f : width - cardWidth - 9.f), frameY + height - 70.f});
        bubble->setScale(.15f);
        m_emotes->addChild(bubble);
        auto* bg = NineSlice::create("square02b_001.png");
        bg->setContentSize({48.f, 43.f});
        bg->setColor(ccc3(255, 255, 255));
        bg->setPosition({0.f, 0.f});
        bubble->addChild(bg);
        auto* art = emoteArt(room.emote->kind);
        art->setScale(.85f);
        bubble->addChild(art, 2);
        bubble->runAction(CCSequence::create(
            CCEaseBackOut::create(CCScaleTo::create(.22f, 1.f)),
            CCEaseSineInOut::create(CCMoveBy::create(.22f, {0.f, 8.f})),
            CCDelayTime::create(1.25f),
            CCSpawn::create(CCFadeOut::create(.25f), CCScaleTo::create(.25f, .7f), nullptr),
            CCRemoveSelf::create(), nullptr));
        art->runAction(CCRepeat::create(CCSequence::create(
            CCEaseSineInOut::create(CCRotateTo::create(.12f, -10.f)),
            CCEaseSineInOut::create(CCRotateTo::create(.12f, 10.f)),
            CCEaseSineInOut::create(CCRotateTo::create(.12f, 0.f)), nullptr), 2));
    }

    bool init() {
        if (!initScene("Versus Room")) return false;
        setID("versus-room"_spr);
        m_content = CCNode::create();
        addChild(m_content);
        m_emotes = CCNode::create();
        addChild(m_emotes, 30);
        m_status = label(this, "", {m_window.width / 2.f, 10.f}, .3f, m_window.width - 40.f, kIce);
        render();
        return true;
    }
    std::string signature(RoomInfo const& room) {
        auto profileKey = [](PlayerProfile const& profile) {
            return fmt::format("{}:{}:{}:{}:{}:{:.1f}:{}", profile.uid, profile.name,
                profile.icon, profile.color1, profile.color2, profile.winRate, profile.recentGames);
        };
        auto const launchKey = room.launch ? fmt::format("{}:{}:{}:{}:{}", room.launch->id,
            room.launch->requestedAt, room.launch->releasedAt, room.launch->hostLoaded, room.launch->guestLoaded) : "";
        return fmt::format("{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}", room.id, room.name,
            profileKey(room.host), room.guest ? profileKey(*room.guest) : "", room.level.id,
            room.level.name, room.level.difficulty, room.level.stars, room.level.demon,
            room.level.autoLevel, room.started, m_pending, Service::get().busy(), room.guestReady, launchKey,
            room.hostReady, rulesText(room.rules), m_cached, m_downloading, m_cacheError) +
            (room.battle ? fmt::format(":{}:{}:{}", room.battle->finishedAt, room.battle->hostReturned, room.battle->guestReturned) : "");
    }
    void playerCard(CCNode* parent, std::optional<PlayerProfile> const& profile,
        CCPoint origin, CCSize size, bool mine, bool host, bool ready) {
        auto* card = panel(parent, origin, size, false);
        label(card, mine ? "YOU" : "OPPONENT", {size.width / 2.f, size.height - 13.f}, .29f, size.width - 12.f, kIce);
        if (profile) {
            player(card, *profile, {size.width / 2.f, size.height - 46.f}, 1.3f);
            readyBadge(card, {size.width / 2.f, size.height - 73.f}, ready);
            label(card, profile->name, {size.width / 2.f, size.height - 94.f}, .45f, size.width - 16.f);
            label(card, host ? "HOST" : "CHALLENGER", {size.width / 2.f, 43.f}, .22f, size.width - 15.f, kIce);
            label(card, rate(*profile), {size.width / 2.f, 25.f}, .53f, size.width - 15.f);
            label(card, "LAST 10 WIN RATE", {size.width / 2.f, 10.f}, .20f, size.width - 12.f, kMuted);
        } else {
            // Empty seats use a vector silhouette instead of a missing sprite frame.
            auto* placeholder = CCDrawNode::create();
            placeholder->setPosition({size.width / 2.f, size.height - 48.f});
            placeholder->drawDot({0.f, 8.f}, 8.f, {.38f, .61f, .87f, .65f});
            placeholder->drawSegment({-9.f, -12.f}, {9.f, -12.f}, 8.f, {.38f, .61f, .87f, .65f});
            card->addChild(placeholder);
            label(card, "Waiting...", {size.width / 2.f, size.height - 94.f}, .40f, size.width - 18.f, kIce);
            label(card, "1 / 2 players", {size.width / 2.f, 28.f}, .30f, size.width - 16.f, kMuted);
        }
    }
    void render() {
        auto const& current = Service::get().room();
        if (!current) return;
        auto const room = *current;
        if (m_mapID != room.level.id) {
            ++m_downloadGeneration;
            m_mapID = room.level.id;
            m_cached = room.level.id > 0 && mapCached(room.level.id);
            m_downloading = false;
            m_cacheError.clear();
            if (room.level.id > 0 && !m_cached) beginDownload();
            else if (m_cached) preloadBattleAssets();
        } else if (room.level.id > 0) m_cached = mapCached(room.level.id);
        m_signature = signature(room);
        m_readyGlow = nullptr;
        m_readySprite = nullptr;
        m_content->removeAllChildrenWithCleanup(true);
        float const width = std::min(550.f, m_window.width - 44.f);
        float const height = std::min(250.f, m_window.height - 65.f);
        auto* frame = panel(m_content, {(m_window.width - width) / 2.f, (m_window.height - height) / 2.f - 8.f}, {width, height});
        label(frame, room.name, {width / 2.f, height - 17.f}, .46f, width - 56.f, kIce);
        label(frame, rulesText(room.rules), {width / 2.f, height - 35.f}, .37f,
            width - 75.f, ccc3(160, 220, 200), false, "chatFont.fnt");
        if (room.privateRoom) {
            auto* lock = CCSprite::createWithSpriteFrameName("GJ_lockGray_001.png");
            lock->setScale(.38f);
            lock->setPosition({18.f, height - 18.f});
            frame->addChild(lock);
        }
        bool const host = Service::get().isHost();
        float const cardWidth = width * .245f;
        float const cardHeight = height - 93.f;
        std::optional<PlayerProfile> mine = host ? std::optional<PlayerProfile>(room.host) : room.guest;
        std::optional<PlayerProfile> opponent = host ? room.guest : std::optional<PlayerProfile>(room.host);
        if (!mine) mine = Service::get().profile();
        bool const myReady = host ? room.hostReady : room.guestReady;
        bool const opponentReady = host ? room.guestReady : room.hostReady;
        playerCard(frame, mine, {12.f, 48.f}, {cardWidth, cardHeight}, true, host, myReady);
        playerCard(frame, opponent, {width - cardWidth - 12.f, 48.f}, {cardWidth, cardHeight}, false, !host, opponentReady);
        float const centerWidth = width - 2.f * cardWidth - 40.f;
        float const centerX = width / 2.f;
        // The native sprite's NA/Auto values are reversed from GJDifficulty.
        int const difficulty = room.level.id
            ? (room.level.autoLevel ? -1 : std::clamp(room.level.difficulty, 0, 10)) : 0;
        auto* face = GJDifficultySprite::create(difficulty, GJDifficultyName::Short);
        if (face) {
            face->setScale(.90f);
            face->setPosition({centerX, height - 79.f});
            frame->addChild(face);
        }
        label(frame, room.level.id ? room.level.name : "Choose a map",
            {centerX, height - 114.f}, .45f, centerWidth);
        if (room.level.id) {
            auto* star = CCSprite::createWithSpriteFrameName("star_small01_001.png");
            if (star) {
                star->setScale(.75f);
                star->setPosition({centerX + 15.f, height - 134.f});
                frame->addChild(star);
            }
            label(frame, std::to_string(room.level.stars), {centerX - 3.f, height - 134.f}, .40f, 40.f, ccc3(255, 230, 130));
        } else {
            label(frame, "A map for your duel", {centerX, height - 135.f}, .39f,
                centerWidth, kMuted, false, "chatFont.fnt");
        }
        auto* actions = menu(frame);
        auto const control = readyControl(room.started, m_pending, host, room.level.id > 0,
            m_cached, m_downloading, myReady);
        auto* ready = button(actions, this, menu_selector(RoomLayer::onReady),
            control.caption,
            {12.f + cardWidth / 2.f, 24.f}, .55f,
            myReady ? "GJ_button_04.png" : "GJ_button_01.png");
        ready->setID("ready-button"_spr);
        if (ready->getContentSize().width > cardWidth - 6.f) {
            auto* sprite = static_cast<ButtonSprite*>(ready->getNormalImage());
            sprite->setScale(sprite->getScale() * (cardWidth - 6.f) / ready->getContentSize().width);
            ready->updateSprite();
        }
        // Download/retry uses this same button; do not leave a dead Ready button.
        enabled(ready, control.enabled());
        if (!myReady && m_cached && ready->isEnabled()) {
            // Tint the actual background; menu-item color does not propagate
            // through every ButtonSprite child. Keep phase across room redraws.
            m_readySprite = static_cast<ButtonSprite*>(ready->getNormalImage());
            m_readyGlow = NineSlice::create("square02b_001.png");
            m_readyGlow->setContentSize(ready->getContentSize() + CCSize{8.f, 7.f});
            m_readyGlow->setPosition(ready->getPosition());
            frame->addChild(m_readyGlow, 4);
            animateReady();
        }
        if (host) {
            auto* choose = button(actions, this, menu_selector(RoomLayer::onChoose), "Choose Map",
                {centerX - 18.f, 84.f}, .46f, "GJ_button_04.png");
            auto* gearSprite = CCSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png");
            gearSprite->setScale(.5f);
            auto* gear = CCMenuItemSpriteExtra::create(gearSprite, this, menu_selector(RoomLayer::onRules));
            gear->setPosition({centerX + 66.f, 84.f});
            actions->addChild(gear);
            label(frame, "Game Rule", {centerX + 66.f, 64.f}, .28f, 75.f, kIce, false, "chatFont.fnt");
            enabled(gear, !m_pending && !Service::get().busy() && !room.started);
            auto* start = button(actions, this, menu_selector(RoomLayer::onStart), room.battle && room.battle->finishedAt ? "Finishing" : room.started ? "Preparing" : "Start",
                {width - cardWidth / 2.f - 12.f, 24.f}, .57f);
            enabled(choose, !m_pending && !Service::get().busy() && !room.started);
            enabled(start, !m_pending && !Service::get().busy() && !room.started && room.guest.has_value() &&
                room.hostReady && room.guestReady && room.level.id > 0 && m_cached);
        } else {
            label(frame, "Host chooses the map and rules", {centerX, 84.f}, .36f, centerWidth, kIce, false, "chatFont.fnt");
            label(frame, "Host starts", {width - cardWidth / 2.f - 12.f, 24.f}, .30f, cardWidth - 12.f, kMuted);
        }
        if (room.level.id > 0 && !m_cached) {
            auto* download = button(actions, this, menu_selector(RoomLayer::onDownload),
                m_downloading ? "Downloading..." : "Download", {centerX, 55.f}, .44f, "GJ_button_04.png");
            enabled(download, !m_downloading && !room.started);
        } else label(frame, room.level.id > 0 ? "Map and audio ready" : "No map selected",
            {centerX, 55.f}, .32f, centerWidth, m_cached ? ccc3(145, 255, 185) : kMuted, false, "chatFont.fnt");

        static char const* kinds[] = {"like", "smile", "angry", "fire"};
        for (int i = 0; i < 4; ++i) {
            auto* sprite = CCSprite::create();
            sprite->setContentSize({29.f, 29.f});
            auto* bg = NineSlice::create("square02b_001.png");
            bg->setContentSize({29.f, 29.f});
            bg->setColor(ccc3(5, 27, 73));
            bg->setPosition({14.5f, 14.5f});
            sprite->addChild(bg);
            auto* art = emoteArt(kinds[i]);
            art->setScale(.70f);
            art->setPosition({14.5f, 14.5f});
            sprite->addChild(art);
            auto* item = CCMenuItemSpriteExtra::create(sprite, this, menu_selector(RoomLayer::onEmote));
            item->setTag(i);
            item->setPosition({centerX + (i - 1.5f) * 32.f, 22.f});
            actions->addChild(item);
            enabled(item, room.guest.has_value() && !room.started);
        }
        std::string const status = m_pending ? "Updating room..." : room.battle && room.battle->finishedAt ? "Waiting for both players to return..." : room.started ? "Preparing both players..." :
            m_downloading ? "Downloading the selected map and audio..." : !m_cacheError.empty() ? m_cacheError + " Tap Download to retry." :
            !room.level.id ? "Host: choose a map and game rules" :
            !myReady ? "Press Ready after your map finishes downloading" :
            !opponent ? "Ready - waiting for an opponent" : !opponentReady ? "Ready - waiting for the other player" :
            host ? "Both players ready - press Start" : "Both players ready - waiting for the host";
        m_status->setString(status.c_str());
        m_status->setScale(.3f);
        if (m_status->getContentSize().width * .3f > m_window.width - 40.f)
            m_status->setScale((m_window.width - 40.f) / m_status->getContentSize().width);
    }
    void onRules(CCObject*) {
        auto const& room = Service::get().room();
        if (m_pending || Service::get().busy() || !room || !Service::get().isHost() || room->started) return;
        if (auto* popup = GameRulesPopup::create(*room)) popup->show();
    }
    void onDownload(CCObject*) {
        if (!m_downloading) {
            m_cacheError.clear();
            beginDownload();
            requestRender();
        }
    }
    void onEmote(CCObject* sender) {
        auto const& room = Service::get().room();
        if (m_sendingEmote || m_emoteCooldown > 0.f || !room || !room->guest || room->started) return;
        static char const* kinds[] = {"like", "smile", "angry", "fire"};
        int const tag = static_cast<CCNode*>(sender)->getTag();
        if (tag < 0 || tag >= 4) return;
        m_sendingEmote = true;
        m_emoteCooldown = 1.f;
        Service::get().sendEmote(kinds[tag], [self = WeakRef<RoomLayer>(this)](bool success, std::string detail) {
            auto owner = self.lock();
            if (!owner) return;
            owner->m_sendingEmote = false;
            if (!success) error(detail);
        });
    }
    void onChoose(CCObject*) {
        auto const& room = Service::get().room();
        if (m_pending || m_transitioning || Service::get().busy() || !room || !Service::get().isHost() || room->started) return;
        openLevelSearch([](LevelInfo level) {
            // The native search replaces this layer. Persist the selection through the service.
            Service::get().selectLevel(std::move(level), [](bool success, std::string detail) {
                if (!success) error(detail);
            });
        });
    }
    void onStart(CCObject*) {
        auto const& room = Service::get().room();
        if (m_pending || m_transitioning || Service::get().busy() || !room || !Service::get().isHost() || room->started ||
            !room->guest || !room->hostReady || !room->guestReady || !room->level.id) return;
        m_pending = true;
        requestRender();
        Service::get().startMatch([self = WeakRef<RoomLayer>(this)](bool success, std::string detail) {
            auto owner = self.lock();
            if (!owner) return;
            owner->m_pending = false;
            owner->requestRender();
            if (!success) error(detail);
        });
    }
    void onReady(CCObject*) {
        auto const& room = Service::get().room();
        if (m_pending || m_transitioning || !room || room->started ||
            (!Service::get().isHost() && !room->guest)) return;
        m_cached = mapCached(room->level.id);
        bool const host = Service::get().isHost();
        auto const control = readyControl(room->started, m_pending, host, room->level.id > 0,
            m_cached, m_downloading, host ? room->hostReady : room->guestReady);
        if (control.action == ReadyAction::None) return;
        if (control.action == ReadyAction::ChooseMap) { onChoose(nullptr); return; }
        if (control.action == ReadyAction::Download) {
            beginDownload();
            requestRender();
            return;
        }
        bool const ready = control.action == ReadyAction::Ready;
        m_pending = true;
        requestRender();
        Service::get().setReady(ready, [self = WeakRef<RoomLayer>(this)](bool success, std::string detail) {
            auto owner = self.lock();
            if (!owner) return;
            owner->m_pending = false;
            owner->requestRender();
            if (!success) error(detail);
        });
    }
    void goBack() override {
        if (m_pending || m_transitioning || Service::get().busy()) return;
        m_pending = true;
        enabled(m_back, false);
        m_status->setString("Leaving room...");
        Service::get().leaveRoom([self = WeakRef<RoomLayer>(this)](bool success, std::string detail) {
            auto owner = self.lock();
            if (!owner) return;
            owner->m_pending = false;
            if (success || !Service::get().room()) {
                owner->m_transitioning = true;
                showLobby();
                return;
            }
            enabled(owner->m_back, true);
            owner->requestRender();
            error(detail);
        });
    }
public:
    CREATE_FUNC(RoomLayer);
    void update(float dt) override {
        if (m_transitioning) return;
        m_readyPhase = std::fmod(m_readyPhase + dt * 2.f, 6.2831853f);
        animateReady();
        m_emoteCooldown = std::max(0.f, m_emoteCooldown - dt);
        m_elapsed += dt;
        if (m_elapsed < 1.f) return;
        m_elapsed = 0.f;
        auto const& room = Service::get().room();
        if (!room && !m_pending) {
            m_transitioning = true;
            std::string notice = Service::get().takeNotice();
            if (notice.empty()) notice = "The host left. This room has closed.";
            showLobby(std::move(notice));
            return;
        }
        if (room && room->battle && room->battle->finishedAt > 0 && m_returnedBattle != room->battle->id) {
            auto id = room->battle->id;
            Service::get().acknowledgeResult([self = WeakRef<RoomLayer>(this), id](bool ok, std::string) {
                if (auto owner = self.lock(); owner && ok) owner->m_returnedBattle = id;
            });
        }
        if (room && room->started && room->launch && room->battle && room->battle->finishedAt == 0 &&
            !m_pending && !isMatchLaunchActive()) {
            // Wait until our scene's fade has completed before replacing it.
            CCNode* scene = this;
            while (scene->getParent()) scene = scene->getParent();
            if (scene == CCDirector::sharedDirector()->getRunningScene()) {
                m_transitioning = true;
                launchMatch();
                return;
            }
        }
        if (room) {
            // Native downloads may finish outside our cache callback, or files
            // may be removed. Refresh availability before comparing the UI state.
            bool const cached = room->level.id > 0 && mapCached(room->level.id);
            if (cached != m_cached) {
                m_cached = cached;
                if (cached) { m_downloading = false; m_cacheError.clear(); preloadBattleAssets(); }
            }
            showRoomEmote(*room);
            if (signature(*room) != m_signature) requestRender();
        }
    }
};

} // namespace

CCScene* lobbyScene(std::string notice) {
    auto* scene = CCScene::create();
    if (auto* layer = LobbyLayer::create(std::move(notice))) scene->addChild(layer);
    return scene;
}

CCScene* roomScene() {
    auto* scene = CCScene::create();
    if (auto* layer = RoomLayer::create()) scene->addChild(layer);
    return scene;
}

void showLobby(std::string notice) {
    CCDirector::sharedDirector()->replaceScene(CCTransitionFade::create(.25f, lobbyScene(std::move(notice))));
}

void showRoom() {
    if (!Service::get().room()) {
        showLobby(Service::get().takeNotice());
        return;
    }
    CCDirector::sharedDirector()->replaceScene(CCTransitionFade::create(.25f, roomScene()));
}

} // namespace versus
