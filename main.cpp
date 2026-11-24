#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/EffectGameObject.hpp>
#include <Geode/modify/HardStreak.hpp>
#include <Geode/binding/RingObject.hpp>
#include <Windows.h>
#include <cstdio>
#include <vector>

using namespace geode::prelude;

class TrajectoryBufferSim {
private:
    PlayerObject* m_simPlayer1 = nullptr;
    PlayerObject* m_simPlayer2 = nullptr;

    bool m_simulating = false;
    bool m_firingBuffered = false;
    bool m_simulationDead = false;
    bool m_simInputHolding = false;

    bool m_physicalHolding = false;
    bool m_gameHolding = false;

    int  m_presetIndex = 0;
    bool m_enabled = true;
    int  m_maxTicksNormal = 2;
    int  m_maxTicksWave = 2;

    int  m_waitTicks = -1;
    bool m_bufferedPush = true;
    bool m_queuedOpposite = false;
    float m_frameDt = 0.25f;

    std::vector<GameObject*> m_collisionScratch;

    PlayerObject* createTrajectoryPlayer() {
        PlayLayer* pl = PlayLayer::get();
        if (!pl || !pl->m_objectLayer) return nullptr;

        PlayerObject* player = PlayerObject::create(1, 1, pl, pl, true);
        if (!player) return nullptr;
        player->retain();
        player->setPosition({0, 105});
        player->setVisible(false);
        player->unscheduleUpdate();
        pl->m_objectLayer->addChild(player);
        return player;
    }

    void resetCollisionLog(PlayerObject* self) {
        self->m_collisionLogTop->removeAllObjects();
        self->m_collisionLogBottom->removeAllObjects();
        self->m_collisionLogLeft->removeAllObjects();
        self->m_collisionLogRight->removeAllObjects();
    }

    void syncSimPlayer(PlayerObject* sim, PlayerObject* real) {
        sim->setVisible(false);
        sim->copyAttributes(real);
        sim->setPosition(real->getPosition());
        sim->setScale(real->getScale());
        sim->setContentSize(real->getContentSize());

        sim->m_width          = real->m_width;
        sim->m_height         = real->m_height;
        sim->m_gravityMod     = real->m_gravityMod;
        sim->m_isOnGround     = real->m_isOnGround;
        sim->m_isUpsideDown   = real->m_isUpsideDown;
        sim->m_vehicleSize    = real->m_vehicleSize;
        sim->m_playerSpeed    = real->m_playerSpeed;
        sim->m_yVelocity      = real->m_yVelocity;
        sim->m_gravity        = real->m_gravity;
        sim->m_speedMultiplier = real->m_speedMultiplier;

        sim->m_isShip    = real->m_isShip;
        sim->m_isBall    = real->m_isBall;
        sim->m_isBird    = real->m_isBird;
        sim->m_isDart    = real->m_isDart;
        sim->m_isRobot   = real->m_isRobot;
        sim->m_isSpider  = real->m_isSpider;
        sim->m_isSwing   = real->m_isSwing;

        sim->m_isDead = false;
        sim->m_touchedRings.clear();
    }

    bool isCollisionRelevant(GameObject* obj) const {
        if (!obj) return false;
        switch (obj->m_objectType) {
            case GameObjectType::Solid:
            case GameObjectType::Hazard:
            case GameObjectType::AnimatedHazard:
            case GameObjectType::CollisionObject:
            case GameObjectType::Slope:
                return true;
            default:
                return false;
        }
    }

    float getValidDt() const {
        if (m_frameDt > 0.001f && m_frameDt < 2.0f) return m_frameDt;
        return 0.25f;
    }

public:
    void init() {
        quit();
        PlayLayer* pl = PlayLayer::get();
        if (!pl) return;

        m_simPlayer1 = createTrajectoryPlayer();
        m_simPlayer2 = createTrajectoryPlayer();
    }

    void quit() {
        if (m_simPlayer1) {
            m_simPlayer1->removeFromParentAndCleanup(true);
            m_simPlayer1->release();
            m_simPlayer1 = nullptr;
        }
        if (m_simPlayer2) {
            m_simPlayer2->removeFromParentAndCleanup(true);
            m_simPlayer2->release();
            m_simPlayer2 = nullptr;
        }
        m_simulating       = false;
        m_firingBuffered   = false;
        m_waitTicks        = -1;
        m_queuedOpposite   = false;
        m_physicalHolding  = false;
        m_gameHolding      = false;
    }

    void cyclePreset() {
        m_presetIndex = (m_presetIndex + 1) % 7;
        m_waitTicks = -1;
        m_queuedOpposite = false;
        
        switch (m_presetIndex) {
            case 0: m_enabled = true;  m_maxTicksNormal = 2;  m_maxTicksWave = 2;  break;
            case 1: m_enabled = true;  m_maxTicksNormal = 4;  m_maxTicksWave = 4;  break;
            case 2: m_enabled = true;  m_maxTicksNormal = 8;  m_maxTicksWave = 8;  break;
            case 3: m_enabled = true;  m_maxTicksNormal = 16; m_maxTicksWave = 8;  break;
            case 4: m_enabled = true;  m_maxTicksNormal = 24; m_maxTicksWave = 8;  break;
            case 5: m_enabled = true;  m_maxTicksNormal = 32; m_maxTicksWave = 8;  break;
            case 6: m_enabled = false; m_maxTicksNormal = 0;  m_maxTicksWave = 0;  break;
        }
        
        printf("[InputBuffer] F6 Cycle -> Preset %d | Normal: %d | Wave: %d\n", 
            m_presetIndex + 1, m_maxTicksNormal, m_maxTicksWave);
    }

    bool isSimulating() const { return m_simulating; }
    bool isFiringBuffered() const { return m_firingBuffered; }
    bool isSimulationPlayer(PlayerObject* player) const {
        return player && (player == m_simPlayer1 || player == m_simPlayer2);
    }
    bool canSimulateRingActivation(PlayerObject* player, EffectGameObject* object) const {
        return m_simulating && m_simInputHolding && isSimulationPlayer(player) &&
               geode::cast::typeinfo_cast<RingObject*>(object);
    }

    bool checkSimulationDeath(PlayerObject* player) {
        if (m_simulating && (player == m_simPlayer1 || player == m_simPlayer2)) {
            m_simulationDead = true;
            return true;
        }
        if (!m_simulating) {
            m_waitTicks = -1;
            m_queuedOpposite = false;
            m_gameHolding = false;
        }
        return false;
    }

    void setFrameDelta(float dt) {
        m_frameDt = dt;
    }

    int getSurvivalTicks(PlayLayer* pl, PlayerObject* realP1, PlayerObject* realP2,
                         bool isDual, int delayFrames, bool actionPush,
                         int maxTicks, bool returnTotalTicks = false) {
        if (!pl || !m_simPlayer1 || !realP1) return maxTicks;

        m_simulating = true;
        m_simulationDead = false;

        syncSimPlayer(m_simPlayer1, realP1);
        if (isDual && m_simPlayer2 && realP2) {
            syncSimPlayer(m_simPlayer2, realP2);
        }

        bool preInputHolding = m_gameHolding;
        m_simInputHolding = preInputHolding;
        if (preInputHolding) {
            m_simPlayer1->pushButton(PlayerButton::Jump);
            if (isDual && m_simPlayer2) m_simPlayer2->pushButton(PlayerButton::Jump);
        } else {
            m_simPlayer1->releaseButton(PlayerButton::Jump);
            if (isDual && m_simPlayer2) m_simPlayer2->releaseButton(PlayerButton::Jump);
        }

        float dt = getValidDt();
        int survived = 0;
        int elapsedTicks = 0;
        bool reachedInput = false;
        bool isContinuousMode = realP1->m_isDart || realP1->m_isBird || realP1->m_isSwing ||
                                (isDual && realP2 && (realP2->m_isDart || realP2->m_isBird ||
                                                     realP2->m_isSwing));
        int airborneTicks = 0;
        int safeLandedTicks = 0;

        for (int i = 0; i < delayFrames + maxTicks; i++) {
            if (i == delayFrames) {
                reachedInput = true;
                m_simInputHolding = actionPush;
                if (actionPush) {
                    m_simPlayer1->pushButton(PlayerButton::Jump);
                    if (isDual && m_simPlayer2) m_simPlayer2->pushButton(PlayerButton::Jump);
                } else {
                    m_simPlayer1->releaseButton(PlayerButton::Jump);
                    if (isDual && m_simPlayer2) m_simPlayer2->releaseButton(PlayerButton::Jump);
                }
            }

            m_simPlayer1->update(dt);
            m_simPlayer1->updateRotation(dt);
            if (isDual && m_simPlayer2) {
                m_simPlayer2->update(dt);
                m_simPlayer2->updateRotation(dt);
            }
            if (m_simulationDead) break;

            resetCollisionLog(m_simPlayer1);
            pl->checkCollisions(m_simPlayer1, dt, false);
            if (m_simulationDead) break;

            if (isDual && m_simPlayer2 && realP2) {
                resetCollisionLog(m_simPlayer2);
                pl->checkCollisions(m_simPlayer2, dt, false);
                if (m_simulationDead) break;
            }

            elapsedTicks++;
            if (i >= delayFrames) {
                survived++;
                if (!isContinuousMode) {
                    bool p1Grounded = m_simPlayer1->m_isOnGround;
                    bool p2Grounded = (!isDual || !m_simPlayer2) ? true : m_simPlayer2->m_isOnGround;
                    if (!p1Grounded || !p2Grounded) {
                        airborneTicks++;
                        safeLandedTicks = 0;
                    } else if (airborneTicks >= 15) {
                        safeLandedTicks++;
                        if (safeLandedTicks >= 10) {
                            survived = maxTicks;
                            break;
                        }
                    }
                }
            }
        }

        m_simulating = false;
        if (returnTotalTicks) return reachedInput ? elapsedTicks : 0;
        return survived;
    }

    bool interceptInput(GJBaseGameLayer* layer, bool push, bool isPlayer1) {
        PlayLayer* pl = PlayLayer::get();
        if (!pl) return true;

        bool isTwoPlayerControl = pl->m_level && pl->m_level->m_twoPlayerMode;
        bool isDual = pl->m_gameState.m_isDualMode && pl->m_player2 != nullptr && !isTwoPlayerControl;

        if (!isPlayer1) {
            if (isDual && !isTwoPlayerControl && (m_waitTicks >= 0 || m_waitTicks == -2)) {
                return false;
            }
            return true;
        }

        m_physicalHolding = push;

        if (!m_enabled) {
            m_gameHolding = push;
            return true;
        }

        PlayerObject* p1 = layer->m_player1;
        PlayerObject* p2 = isDual ? pl->m_player2 : nullptr;
        if (!p1 || p1->m_isDead) {
            m_waitTicks = -1;
            m_queuedOpposite = false;
            m_gameHolding = push;
            return true;
        }

        if (p1->m_isShip || (isDual && p2 && p2->m_isShip)) {
            m_waitTicks = -1;
            m_queuedOpposite = false;
            m_gameHolding = push;
            return true;
        }

        if (m_waitTicks >= 0) {
            m_queuedOpposite = (push != m_bufferedPush);
            return false;
        }

        if (m_waitTicks == -2) {
            m_waitTicks = -1;
            m_queuedOpposite = false;
            if (m_gameHolding != !m_bufferedPush) {
                m_SimFireButton(pl, !m_bufferedPush);
            }
        }

        bool isWave = p1->m_isDart || (isDual && p2 && p2->m_isDart);
        bool isMiniWave = (p1->m_isDart && p1->m_vehicleSize < 1.0f) ||
                          (isDual && p2 && p2->m_isDart && p2->m_vehicleSize < 1.0f);

        bool releaseAffectsPhysics = p1->m_isDart || p1->m_isRobot ||
                                     p1->m_isBird || p1->m_isSwing ||
                                     (isDual && p2 && (p2->m_isDart || p2->m_isRobot ||
                                                       p2->m_isBird || p2->m_isSwing));
        if (!push && !releaseAffectsPhysics) {
            m_gameHolding = false;
            return true;
        }

        m_queuedOpposite = false;
        const char* act = push ? "PRESS" : "RELEASE";

        int targetHorizon = isMiniWave ? 50 : (isWave ? 65 : 160);
        int maxDelayFrames = isWave ? m_maxTicksWave : m_maxTicksNormal;

        int ticksFrame0 = getSurvivalTicks(pl, p1, p2, isDual, 0, push, targetHorizon, isWave);
        
        if (ticksFrame0 >= targetHorizon) {
            m_gameHolding = push;
            return true;
        }

        int bestDelay = 0;
        int bestTicks = ticksFrame0;
        int minimumUsefulGain = 2;

        for (int delay = 1; delay <= maxDelayFrames; delay++) {
            int ticks = getSurvivalTicks(pl, p1, p2, isDual, delay, push, targetHorizon, isWave);

            if (isWave) {
                if (ticks <= delay) continue;
                int postInputTicks = ticks - delay;
                if (ticks >= delay + targetHorizon &&
                    postInputTicks >= ticksFrame0 + minimumUsefulGain) {
                    bestDelay = delay;
                    bestTicks = postInputTicks;
                    break;
                }
                if (postInputTicks > bestTicks) {
                    bestDelay = delay;
                    bestTicks = postInputTicks;
                }
            } else {
                if (ticks > bestTicks) {
                    bestTicks = ticks;
                    bestDelay = delay;
                }
                if (ticks >= targetHorizon && ticks >= ticksFrame0 + minimumUsefulGain) {
                    bestDelay = delay;
                    bestTicks = ticks;
                    break;
                }
            }
        }

        if (bestDelay > 0 && bestTicks >= ticksFrame0 + minimumUsefulGain) {
            printf("[InputBuffer] *** SAVED +%d TICKS %s! (Frame0: %d -> Delayed: %d) [Limit: %d] ***\n",
                bestDelay, act, ticksFrame0, bestTicks, maxDelayFrames);

            m_bufferedPush = push;
            m_waitTicks = bestDelay; 
            return false;
        }

        m_gameHolding = push;
        return true;
    }

    void onPlayerUpdate(PlayerObject* player) {
        PlayLayer* pl = PlayLayer::get();
        if (!pl || player != pl->m_player1) return;
        
        if (GetAsyncKeyState(VK_F6) & 1) {
            cyclePreset();
        }

        if (!m_enabled) return;

        if (player->m_isShip ||
            (pl->m_gameState.m_isDualMode && pl->m_player2 && pl->m_player2->m_isShip)) {
            m_waitTicks = -1;
            m_queuedOpposite = false;
            if (m_gameHolding != m_physicalHolding) {
                m_SimFireButton(pl, m_physicalHolding);
            }
            return;
        }

        if (m_waitTicks == 0) {
            m_SimFireButton(pl, m_bufferedPush);
            if (m_queuedOpposite) {
                m_waitTicks = -2;
                m_queuedOpposite = false;
            } else {
                m_waitTicks = -1;
            }
        } else if (m_waitTicks > 0) {
            m_waitTicks--;
        } else if (m_waitTicks == -2) {
            m_SimFireButton(pl, !m_bufferedPush);
            m_queuedOpposite = false;
            m_waitTicks = -1;
        } else if (m_waitTicks == -1) {
            if (m_gameHolding != m_physicalHolding) {
                m_SimFireButton(pl, m_physicalHolding);
            }
        }
    }

    void m_SimFireButton(PlayLayer* pl, bool push) {
        m_firingBuffered = true;
        m_gameHolding = push;
        pl->handleButton(push, 1, true);
        if (pl->m_gameState.m_isDualMode && (!pl->m_level || !pl->m_level->m_twoPlayerMode)) {
            pl->handleButton(push, 1, false);
        }
        m_firingBuffered = false;
    }

    void doSimCollisionCheck(GJBaseGameLayer* layer, PlayerObject* player,
                             std::vector<GameObject*>* vec, int objectsCount, float dt) {
        m_collisionScratch.clear();
        m_collisionScratch.reserve(objectsCount);
        for (int i = 0; i < objectsCount; i++) {
            GameObject* obj = vec->at(i);
            if (isCollisionRelevant(obj)) m_collisionScratch.push_back(obj);
        }
        layer->GJBaseGameLayer::collisionCheckObjects(player, &m_collisionScratch,
                                                      m_collisionScratch.size(), dt);
    }
};

static TrajectoryBufferSim s_sim;

$on_mod(Loaded) {
    AllocConsole();
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);
    SetConsoleTitleA("GuacTuah - Live Console");
    printf("============================================================\n");
    printf(" [InputBuffer] v5.3 LOADED (Collision Fix)\n");
    printf(" [InputBuffer] Press F6 in-level to cycle\n");
    printf("============================================================\n");
}

class $modify(BufPlayLayer, PlayLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriority("PlayLayer::destroyPlayer", -1000000);
        (void)self.setHookPriority("PlayLayer::playEndAnimationToPos", -1000000);
        (void)self.setHookPriority("PlayLayer::flipGravity", -1000000);
    }

    bool init(GJGameLevel* level, bool dontCreateObjects) {
        if (!PlayLayer::init(level, dontCreateObjects)) return false;
        s_sim.init();
        return true;
    }

    void destroyPlayer(PlayerObject* player, GameObject* gameObject) {
        if (s_sim.checkSimulationDeath(player)) return;
        PlayLayer::destroyPlayer(player, gameObject);
    }

    void playEndAnimationToPos(cocos2d::CCPoint p0) {
        if (s_sim.isSimulating()) return;
        PlayLayer::playEndAnimationToPos(p0);
    }

    void flipGravity(PlayerObject* p0, bool p1, bool p2) {
        if (s_sim.isSimulating()) return;
        PlayLayer::flipGravity(p0, p1, p2);
    }

    void onQuit() {
        PlayLayer::onQuit();
        s_sim.quit();
    }
};

class $modify(BufEditorLayer, LevelEditorLayer) {
    bool init(GJGameLevel* level, bool unk) {
        bool res = LevelEditorLayer::init(level, unk);
        s_sim.quit();
        return res;
    }
};

class $modify(BufHardStreak, HardStreak) {
    static void onModify(auto& self) { (void)self.setHookPriority("HardStreak::addPoint", -1000000); }
    void addPoint(cocos2d::CCPoint p0) {
        if (s_sim.isSimulating()) return;
        HardStreak::addPoint(p0);
    }
};

class $modify(BufEffectGameObject, EffectGameObject) {
    static void onModify(auto& self) { (void)self.setHookPriority("EffectGameObject::triggerObject", -1000000); }
    void triggerObject(GJBaseGameLayer* p0, int p1, const std::vector<int>* p2) {
        if (s_sim.isSimulating()) return;
        EffectGameObject::triggerObject(p0, p1, p2);
    }
};

class $modify(BufGameObject, GameObject) {
    static void onModify(auto& self) { (void)self.setHookPriority("GameObject::playShineEffect", -1000000); }
    void playShineEffect() {
        if (s_sim.isSimulating()) return;
        GameObject::playShineEffect();
    }
};

class $modify(BufPlayerObject, PlayerObject) {
    static void onModify(auto& self) {
        (void)self.setHookPriority("PlayerObject::playSpiderDashEffect", -1000000);
        (void)self.setHookPriority("PlayerObject::incrementJumps", -1000000);
        (void)self.setHookPriority("PlayerObject::ringJump", -1000000);
    }

    void playSpiderDashEffect(cocos2d::CCPoint from, cocos2d::CCPoint to) {
        if (s_sim.isSimulating()) return;
        PlayerObject::playSpiderDashEffect(from, to);
    }

    void incrementJumps() {
        if (s_sim.isSimulating() && !s_sim.isSimulationPlayer(this)) return;
        PlayerObject::incrementJumps();
    }

    void update(float dt) {
        if (PlayLayer::get() && !s_sim.isSimulating()) s_sim.setFrameDelta(dt);
        if (!s_sim.isSimulating()) s_sim.onPlayerUpdate(this);
        PlayerObject::update(dt);
    }

    void ringJump(RingObject* p0, bool p1) {
        if (s_sim.isSimulating() && !s_sim.isSimulationPlayer(this)) return;
        PlayerObject::ringJump(p0, p1);
    }
};

class $modify(BufBaseGameLayer, GJBaseGameLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriority("GJBaseGameLayer::canBeActivatedByPlayer", -1000000);
        (void)self.setHookPriority("GJBaseGameLayer::handleButton", -1000000);
        (void)self.setHookPriority("GJBaseGameLayer::collisionCheckObjects", -1000000);
        (void)self.setHookPriority("GJBaseGameLayer::playerTouchedRing", -1000000);
    }

    bool canBeActivatedByPlayer(PlayerObject* p0, EffectGameObject* p1) {
        if (s_sim.isSimulating()) return s_sim.canSimulateRingActivation(p0, p1);
        return GJBaseGameLayer::canBeActivatedByPlayer(p0, p1);
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        if (!s_sim.isSimulating() && !s_sim.isFiringBuffered() && button == 1 && PlayLayer::get()) {
            if (!s_sim.interceptInput(this, down, isPlayer1)) return;
        }
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    void collisionCheckObjects(PlayerObject* player, std::vector<GameObject*>* vec, int objectsCount, float dt) {
        if (s_sim.isSimulating()) {
            s_sim.doSimCollisionCheck(this, player, vec, objectsCount, dt);
            return;
        }
        GJBaseGameLayer::collisionCheckObjects(player, vec, objectsCount, dt);
    }

    void playerTouchedRing(PlayerObject* player, RingObject* ring) {
        if (s_sim.isSimulating()) {
            if (s_sim.canSimulateRingActivation(player, ring)) {
                player->ringJump(ring, true);
            }
            return;
        }
        GJBaseGameLayer::playerTouchedRing(player, ring);
    }
};
