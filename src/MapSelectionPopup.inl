class MapSelectionPopup final : public Popup {
    MapSelection m_selection;
    std::string m_roomID;
    bool m_pending = false;
    CCMenuItemSpriteExtra* m_manual = nullptr;
    CCMenuItemSpriteExtra* m_random = nullptr;
    CCMenuItemSpriteExtra* m_type = nullptr;
    CCMenuItemSpriteExtra* m_apply = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_difficulties;
    void refresh() {
        auto tint = [](CCMenuItemSpriteExtra* item, bool selected) {
            auto* sprite = static_cast<ButtonSprite*>(item->getNormalImage());
            sprite->m_BGSprite->setColor(selected ? ccc3(100, 255, 125) : ccc3(155, 155, 155));
        };
        tint(m_manual, !m_selection.random); tint(m_random, m_selection.random);
        for (int i = 0; i < 10; ++i) {
            tint(m_difficulties[i], m_selection.difficulty == i);
            enabled(m_difficulties[i], m_selection.random && !m_pending);
        }
        auto* type = static_cast<ButtonSprite*>(m_type->getNormalImage());
        type->setString(m_selection.platformer ? "Platformer" : "Classic (Non-Platformer)");
        enabled(m_type, m_selection.random && !m_pending);
        enabled(m_apply, !m_pending); enabled(m_manual, !m_pending); enabled(m_random, !m_pending);
    }
    bool setup(RoomInfo const& room) {
        if (!Popup::init(420.f, 285.f, "GJ_square02.png")) return false;
        m_selection = room.mapSelection; m_roomID = room.id;
        setTitle("Map Mode", "bigFont.fnt", .65f);
        m_manual = button(m_buttonMenu, this, menu_selector(MapSelectionPopup::onManual), "Map Select", {115.f, 235.f}, .6f);
        m_random = button(m_buttonMenu, this, menu_selector(MapSelectionPopup::onRandom), "Random Map", {305.f, 235.f}, .6f);
        label(m_mainLayer, "Rated maps only - choose a difficulty", {210.f, 204.f}, .52f, 390.f, kIce, false, "chatFont.fnt");
        for (int i = 0; i < 10; ++i) {
            auto* item = button(m_buttonMenu, this, menu_selector(MapSelectionPopup::onDifficulty), RANDOM_DIFFICULTIES[i],
                {110.f + (i / 5) * 200.f, 177.f - (i % 5) * 25.f}, .46f);
            item->setTag(i); m_difficulties.push_back(item);
        }
        m_type = button(m_buttonMenu, this, menu_selector(MapSelectionPopup::onType), "Classic (Non-Platformer)", {210.f, 49.f}, .48f);
        m_apply = button(m_buttonMenu, this, menu_selector(MapSelectionPopup::onApply), "Apply", {210.f, 18.f}, .5f);
        refresh(); return true;
    }
    void onManual(CCObject*) { m_selection.random = false; refresh(); }
    void onRandom(CCObject*) { m_selection.random = true; refresh(); }
    void onDifficulty(CCObject* sender) { m_selection.difficulty = static_cast<CCNode*>(sender)->getTag(); refresh(); }
    void onType(CCObject*) { m_selection.platformer = !m_selection.platformer; refresh(); }
    void onApply(CCObject*) {
        auto const& room = Service::get().room();
        if (m_pending || !room || room->id != m_roomID) return;
        m_pending = true; refresh();
        Service::get().configureMapSelection(m_selection, [self = WeakRef<MapSelectionPopup>(this)](bool ok, std::string detail) {
            auto popup = self.lock(); if (!popup) return;
            popup->m_pending = false; popup->refresh();
            if (!ok) { error(detail); return; }
            bool manual = !popup->m_selection.random;
            popup->onClose(nullptr);
            if (manual) openLevelSearch([](LevelInfo level) {
                Service::get().selectLevel(std::move(level), [](bool ok, std::string detail) { if (!ok) error(detail); });
            });
        });
    }
public:
    static MapSelectionPopup* create(RoomInfo const& room) {
        auto* popup = new MapSelectionPopup;
        if (popup->setup(room)) { popup->autorelease(); return popup; }
        delete popup; return nullptr;
    }
};

class MapRoulettePopup final : public Popup {
    MapDraw m_draw;
    std::string m_roomID;
    std::vector<CCNode*> m_cards;
    CCLabelBMFont* m_caption = nullptr;
    int m_tick = -1;
    float m_soundCooldown = 0.f;
    bool m_revealed = false;
    bool setup(RoomInfo const& room) {
        if (!room.mapDraw || !Popup::init(440.f, 225.f, "GJ_square02.png")) return false;
        m_draw = *room.mapDraw; m_roomID = room.id;
        setTitle("Random Map", "bigFont.fnt", .65f);
        m_closeBtn->setVisible(false);
        auto* clip = CCClippingNode::create();
        auto* stencil = CCDrawNode::create();
        CCPoint vertices[] = {{0, 0}, {400, 0}, {400, 108}, {0, 108}};
        stencil->drawPolygon(vertices, 4, {1, 1, 1, 1}, 0, {0, 0, 0, 0});
        clip->setStencil(stencil); clip->setPosition({20.f, 65.f});
        m_mainLayer->addChild(clip);
        for (auto const& level : m_draw.levels) {
            auto* card = panel(clip, {}, {134.f, 100.f}, false);
            auto* face = GJDifficultySprite::create(std::clamp(level.difficulty, 0, 10), GJDifficultyName::Short);
            if (face) { face->setPosition({67.f, 70.f}); face->setScale(.7f); card->addChild(face); }
            label(card, level.name, {67.f, 38.f}, .42f, 124.f);
            label(card, fmt::format("{}", level.stars), {60.f, 15.f}, .4f, 34.f, ccc3(255, 220, 100));
            auto* star = CCSprite::createWithSpriteFrameName("star_small01_001.png");
            if (star) { star->setPosition({78.f, 15.f}); star->setScale(.6f); card->addChild(star); }
            m_cards.push_back(card);
        }
        label(m_mainLayer, "v", {220.f, 180.f}, .55f, 30.f, ccc3(255, 220, 100));
        m_caption = label(m_mainLayer, "Rolling rated maps...", {220.f, 42.f}, .5f, 400.f, kIce);
        label(m_mainLayer, "Map, music and SFX download after selection", {220.f, 20.f}, .42f, 400.f, kMuted, false, "chatFont.fnt");
        scheduleUpdate(); update(0); return true;
    }
    void onClose(CCObject*) override {} // synchronized selection cannot be skipped
public:
    void update(float dt) override {
        auto const& room = Service::get().room();
        if (!room || room->id != m_roomID || !room->mapDraw || room->mapDraw->id != m_draw.id) {
            Popup::onClose(nullptr); return;
        }
        int64_t elapsed = Service::get().serverNow() - m_draw.at;
        auto position = roulettePosition(static_cast<int>(m_cards.size()), m_draw.selected, elapsed);
        auto count = static_cast<double>(m_cards.size());
        for (size_t i = 0; i < m_cards.size(); ++i) {
            double offset = std::fmod(double(i) - position, count);
            if (offset < -count / 2.) offset += count;
            if (offset > count / 2.) offset -= count;
            auto* card = m_cards[i];
            card->setPosition({float(200. + offset * 142. - 67.), 4.f});
            card->setVisible(std::abs(offset) < 2.1);
        }
        m_soundCooldown = std::max(0.f, m_soundCooldown - dt);
        int tick = static_cast<int>(position);
        if (m_tick >= 0 && tick != m_tick && m_soundCooldown <= 0.f && !m_revealed) {
            audio::playBuiltin("counter003.ogg"); m_soundCooldown = .06f;
        }
        m_tick = tick;
        if (elapsed >= DRAW_LEAD_MS + DRAW_SPIN_MS && !m_revealed) {
            m_revealed = true;
            m_caption->setString(m_draw.levels[m_draw.selected].name.c_str());
            m_caption->limitLabelWidth(390.f, .6f, .25f);
            audio::playBuiltin("gold01.ogg");
        }
        if (room->mapDraw->settled && elapsed >= DRAW_END_MS) Popup::onClose(nullptr);
    }
    static MapRoulettePopup* create(RoomInfo const& room) {
        auto* popup = new MapRoulettePopup;
        if (popup->setup(room)) { popup->autorelease(); return popup; }
        delete popup; return nullptr;
    }
};
