class MapSelectionPopup final : public Popup {
    MapSelection m_selection;
    std::string m_roomID;
    bool m_pending = false;
    CCMenuItemSpriteExtra* m_manual = nullptr;
    CCMenuItemSpriteExtra* m_random = nullptr;
    CCMenuItemSpriteExtra* m_type = nullptr;
    CCMenuItemSpriteExtra* m_apply = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_difficulties;
    std::vector<CCSprite*> m_checks;
    std::vector<CCLabelBMFont*> m_names;
    struct StarChoice { int category, bit; CCMenuItemSpriteExtra* item; CCSprite* check; };
    std::vector<StarChoice> m_stars;
    CCLabelBMFont* m_hint = nullptr;
    CCLabelBMFont* m_manualHint = nullptr;
    void refresh() {
        auto tint = [](CCMenuItemSpriteExtra* item, bool selected) {
            auto* sprite = static_cast<ButtonSprite*>(item->getNormalImage());
            sprite->m_BGSprite->setColor(selected ? ccc3(100, 255, 125) : ccc3(155, 155, 155));
        };
        tint(m_manual, !m_selection.random); tint(m_random, m_selection.random);
        for (int i = 0; i < 10; ++i) {
            bool selected = (m_selection.mask & CATEGORY_MASKS[i]) != 0;
            auto* face = static_cast<CCSprite*>(m_difficulties[i]->getNormalImage());
            face->setColor(selected ? ccWHITE : ccc3(125, 125, 125));
            m_checks[i]->setVisible(selected);
            m_names[i]->setColor(selected ? ccc3(110, 255, 140) : kMuted);
            m_difficulties[i]->setVisible(m_selection.random);
            m_names[i]->setVisible(m_selection.random);
            enabled(m_difficulties[i], m_selection.random && !m_pending);
        }
        for (auto const& choice : m_stars) {
            bool visible = m_selection.random && (m_selection.mask & CATEGORY_MASKS[choice.category]);
            choice.item->setVisible(visible);
            enabled(choice.item, visible && !m_pending);
            choice.check->setDisplayFrame(CCSpriteFrameCache::sharedSpriteFrameCache()->spriteFrameByName(
                (m_selection.mask & choice.bit) ? "GJ_checkOn_001.png" : "GJ_checkOff_001.png"));
        }
        auto* type = static_cast<ButtonSprite*>(m_type->getNormalImage());
        type->setString(m_selection.platformer ? "Platformer" : "Classic");
        m_type->updateSprite(); m_type->setVisible(m_selection.random);
        auto* applySprite = ButtonSprite::create(m_selection.random ? "Apply" : "Search Map ..", "goldFont.fnt", "GJ_button_01.png");
        applySprite->setScale(.57f); m_apply->setSprite(applySprite);
        m_apply->setPosition({m_selection.random ? 354.f : 240.f, 29.f});
        m_hint->setVisible(m_selection.random); m_manualHint->setVisible(!m_selection.random);
        enabled(m_type, m_selection.random && !m_pending);
        enabled(m_apply, !m_pending && validSelection(m_selection));
        enabled(m_manual, !m_pending); enabled(m_random, !m_pending);
    }
    bool setup(RoomInfo const& room) {
        if (!Popup::init(480.f, 310.f, "GJ_square02.png")) return false;
        m_selection = room.mapSelection; m_roomID = room.id;
        setTitle("Map Mode", "bigFont.fnt", .65f);
        m_manual = button(m_buttonMenu, this, menu_selector(MapSelectionPopup::onManual), "Map Select", {132.f, 258.f}, .6f);
        m_random = button(m_buttonMenu, this, menu_selector(MapSelectionPopup::onRandom), "Random Map", {348.f, 258.f}, .6f);
        m_hint = label(m_mainLayer, "Rated only | Multi-select faces, then check stars", {240.f, 232.f}, .55f, 444.f, kIce, false, "chatFont.fnt");
        m_manualHint = label(m_mainLayer, "Choose a map using the GD search", {240.f, 159.f}, .6f, 420.f, kIce, false, "chatFont.fnt");
        for (int i = 0; i < 10; ++i) {
            float const x = 54.f + (i % 5) * 93.f;
            float const y = i < 5 ? 201.f : 119.f;
            auto* face = GJDifficultySprite::create(CATEGORY_FACES[i], GJDifficultyName::Short);
            face->setScale(.88f);
            auto* item = CCMenuItemSpriteExtra::create(face, this, menu_selector(MapSelectionPopup::onDifficulty));
            item->m_scaleMultiplier = 1.1f; item->setPosition({x, y}); m_buttonMenu->addChild(item);
            item->setTag(i); m_difficulties.push_back(item);
            auto* check = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
            check->setScale(.36f); check->setPosition({face->getContentSize().width - 1.f, face->getContentSize().height - 2.f});
            face->addChild(check); m_checks.push_back(check);
            std::string name = RANDOM_DIFFICULTIES[i];
            if (i >= 5) name.replace(name.find(' '), 1, "\n");
            m_names.push_back(label(m_mainLayer, name, {x, y - 29.f}, .29f, 86.f));
            int bits = CATEGORY_MASKS[i];
            int count = std::popcount(static_cast<unsigned>(bits)), column = 0;
            for (int bitIndex = 0; bitIndex < 13; ++bitIndex) if (bits & (1 << bitIndex)) {
                float const start = x - (count - 1) * 22.f + column++ * 44.f;
                float const starY = i < 5 ? 148.f : 64.f;
                // One hit target includes the checkbox, number and native star.
                auto* optionSprite = CCSprite::create(); optionSprite->setContentSize({42.f, 24.f});
                auto* box = CCSprite::createWithSpriteFrameName("GJ_checkOff_001.png"); box->setScale(.43f);
                box->setPosition({8.f, 12.f}); optionSprite->addChild(box);
                starRating(optionSprite, std::to_string(bitIndex < 8 ? bitIndex + 2 : 10), {29.f, 12.f}, .30f, 22.f);
                auto* option = CCMenuItemSpriteExtra::create(optionSprite, this, menu_selector(MapSelectionPopup::onStar));
                option->setPosition({start, starY}); option->setTag(1 << bitIndex); option->m_scaleMultiplier = 1.1f;
                m_buttonMenu->addChild(option);
                m_stars.push_back({i, 1 << bitIndex, option, box});
            }
        }
        m_type = button(m_buttonMenu, this, menu_selector(MapSelectionPopup::onType), "Classic", {126.f, 29.f}, .52f);
        m_apply = button(m_buttonMenu, this, menu_selector(MapSelectionPopup::onApply), "Apply", {354.f, 29.f}, .57f);
        refresh(); return true;
    }
    void onManual(CCObject*) { m_selection.random = false; refresh(); }
    void onRandom(CCObject*) { m_selection.random = true; refresh(); }
    void onDifficulty(CCObject* sender) {
        int bits = CATEGORY_MASKS[sender->getTag()];
        if (m_selection.mask & bits) m_selection.mask &= ~bits;
        else m_selection.mask |= bits;
        refresh();
    }
    void onStar(CCObject* sender) { m_selection.mask ^= sender->getTag(); refresh(); }
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
            if (face) { face->setPosition({67.f, 76.f}); face->setScale(.65f); card->addChild(face); }
            label(card, level.name, {67.f, 47.f}, .39f, 124.f);
            label(card, level.creator.empty() ? "Unknown creator" : level.creator, {67.f, 29.f}, .36f, 122.f, ccWHITE, false, "goldFont.fnt");
            label(card, fmt::format("{}", level.stars), {60.f, 11.f}, .36f, 34.f, ccc3(255, 220, 100));
            auto* star = CCSprite::createWithSpriteFrameName("star_small01_001.png");
            if (star) { star->setPosition({78.f, 11.f}); star->setScale(.6f); card->addChild(star); }
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
