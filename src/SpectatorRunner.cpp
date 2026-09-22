#include "SpectatorRunner.hpp"
#include "RunnerPhysics.hpp"
#include <Geode/binding/SimplePlayer.hpp>
#include <Geode/binding/GameManager.hpp>
#include <cmath>
#include <random>
using namespace geode::prelude;
namespace versus {
namespace {
CCDrawNode* batMonster() {
    auto* bat = CCDrawNode::create();
    ccColor4F black = {0.f, 0.f, 0.f, 1.f};
    CCPoint leftWing[] = {{-4.f, 2.f}, {-11.f, 9.f}, {-22.f, 6.f},
        {-19.f, 0.f}, {-27.f, -5.f}, {-17.f, -6.f}, {-12.f, -3.f}, {-4.f, -5.f}};
    CCPoint rightWing[] = {{4.f, 2.f}, {11.f, 9.f}, {22.f, 6.f},
        {19.f, 0.f}, {27.f, -5.f}, {17.f, -6.f}, {12.f, -3.f}, {4.f, -5.f}};
    bat->drawPolygon(leftWing, 8, black, 0.f, black);
    bat->drawPolygon(rightWing, 8, black, 0.f, black);
    bat->drawDot({0.f, 0.f}, 8.f, black);
    CCPoint leftEar[] = {{-6.f, 4.f}, {-6.f, 12.f}, {-1.f, 7.f}};
    CCPoint rightEar[] = {{6.f, 4.f}, {6.f, 12.f}, {1.f, 7.f}};
    bat->drawPolygon(leftEar, 3, black, 0.f, black);
    bat->drawPolygon(rightEar, 3, black, 0.f, black);
    bat->drawDot({-3.f, 1.f}, 2.f, {1.f, 1.f, 1.f, 1.f});
    bat->drawDot({3.f, 1.f}, 2.f, {1.f, 1.f, 1.f, 1.f});
    bat->drawDot({-2.5f, 1.f}, .8f, black);
    bat->drawDot({3.5f, 1.f}, .8f, black);
    return bat;
}
}
struct SpectatorRunner::Impl {
    struct Strip { std::vector<CCSprite*> tiles; float width, offset = 0.f, factor; };
    struct Obstacle { CCNode* node; float x, y, halfWidth, halfHeight; bool flying; };
    std::vector<Strip> strips;
    std::vector<Obstacle> obstacles;
    CCNode* field = nullptr;
    SimplePlayer* avatar = nullptr;
    CCLabelBMFont *percent = nullptr, *stats = nullptr, *scoreLabel = nullptr, *hint = nullptr;
    GameRules rules;
    RunnerJump jump;
    float width = 0.f, distance = 0.f, spawn = 1.4f, time = 0.f, deathTime = 0.f;
    bool dead = false;
    int best = 0, displayedScore = -1;
    std::string displayedStats, displayedPercent;
    std::mt19937 random{std::random_device{}()};
    void press() {
        if (dead) {
            if (deathTime < .5f) return;
            for (auto& obstacle : obstacles) obstacle.node->removeFromParent();
            obstacles.clear(); jump = {}; distance = 0.f; spawn = 1.4f; dead = false;
            avatar->setOpacity(255); hint->setString("Tap / Space to jump - hold for a longer jump");
        }
        jump.press();
    }
};
SpectatorRunner::SpectatorRunner() : m(std::make_unique<Impl>()) {}
SpectatorRunner::~SpectatorRunner() = default;
void preloadRunnerAssets() {
    static bool loaded = false; if (loaded) return; loaded = true;
    CCTextureCache::sharedTextureCache()->addImage("game_bg_01_001.png", false);
    CCTextureCache::sharedTextureCache()->addImage("groundSquare_01_001.png", false);
    CCSprite::createWithSpriteFrameName("spike_01_001.png");
}
bool SpectatorRunner::init(PlayerProfile const& profile, GameRules const& rules) {
    if (!CCLayer::init()) return false;
    setID("spectator-runner"_spr); m->rules = rules;
    auto window = CCDirector::sharedDirector()->getWinSize();
    float width = std::min(510.f, window.width - 20.f), height = std::min(275.f, window.height - 20.f);
    auto origin = CCPoint{(window.width-width)/2.f, (window.height-height)/2.f};
    auto* shade = CCLayerColor::create(ccc4(3, 13, 35, 205)); addChild(shade);
    auto* panel = CCScale9Sprite::create("square02b_001.png"); panel->setColor(ccc3(10, 45, 92));
    panel->setContentSize({width,height}); panel->setPosition(origin+CCPoint{width/2.f,height/2.f}); addChild(panel);
    auto text = [this](std::string value, CCPoint p, float scale, float maxWidth) {
        auto* label = CCLabelBMFont::create(value.c_str(), "chatFont.fnt");
        label->setPosition(p); label->setScale(std::min(scale, maxWidth/std::max(1.f,label->getContentSize().width)));
        addChild(label,5); return label;
    };
    text("SPECTATOR RUNNER  /  " + profile.name, origin+CCPoint{width/2.f,height-15.f}, .75f, width-20.f);
    m->stats=text("",origin+CCPoint{width/2.f,height-34.f},.65f,width-20.f);
    m->scoreLabel=text("Runner 0  /  Best 0",origin+CCPoint{width-15.f,51.f},.5f,width-25.f);
    m->scoreLabel->setAnchorPoint({1.f,.5f});
    m->hint=text("Tap / Space to jump - hold for a longer jump",origin+CCPoint{width/2.f,30.f},.55f,width-20.f);
    text("Waiting for the player to finish...",origin+CCPoint{width/2.f,13.f},.6f,width-20.f)->setColor(ccc3(146,220,255));
    float fieldHeight=height-91.f; m->width=width-20.f;
    auto* stencil=CCDrawNode::create(); CCPoint corners[]={{0,0},{m->width,0},{m->width,fieldHeight},{0,fieldHeight}};
    stencil->drawPolygon(corners,4,{1,1,1,1},0,{1,1,1,1});
    auto* clip=CCClippingNode::create(stencil);clip->setPosition(origin+CCPoint{10.f,46.f});addChild(clip,1);m->field=clip;
    auto strip=[&](char const* file,float h,ccColor3B color,float factor) {
        Impl::Strip result; result.factor=factor;
        auto* texture=CCTextureCache::sharedTextureCache()->addImage(file,false);
        if(!texture) return;
        result.width=texture->getContentSize().width*h/texture->getContentSize().height;
        for(int i=0;i<static_cast<int>(std::ceil(m->width/result.width))+2;++i){
            auto* tile=CCSprite::createWithTexture(texture);tile->setAnchorPoint({0,0});tile->setScale(h/tile->getContentSize().height);
            tile->setColor(color);clip->addChild(tile);result.tiles.push_back(tile);
        }
        m->strips.push_back(std::move(result));
    };
    strip("game_bg_01_001.png",fieldHeight,ccc3(32,104,190),.13f);
    strip("groundSquare_01_001.png",20.f,ccc3(75,190,245),1.f);
    m->avatar=SimplePlayer::create(std::clamp(profile.icon,1,std::max(1,GameManager::sharedState()->countForType(IconType::Cube))));
    m->avatar->setColors(ccc3((profile.color1>>16)&255,(profile.color1>>8)&255,profile.color1&255),ccc3((profile.color2>>16)&255,(profile.color2>>8)&255,profile.color2&255));
    m->avatar->setScale(.9f);clip->addChild(m->avatar,3);
    m->percent=CCLabelBMFont::create("0%","bigFont.fnt");m->percent->setScale(.38f);clip->addChild(m->percent,4);
    setTouchEnabled(true);setKeyboardEnabled(true);scheduleUpdate();update(0.f);
    return true;
}
SpectatorRunner* SpectatorRunner::create(PlayerProfile const& profile, GameRules const& rules) {
    auto* result=new SpectatorRunner;
    if(result->init(profile,rules)){result->autorelease();return result;} delete result;return nullptr;
}
void SpectatorRunner::setOpponentState(BattlePlayerState const& state) {
    auto percent = fmt::format("{}%",state.currentPercent);
    if (percent != m->displayedPercent) { m->displayedPercent = percent; m->percent->setString(percent.c_str()); }
    std::string attempts=m->rules.mode==0&&!m->rules.practice?fmt::format("{} left",std::max(0,m->rules.attempts-state.attemptsUsed)):fmt::format("Attempt {}",std::max(1,state.runNumber));
    auto stats = fmt::format("{}  /  Best {}%{}",attempts,state.bestPercent,state.paused?"  /  PAUSED":"");
    if (stats != m->displayedStats) { m->displayedStats = stats; m->stats->setString(stats.c_str()); }
}
void SpectatorRunner::registerWithTouchDispatcher(){CCDirector::sharedDirector()->getTouchDispatcher()->addTargetedDelegate(this,-100,true);}
bool SpectatorRunner::ccTouchBegan(CCTouch* touch,CCEvent*) { if(!isVisible() || touch->getLocation().y > CCDirector::sharedDirector()->getWinSize().height - 60.f)return false;m->press();return true; }
void SpectatorRunner::ccTouchEnded(CCTouch*,CCEvent*) {m->jump.release();}
void SpectatorRunner::ccTouchCancelled(CCTouch*,CCEvent*) {m->jump.release();}
void SpectatorRunner::keyDown(enumKeyCodes key,double) {if(isVisible()&&(key==KEY_Space||key==KEY_Up||key==KEY_W))m->press();}
void SpectatorRunner::keyUp(enumKeyCodes key,double) {if(key==KEY_Space||key==KEY_Up||key==KEY_W)m->jump.release();}
void SpectatorRunner::update(float dt) {
    if(!isVisible()){m->jump.release();return;}
    dt=std::clamp(dt,0.f,.05f);m->time+=dt;
    if(m->dead)m->deathTime+=dt;
    float speed=std::min(220.f,125.f+m->distance*.02f);
    if(!m->dead){
        for(float remaining=dt;remaining>0.f;){float step=std::min(remaining,1.f/120.f);m->jump.step(step);remaining-=step;}
        m->distance+=dt*speed;m->spawn-=dt;
        if(m->spawn<=0.f&&m->obstacles.size()<5){
            bool flying=m->distance>350.f&&m->random()%3==0;
            CCNode* node=flying?static_cast<CCNode*>(batMonster()):static_cast<CCNode*>(CCSprite::createWithSpriteFrameName("spike_01_001.png"));
            if(node){node->setScale(flying?.92f:.75f);m->field->addChild(node,2);m->obstacles.push_back({node,m->width+30.f,flying?(m->random()%2?66.f:94.f):29.f,flying?20.f:8.f,flying?9.f:10.f,flying});}
            m->spawn=1.05f+(m->random()%60)/100.f;
        }
        for(auto& obstacle:m->obstacles){
            obstacle.x-=dt*speed;
            float y=obstacle.y+(obstacle.flying?std::sin(m->time*6.f)*3.f:0.f);
            obstacle.node->setPosition({obstacle.x,y});
            if (obstacle.flying) obstacle.node->setScaleY(.77f + .2f * std::sin(m->time * 14.f + obstacle.x * .03f));
            if(std::abs(obstacle.x-55.f)<obstacle.halfWidth+7.f&&std::abs(y-(29.f+m->jump.height))<obstacle.halfHeight+7.f){
                m->dead=true;m->deathTime=0.f;m->jump.release();m->avatar->setOpacity(140);
                m->best=std::max(m->best,static_cast<int>(m->distance/10.f));m->displayedScore=-1;m->hint->setString("Crashed! Tap / Space to retry");
            }
        }
        std::erase_if(m->obstacles,[](auto const& obstacle){if(obstacle.x>-30.f)return false;obstacle.node->removeFromParent();return true;});
        for(auto& strip:m->strips){strip.offset=std::fmod(strip.offset+dt*speed*strip.factor,strip.width);for(size_t i=0;i<strip.tiles.size();++i)strip.tiles[i]->setPosition({i*strip.width-strip.offset,0.f});}
    }
    m->avatar->setPosition({55.f,29.f+m->jump.height});m->percent->setPosition({55.f,49.f+m->jump.height});
    int score = static_cast<int>(m->distance/10.f);
    if (score != m->displayedScore) { m->displayedScore = score; m->scoreLabel->setString(fmt::format("Runner {}  /  Best {}",score,m->best).c_str()); }
}
}
