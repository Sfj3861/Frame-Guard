#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <deque>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <vector>
#include <limits>

using namespace geode::prelude;

namespace {
    constexpr int kOverlayZ = 9999;
    constexpr std::size_t kTrailLimit = 48;

    class FrameGuardOverlay;
    static FrameGuardOverlay* g_activeOverlay = nullptr;

    std::string toggleText(char const* name, bool value) {
        return fmt::format("{}: {}", name, value ? "ON" : "OFF");
    }

    namespace FrameGuardTelemetry {
        struct Event {
            std::uint64_t frame;
            long long micros;
            int button;
            bool pressed;
            long long deltaMicros;
        };
        static std::deque<Event> events;
        static std::uint64_t frame = 0;
        static std::uint64_t inputs = 0;
        static int lastButton = -1;
        static bool lastPressed = false;
        static std::uint64_t lastFrame = 0;
        static long long lastMicros = -1;
        static long long deltaMicros = 0;
        static bool heldPlayer1 = false;
        static bool heldPlayer2 = false;
        static std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();

        static bool isHeld(bool secondPlayer) {
            return secondPlayer ? heldPlayer2 : heldPlayer1;
        }

        static void setHeld(bool secondPlayer, bool held) {
            if (secondPlayer) heldPlayer2 = held;
            else heldPlayer1 = held;
        }

        static void nextFrame() { ++frame; }
        static void record(PlayerButton button, bool pressed) {
            if (!Mod::get()->getSettingValue<bool>("frameguard-enabled")) return;
            auto now = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - started).count();
            deltaMicros = lastMicros < 0 ? 0 : now - lastMicros;
            lastMicros = now;
            lastButton = static_cast<int>(button);
            lastPressed = pressed;
            lastFrame = frame;
            ++inputs;
            events.push_back(Event{frame, now, lastButton, pressed, deltaMicros});
            while (events.size() > 16) events.pop_front();
        }
        static void reset() {
            frame = inputs = lastFrame = 0;
            lastButton = -1;
            lastMicros = -1;
            deltaMicros = 0;
            heldPlayer1 = false;
            heldPlayer2 = false;
            events.clear();
            started = std::chrono::steady_clock::now();
        }
        static std::string summary() {
            if (!Mod::get()->getSettingValue<bool>("frameguard-enabled")) return "FrameGuard OFF";
            if (lastButton < 0) return fmt::format("FrameGuard ON | frame {} | inputs 0", frame);
            return fmt::format("FrameGuard ON | frame {} | inputs {} | {} B{} @{} | delta {} us",
                frame, inputs, lastPressed ? "PRESS" : "RELEASE", lastButton, lastFrame, deltaMicros);
        }
        static bool exportCSV() {
            auto path = Mod::get()->getSaveDir() / "frameguard-inputs.csv";
            std::ofstream out(path);
            if (!out) return false;
            out << "frame,timestamp_us,button,edge,delta_us\n";
            for (auto const& event : events) {
                out << event.frame << "," << event.micros << "," << event.button << ","
                    << (event.pressed ? "PRESS" : "RELEASE") << "," << event.deltaMicros << "\n";
            }
            out.flush();
            return static_cast<bool>(out);
        }
    }

    // Frame-Guard draws collision geometry independently, using Geometry Dash's
    // collision rectangles, circles, slope direction and player oriented box.
    // It deliberately excludes decoration and non-collidable visual objects.
    class FrameGuardOverlay final : public cocos2d::CCNode {
    public:
        static FrameGuardOverlay* create() {
            auto* node = new FrameGuardOverlay();
            if (node && node->init()) {
                node->autorelease();
                return node;
            }
            CC_SAFE_DELETE(node);
            return nullptr;
        }

        bool init() override {
            if (!CCNode::init()) return false;
            m_draw = cocos2d::CCDrawNode::create();
            if (!m_draw) return false;
            this->addChild(m_draw);
            m_windowLabel1 = cocos2d::CCLabelBMFont::create("PREDICTION: --", "bigFont.fnt");
            m_windowLabel2 = cocos2d::CCLabelBMFont::create("PREDICTION: --", "bigFont.fnt");
            if (m_windowLabel1) {
                m_windowLabel1->setScale(0.34f);
                m_windowLabel1->setAnchorPoint(ccp(0.5f, 0.f));
                m_windowLabel1->setVisible(false);
                this->addChild(m_windowLabel1, 3);
            }
            if (m_windowLabel2) {
                m_windowLabel2->setScale(0.34f);
                m_windowLabel2->setAnchorPoint(ccp(0.5f, 0.f));
                m_windowLabel2->setVisible(false);
                this->addChild(m_windowLabel2, 3);
            }
            this->setPosition(cocos2d::CCPointZero);
            this->setID("frame-guard-overlay");
            g_activeOverlay = this;
            return true;
        }

        void resetTrail() {
            m_trail1.clear();
            m_trail2.clear();
            m_collisionObjects.clear();
            m_cachedObjectCount = std::numeric_limits<unsigned int>::max();
        }

        void refresh(PlayLayer* layer) {
            if (!layer || !m_draw) return;
            m_draw->clear();
            const int opacity = std::clamp(Mod::get()->getSettingValue<int>("overlay-opacity"), 0, 100);
            m_draw->setOpacity(static_cast<GLubyte>(opacity * 255 / 100));

            const bool showHitboxes = Mod::get()->getSettingValue<bool>("show-hitboxes");
            const bool showPlayerBounds = Mod::get()->getSettingValue<bool>("show-player-bounds");
            const bool showTrajectory = Mod::get()->getSettingValue<bool>("show-trajectory");
            const int marginTicks = std::clamp(Mod::get()->getSettingValue<int>("safe-margin-ticks"), 0, 12);
            const bool showSafeWindow = Mod::get()->getSettingValue<bool>("show-safe-window");

            if ((showHitboxes || showTrajectory || showSafeWindow) && layer->m_objects) {
                // Build the collision cache for both drawing and path-safety checks.
                const auto count = layer->m_objects->count();
                if (count != m_cachedObjectCount) {
                    m_collisionObjects.clear();
                    m_collisionObjects.reserve(count);
                    for (unsigned int i = 0; i < count; ++i) {
                        auto* object = typeinfo_cast<GameObject*>(layer->m_objects->objectAtIndex(i));
                        if (object && isCollisionObject(object))
                            m_collisionObjects.push_back(object);
                    }
                    m_cachedObjectCount = count;
                }
                if (showHitboxes) {
                    // Only draw geometry near the active camera/player. Drawing every
                    // object in a full level every frame caused avoidable CPU/GPU load.
                    const auto* player = layer->m_player1;
                    const cocos2d::CCPoint center = player ? cocos2d::CCPoint(player->m_positionX, player->m_positionY) : cocos2d::CCPointZero;
                    const float minX = center.x - 220.f;
                    const float maxX = center.x + 1100.f;
                    const float minY = center.y - 520.f;
                    const float maxY = center.y + 520.f;
                    for (auto* object : m_collisionObjects) {
                        if (!object) continue;
                        const auto rect = collisionRect(object);
                        if (rect.size.width <= 0.f || rect.size.height <= 0.f) continue;
                        // Cull by the actual collision rectangle, not object center/radius.
                        // This keeps long rectangles and offset hitboxes visible at screen edges.
                        if (rect.getMaxX() < minX || rect.getMinX() > maxX ||
                            rect.getMaxY() < minY || rect.getMinY() > maxY) continue;
                        drawObjectGeometry(object);
                    }
                }
            }

            // Record the actual post-physics player collision geometry each update.
            // This trail follows the real player transforms; the forward projection
            // remains a separate estimate because future triggers/contacts cannot be
            // reproduced reliably without running the game's own simulation.
            if (showTrajectory && layer->m_player1) {
                captureTrail(layer->m_player1, m_trail1);
                drawTrail(m_trail1, cocos2d::ccc4f(0.10f, 0.82f, 1.0f, 1.0f));
                drawPrediction(layer->m_player1, FrameGuardTelemetry::isHeld(false),
                    cocos2d::ccc4f(0.10f, 0.82f, 1.0f, 1.0f));
                if (layer->m_player2 && layer->m_player2->isRunning()) {
                    captureTrail(layer->m_player2, m_trail2);
                    drawTrail(m_trail2, cocos2d::ccc4f(1.0f, 0.45f, 0.10f, 1.0f));
                    drawPrediction(layer->m_player2, FrameGuardTelemetry::isHeld(true),
                        cocos2d::ccc4f(1.0f, 0.45f, 0.10f, 1.0f));
                } else {
                    m_trail2.clear();
                }
            } else {
                m_trail1.clear();
                m_trail2.clear();
            }

            if (showPlayerBounds || showHitboxes) {
                if (layer->m_player1) drawPlayerGeometry(layer->m_player1);
                if (layer->m_player2 && layer->m_player2->isRunning())
                    drawPlayerGeometry(layer->m_player2);
            }

            updatePredictionLabels(layer, showSafeWindow, marginTicks);
        }

        void updatePredictionLabels(PlayLayer* layer, bool enabled, int horizon) {
            auto updateOne = [&](PlayerObject* player, cocos2d::CCLabelBMFont* label) {
                if (!label) return;
                if (!enabled || !player || player->m_isDead) {
                    label->setVisible(false);
                    return;
                }
                label->setVisible(true);
                const int windowHorizon = std::clamp(horizon, 1, 12);
                std::string message;
                if (!(player->m_isDart || player->m_isShip || player->m_isSwing)) {
                    const int collisionTick = findHazardCollisionTick(player, windowHorizon);
                    message = collisionTick < 0
                        ? fmt::format("PATH CLEAR: {} TICKS (APPROX)", windowHorizon)
                        : fmt::format("HAZARD IN {} TICKS (APPROX)", collisionTick);
                } else {
                    const bool held = FrameGuardTelemetry::isHeld(player->m_isSecondPlayer);
                    int firstSafe = -1;
                    int lastSafe = -1;
                    for (int delay = 0; delay < windowHorizon; ++delay) {
                        if (isInputDelayPathClear(player, windowHorizon, delay, !held)) {
                            if (firstSafe < 0) firstSafe = delay;
                            lastSafe = delay;
                        } else if (firstSafe >= 0) {
                            break;
                        }
                    }
                    if (firstSafe < 0) {
                        message = fmt::format("{} NOW: NO CLEAR WINDOW", held ? "RELEASE" : "PRESS");
                    } else if (firstSafe == lastSafe) {
                        message = fmt::format("{} IN {} TICKS (EST.)", held ? "RELEASE" : "PRESS", firstSafe);
                    } else {
                        message = fmt::format("{} WINDOW {}-{} TICKS (EST.)",
                            held ? "RELEASE" : "PRESS", firstSafe, lastSafe);
                    }
                }
                label->setString(message.c_str());
                auto pos = player->getPosition();
                label->setPosition(ccp(pos.x, pos.y + 30.f));
            };
            updateOne(layer->m_player1, m_windowLabel1);
            updateOne(layer->m_player2 && layer->m_player2->isRunning() ? layer->m_player2 : nullptr,
                m_windowLabel2);
        }

        // Approximate continuous-input simulation. It intentionally supports only
        // modes where held/released input continuously changes vertical motion.
        // It does not emulate GD's full physics, collision resolution, triggers or portals.
        bool isInputDelayPathClear(PlayerObject* player, int horizon, int delayTicks, bool targetHeld) const {
            if (!player || player->m_isDead || m_collisionObjects.empty()) return false;
            horizon = std::clamp(horizon, 1, 12);
            delayTicks = std::clamp(delayTicks, 0, horizon - 1);
            auto pos = player->getPosition();
            float x = pos.x;
            float y = pos.y;
            double vx = player->getCurrentXVelocity();
            double vy = player->getYVelocity();
            const float gravitySign = static_cast<float>(player->flipMod());
            const float gravity = static_cast<float>(player->m_gravity * player->m_gravityMod);
            const bool currentHeld = FrameGuardTelemetry::isHeld(player->m_isSecondPlayer);
            const auto rect = player->getObjectRect(player->m_vehicleSize, player->m_vehicleSize);
            const float hw = std::max(2.f, rect.size.width * 0.5f);
            const float hh = std::max(2.f, rect.size.height * 0.5f);
            constexpr float step = 0.5f;

            auto clearOfHazards = [&](float px, float py) {
                cocos2d::CCRect box(px - hw, py - hh, hw * 2.f, hh * 2.f);
                for (auto* object : m_collisionObjects) {
                    if (!object) continue;
                    const bool hazard = typeIs(object, {
                        GameObjectType::Hazard, GameObjectType::AnimatedHazard
                    }) || (object->m_objectType == GameObjectType::Slope && object->m_slopeIsHazard);
                    if (hazard && box.intersectsRect(collisionRect(object))) return false;
                }
                return true;
            };

            for (int tick = 0; tick < horizon; ++tick) {
                const bool held = tick < delayTicks ? currentHeld : targetHeld;
                if (player->m_isDart) {
                    vy = held ? 6.0 : -6.0;
                } else {
                    vy += (held ? 0.72 : -0.72) * gravitySign * step;
                    vy = std::clamp(vy, -15.0, 15.0);
                }
                x += static_cast<float>(vx * step);
                y += static_cast<float>(vy * step);
                if (!clearOfHazards(x, y)) return false;
            }
            return true;
        }

        // Current-input lead time for modes without a supported input-window model.
        int findHazardCollisionTick(PlayerObject* player, int horizon) const {
            if (!player || player->m_isDead || m_collisionObjects.empty()) return -1;
            horizon = std::clamp(horizon, 1, 12);
            auto pos = player->getPosition();
            float x = pos.x;
            float y = pos.y;
            double vx = player->getCurrentXVelocity();
            double vy = player->getYVelocity();
            const float gravitySign = static_cast<float>(player->flipMod());
            const float gravity = static_cast<float>(player->m_gravity * player->m_gravityMod);
            const bool held = FrameGuardTelemetry::isHeld(player->m_isSecondPlayer);
            const auto rect = player->getObjectRect(player->m_vehicleSize, player->m_vehicleSize);
            const float hw = std::max(2.f, rect.size.width * 0.5f);
            const float hh = std::max(2.f, rect.size.height * 0.5f);
            constexpr float step = 0.5f;

            for (int tick = 1; tick <= horizon; ++tick) {
                if (player->m_isDart) vy = held ? 6.0 : -6.0;
                else {
                    vy -= gravity * gravitySign * 0.6 * step;
                    vy = std::clamp(vy, -15.0, 15.0);
                }
                x += static_cast<float>(vx * step);
                y += static_cast<float>(vy * step);
                cocos2d::CCRect box(x - hw, y - hh, hw * 2.f, hh * 2.f);
                for (auto* object : m_collisionObjects) {
                    if (!object) continue;
                    const bool hazard = typeIs(object, {
                        GameObjectType::Hazard, GameObjectType::AnimatedHazard
                    }) || (object->m_objectType == GameObjectType::Slope && object->m_slopeIsHazard);
                    if (hazard && box.intersectsRect(collisionRect(object))) return tick;
                }
            }
            return -1;
        }

        ~FrameGuardOverlay() override {
            if (g_activeOverlay == this) g_activeOverlay = nullptr;
        }

    private:
        static bool typeIs(GameObject* obj, std::initializer_list<GameObjectType> types) {
            return std::find(types.begin(), types.end(), obj->m_objectType) != types.end();
        }

        static bool isCollisionObject(GameObject* obj) {
            if (!obj || obj->m_isDecoration || obj->m_isDecoration2 || obj->m_unk3ee)
                return false;
            if (typeIs(obj, { GameObjectType::EnterEffectObject }))
                return false;
            return typeIs(obj, {
                GameObjectType::Hazard, GameObjectType::AnimatedHazard,
                GameObjectType::Solid, GameObjectType::Slope,
                GameObjectType::Breakable, GameObjectType::UserCoin,
                GameObjectType::Collectible, GameObjectType::SecretCoin,
                GameObjectType::InverseGravityPortal, GameObjectType::NormalGravityPortal,
                GameObjectType::ShipPortal, GameObjectType::CubePortal,
                GameObjectType::YellowJumpPad, GameObjectType::PinkJumpPad,
                GameObjectType::GravityPad, GameObjectType::YellowJumpRing,
                GameObjectType::PinkJumpRing, GameObjectType::GravityRing,
                GameObjectType::InverseMirrorPortal, GameObjectType::NormalMirrorPortal,
                GameObjectType::BallPortal, GameObjectType::RegularSizePortal,
                GameObjectType::MiniSizePortal, GameObjectType::UfoPortal,
                GameObjectType::DualPortal, GameObjectType::SoloPortal,
                GameObjectType::WavePortal, GameObjectType::RobotPortal,
                GameObjectType::TeleportPortal, GameObjectType::GreenRing,
                GameObjectType::DropRing, GameObjectType::SpiderPortal,
                GameObjectType::RedJumpPad, GameObjectType::RedJumpRing,
                GameObjectType::CustomRing, GameObjectType::DashRing,
                GameObjectType::GravityDashRing, GameObjectType::SwingPortal,
                GameObjectType::GravityTogglePortal, GameObjectType::SpiderOrb,
                GameObjectType::SpiderPad, GameObjectType::TeleportOrb,
                GameObjectType::Modifier
            });
        }

        static cocos2d::ccColor4F colourFor(GameObject* obj) {
            if (typeIs(obj, { GameObjectType::Hazard, GameObjectType::AnimatedHazard }) ||
                (obj->m_objectType == GameObjectType::Slope && obj->m_slopeIsHazard))
                return cocos2d::ccc4f(1.0f, 0.12f, 0.12f, 0.96f);
            if (typeIs(obj, { GameObjectType::Solid, GameObjectType::Slope }))
                return cocos2d::ccc4f(0.20f, 0.42f, 1.0f, 0.96f);
            if (obj->m_isPassable || typeIs(obj, { GameObjectType::Breakable }))
                return cocos2d::ccc4f(0.0f, 0.95f, 1.0f, 0.96f);
            if (typeIs(obj, {
                GameObjectType::UserCoin, GameObjectType::Collectible, GameObjectType::SecretCoin,
                GameObjectType::InverseGravityPortal, GameObjectType::NormalGravityPortal,
                GameObjectType::ShipPortal, GameObjectType::CubePortal, GameObjectType::YellowJumpPad,
                GameObjectType::PinkJumpPad, GameObjectType::GravityPad, GameObjectType::YellowJumpRing,
                GameObjectType::PinkJumpRing, GameObjectType::GravityRing, GameObjectType::BallPortal,
                GameObjectType::RegularSizePortal, GameObjectType::MiniSizePortal, GameObjectType::UfoPortal,
                GameObjectType::DualPortal, GameObjectType::SoloPortal, GameObjectType::WavePortal,
                GameObjectType::RobotPortal, GameObjectType::TeleportPortal, GameObjectType::GreenRing,
                GameObjectType::DropRing, GameObjectType::SpiderPortal, GameObjectType::RedJumpPad,
                GameObjectType::RedJumpRing, GameObjectType::CustomRing, GameObjectType::DashRing,
                GameObjectType::GravityDashRing, GameObjectType::SwingPortal, GameObjectType::GravityTogglePortal,
                GameObjectType::SpiderOrb, GameObjectType::SpiderPad, GameObjectType::TeleportOrb,
                GameObjectType::Modifier
            }))
                return cocos2d::ccc4f(0.12f, 1.0f, 0.25f, 0.96f);
            return cocos2d::ccc4f(1.0f, 0.85f, 0.15f, 0.9f);
        }

        struct PlayerSnapshot {
            std::array<cocos2d::CCPoint, 4> body;
            std::array<cocos2d::CCPoint, 4> mini;
        };

        static std::array<cocos2d::CCPoint, 4> rectCorners(cocos2d::CCRect const& rect) {
            return {{
                ccp(rect.getMinX(), rect.getMinY()),
                ccp(rect.getMaxX(), rect.getMinY()),
                ccp(rect.getMaxX(), rect.getMaxY()),
                ccp(rect.getMinX(), rect.getMaxY())
            }};
        }

        static PlayerSnapshot snapshotPlayer(PlayerObject* player) {
            PlayerSnapshot snapshot{};
            auto bodyRect = player->getObjectRect(player->m_vehicleSize, player->m_vehicleSize);
            snapshot.body = rectCorners(bodyRect);
            if (player->m_orientedBox) {
                for (int i = 0; i < 4; ++i)
                    snapshot.body[i] = player->m_orientedBox->m_corners[i];
            }
            snapshot.mini = rectCorners(player->getObjectRect(0.3f, 0.3f));
            return snapshot;
        }

        static void captureTrail(PlayerObject* player, std::deque<PlayerSnapshot>& trail) {
            if (!player || player->m_isDead) {
                trail.clear();
                return;
            }
            trail.push_back(snapshotPlayer(player));
            while (trail.size() > kTrailLimit) trail.pop_front();
        }

        void drawTrail(std::deque<PlayerSnapshot> const& trail, cocos2d::ccColor4F base) {
            if (trail.empty()) return;
            for (std::size_t i = 0; i < trail.size(); ++i) {
                const float fade = static_cast<float>(i + 1) /
                    static_cast<float>(trail.size());
                auto bodyColour = base;
                bodyColour.a = 0.04f + 0.26f * fade;
                auto miniColour = cocos2d::ccc4f(0.22f, 1.0f, 0.48f, 0.03f + 0.18f * fade);
                const auto& state = trail[i];
                m_draw->drawPolygon(const_cast<cocos2d::CCPoint*>(state.body.data()), 4,
                    cocos2d::ccc4f(0.f, 0.f, 0.f, 0.f), 0.65f, bodyColour,
                    cocos2d::BorderAlignment::Inside);
                m_draw->drawPolygon(const_cast<cocos2d::CCPoint*>(state.mini.data()), 4,
                    cocos2d::ccc4f(0.f, 0.f, 0.f, 0.f), 0.55f, miniColour,
                    cocos2d::BorderAlignment::Inside);
            }
        }

        void drawPrediction(PlayerObject* player, bool held, cocos2d::ccColor4F colour) {
            if (!player || player->m_isDead) return;

            // Limit prediction to the next eight update steps; each outline is one step.
            // The observed trail above is exact sampled history; future motion remains estimated.
            constexpr int iterations = 8;
            constexpr float step = 1.0f;
            constexpr int boxEvery = 1;
            auto position = player->getPosition();
            // Build a local collision set once per frame instead of scanning every
            // object for every projected point. This reduces work on long levels.
            std::vector<GameObject*> nearbyObjects;
            nearbyObjects.reserve(std::min<std::size_t>(m_collisionObjects.size(), 256));
            for (auto* object : m_collisionObjects) {
                if (!object || !typeIs(object, {
                    GameObjectType::Hazard, GameObjectType::AnimatedHazard,
                    GameObjectType::Solid, GameObjectType::Slope
                })) continue;
                const auto rect = collisionRect(object);
                if (rect.getMaxX() < position.x - 24.f ||
                    rect.getMinX() > position.x + 980.f ||
                    rect.getMaxY() < position.y - 460.f ||
                    rect.getMinY() > position.y + 460.f) continue;
                nearbyObjects.push_back(object);
            }
            float x = position.x;
            float y = position.y;
            double xVelocity = player->getCurrentXVelocity();
            double yVelocity = player->getYVelocity();
            const float gravitySign = static_cast<float>(player->flipMod());
            const float gravity = static_cast<float>(player->m_gravity * player->m_gravityMod);
            const auto currentRect = player->getObjectRect(player->m_vehicleSize, player->m_vehicleSize);
            const float halfWidth = std::max(2.0f, currentRect.size.width * 0.5f);
            const float halfHeight = std::max(2.0f, currentRect.size.height * 0.5f);
            cocos2d::CCPoint previous = ccp(x, y);

            for (int i = 1; i <= iterations; ++i) {
                if (player->m_isDart) {
                    // Wave-like movement changes vertical direction directly with input.
                    yVelocity = held ? 6.0 : -6.0;
                } else if (player->m_isShip || player->m_isBird || player->m_isSwing) {
                    // Flight modes accelerate up while held and down while released.
                    const double thrust = held ? 0.72 : -0.72;
                    yVelocity += thrust * gravitySign * step;
                } else {
                    yVelocity -= gravity * gravitySign * 0.6 * step;
                    yVelocity = std::clamp(yVelocity, -15.0, 15.0);
                }

                x += static_cast<float>(xVelocity * step);
                y += static_cast<float>(yVelocity * step);
                const auto current = ccp(x, y);
                const int marginTicks = std::clamp(
                    Mod::get()->getSettingValue<int>("safe-margin-ticks"), 0, 12);
                const float safetyPad = static_cast<float>(marginTicks) * 1.25f;
                cocos2d::CCRect projectedBox(
                    x - halfWidth - safetyPad, y - halfHeight - safetyPad,
                    halfWidth * 2.f + safetyPad * 2.f, halfHeight * 2.f + safetyPad * 2.f);
                bool unsafe = false;
                for (auto* object : nearbyObjects) {
                    if (!object) continue;
                    if (projectedBox.intersectsRect(collisionRect(object))) {
                        unsafe = true;
                        break;
                    }
                }
                auto segmentColour = unsafe
                    ? cocos2d::ccc4f(1.f, 0.12f, 0.12f, colour.a)
                    : colour;
                segmentColour.a = 0.72f * (1.0f - static_cast<float>(i) /
                    static_cast<float>(iterations) * 0.55f);
                m_draw->drawSegment(previous, current, 1.15f, segmentColour);
                previous = current;

                if (i % boxEvery == 0) {
                    cocos2d::CCPoint box[] = {
                        ccp(x - halfWidth, y - halfHeight), ccp(x + halfWidth, y - halfHeight),
                        ccp(x + halfWidth, y + halfHeight), ccp(x - halfWidth, y + halfHeight)
                    };
                    auto boxColour = colour;
                    boxColour.a = 0.10f + 0.20f * (1.0f - static_cast<float>(i) /
                        static_cast<float>(iterations));
                    m_draw->drawPolygon(box, 4, cocos2d::ccc4f(0.f, 0.f, 0.f, 0.f),
                        0.65f, boxColour, cocos2d::BorderAlignment::Inside);
                }

                if (unsafe || x > position.x + 900.f || y < -200.f || y > 1400.f) break;
            }
        }

        static cocos2d::CCRect collisionRect(GameObject* obj) {
            if (!obj->m_isObjectRectDirty)
                return obj->m_objectRect;
            const bool wasDirty = obj->m_isObjectRectDirty;
            const bool boxOffsetWasCalculated = obj->m_boxOffsetCalculated;
            const auto rect = obj->getObjectRect();
            obj->m_isObjectRectDirty = wasDirty;
            obj->m_boxOffsetCalculated = boxOffsetWasCalculated;
            return rect;
        }

        void drawObjectGeometry(GameObject* obj) {
            const auto colour = colourFor(obj);
            const float width = 0.7f;

            if (obj->m_objectRadius > 0.0f) {
                const float radius = obj->m_objectRadius *
                    std::max<float>(obj->m_scaleX, obj->m_scaleY);
                // Fewer segments reduce tessellation cost for circular hitboxes.
                m_draw->drawCircle(ccp(obj->m_positionX, obj->m_positionY), radius,
                    cocos2d::ccc4f(0.f, 0.f, 0.f, 0.f), width, colour, 24);
                return;
            }

            // Reuse Geometry Dash's cached collision rect when possible. If the
            // game marks it dirty, calculate it once while restoring its dirty flags.
            const auto rect = collisionRect(obj);
            if (rect.size.width <= 0.f || rect.size.height <= 0.f) return;

            if (obj->m_objectType == GameObjectType::Slope) {
                cocos2d::CCPoint points[] = {
                    ccp(rect.getMaxX(), rect.getMinY()),
                    ccp(rect.getMinX(), rect.getMaxY()),
                    ccp(rect.getMinX(), rect.getMinY())
                };
                switch (obj->m_slopeDirection) {
                    case 0:
                    case 7:
                        points[1] = ccp(rect.getMaxX(), rect.getMaxY());
                        break;
                    case 3:
                    case 6:
                        points[0] = ccp(rect.getMaxX(), rect.getMaxY());
                        break;
                    case 1:
                    case 5:
                        points[2] = ccp(rect.getMinX(), rect.getMaxY());
                        points[1] = ccp(rect.getMaxX(), rect.getMaxY());
                        points[0] = ccp(rect.getMaxX(), rect.getMinY());
                        break;
                    default:
                        break;
                }
                m_draw->drawPolygon(points, 3, cocos2d::ccc4f(0.f, 0.f, 0.f, 0.f),
                    width, colour, cocos2d::BorderAlignment::Inside);
                return;
            }

            cocos2d::CCPoint corners[] = {
                ccp(rect.getMinX(), rect.getMinY()),
                ccp(rect.getMaxX(), rect.getMinY()),
                ccp(rect.getMaxX(), rect.getMaxY()),
                ccp(rect.getMinX(), rect.getMaxY())
            };
            if (obj->m_orientedBox) {
                for (int i = 0; i < 4; ++i)
                    corners[i] = obj->m_orientedBox->m_corners[i];
            }
            m_draw->drawPolygon(corners, 4, cocos2d::ccc4f(0.f, 0.f, 0.f, 0.f),
                width, colour, cocos2d::BorderAlignment::Inside);
        }

        void drawPlayerGeometry(PlayerObject* player) {
            if (!player) return;
            const auto normal = cocos2d::ccc4f(1.0f, 0.12f, 0.12f, 0.98f);
            const auto rotated = cocos2d::ccc4f(0.95f, 0.05f, 0.48f, 0.98f);
            const auto mini = cocos2d::ccc4f(0.12f, 0.95f, 1.0f, 0.98f);
            if (player->m_orientedBox) {
                cocos2d::CCPoint corners[] = {
                    player->m_orientedBox->m_corners[0], player->m_orientedBox->m_corners[1],
                    player->m_orientedBox->m_corners[2], player->m_orientedBox->m_corners[3]
                };
                m_draw->drawPolygon(corners, 4, cocos2d::ccc4f(0.f, 0.f, 0.f, 0.f),
                    0.8f, rotated, cocos2d::BorderAlignment::Inside);
            }

            auto rect = player->getObjectRect(player->m_vehicleSize, player->m_vehicleSize);
            cocos2d::CCPoint body[] = {
                ccp(rect.getMinX(), rect.getMinY()), ccp(rect.getMaxX(), rect.getMinY()),
                ccp(rect.getMaxX(), rect.getMaxY()), ccp(rect.getMinX(), rect.getMaxY())
            };
            m_draw->drawPolygon(body, 4, cocos2d::ccc4f(0.f, 0.f, 0.f, 0.f),
                0.8f, normal, cocos2d::BorderAlignment::Inside);

            auto smallRect = player->getObjectRect(0.3f, 0.3f);
            cocos2d::CCPoint small[] = {
                ccp(smallRect.getMinX(), smallRect.getMinY()),
                ccp(smallRect.getMaxX(), smallRect.getMinY()),
                ccp(smallRect.getMaxX(), smallRect.getMaxY()),
                ccp(smallRect.getMinX(), smallRect.getMaxY())
            };
            m_draw->drawPolygon(small, 4, cocos2d::ccc4f(0.f, 0.f, 0.f, 0.f),
                0.8f, mini, cocos2d::BorderAlignment::Inside);
        }

        cocos2d::CCDrawNode* m_draw = nullptr;
        cocos2d::CCLabelBMFont* m_windowLabel1 = nullptr;
        cocos2d::CCLabelBMFont* m_windowLabel2 = nullptr;
        std::deque<PlayerSnapshot> m_trail1;
        std::deque<PlayerSnapshot> m_trail2;
        std::vector<GameObject*> m_collisionObjects;
        unsigned int m_cachedObjectCount = std::numeric_limits<unsigned int>::max();
    };

    class FrameGuardSlider final : public cocos2d::CCLayer {
    public:
        using ChangeCallback = std::function<void(int)>;
        static FrameGuardSlider* create(char const* title, char const* unit, int minimum,
            int maximum, int initial, ChangeCallback callback) {
            auto* node = new FrameGuardSlider();
            if (node && node->initSlider(title, unit, minimum, maximum, initial, std::move(callback))) {
                node->autorelease();
                return node;
            }
            CC_SAFE_DELETE(node);
            return nullptr;
        }
        bool initSlider(char const* title, char const* unit, int minimum, int maximum,
            int initial, ChangeCallback callback) {
            if (!CCLayer::init()) return false;
            this->setTouchEnabled(true);
            m_title = title;
            m_unit = unit;
            m_min = minimum;
            m_max = std::max(minimum + 1, maximum);
            m_value = std::clamp(initial, m_min, m_max);
            m_callback = std::move(callback);
            this->setContentSize({190.f, 34.f});
            m_draw = cocos2d::CCDrawNode::create();
            if (!m_draw) return false;
            this->addChild(m_draw);
            m_label = cocos2d::CCLabelBMFont::create("", "bigFont.fnt");
            if (!m_label) return false;
            m_label->setAnchorPoint(ccp(0.f, 0.5f));
            m_label->setScale(0.34f);
            m_label->setPosition(ccp(0.f, 27.f));
            this->addChild(m_label);
            redraw();
            return true;
        }
        void registerWithTouchDispatcher() override {
            cocos2d::CCDirector::sharedDirector()->getTouchDispatcher()->addTargetedDelegate(this, -128, true);
        }
        bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent*) override {
            auto p = this->convertToNodeSpace(touch->getLocation());
            if (!this->isVisible() || p.x < 0.f || p.x > 190.f || p.y < 0.f || p.y > 23.f) return false;
            setFromX(p.x);
            m_dragging = true;
            return true;
        }
        void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent*) override {
            if (m_dragging) setFromX(this->convertToNodeSpace(touch->getLocation()).x);
        }
        void ccTouchEnded(cocos2d::CCTouch*, cocos2d::CCEvent*) override { m_dragging = false; }
        void ccTouchCancelled(cocos2d::CCTouch*, cocos2d::CCEvent*) override { m_dragging = false; }

        int getValue() const { return m_value; }
        void setValue(int value) {
            value = std::clamp(value, m_min, m_max);
            if (value == m_value) return;
            m_value = value;
            redraw();
        }
    private:
        void setFromX(float x) {
            const float t = std::clamp((x - 6.f) / 178.f, 0.f, 1.f);
            const int next = m_min + static_cast<int>(std::round(t * (m_max - m_min)));
            if (next == m_value) return;
            m_value = next;
            redraw();
            if (m_callback) m_callback(m_value);
        }
        void redraw() {
            if (!m_draw || !m_label) return;
            m_draw->clear();
            const float t = static_cast<float>(m_value - m_min) / static_cast<float>(m_max - m_min);
            m_draw->drawSegment(ccp(6.f, 10.f), ccp(184.f, 10.f), 3.f, cocos2d::ccc4f(0.22f, 0.25f, 0.31f, 0.95f));
            m_draw->drawSegment(ccp(6.f, 10.f), ccp(6.f + 178.f * t, 10.f), 3.f, cocos2d::ccc4f(0.12f, 0.75f, 1.f, 1.f));
            m_draw->drawCircle(ccp(6.f + 178.f * t, 10.f), 6.f, cocos2d::ccc4f(0.12f, 0.75f, 1.f, 1.f), 1.f, cocos2d::ccc4f(1.f, 1.f, 1.f, 1.f), 20);
            m_label->setString(fmt::format("{}: {}{}", m_title, m_value, m_unit).c_str());
        }
        cocos2d::CCDrawNode* m_draw = nullptr;
        cocos2d::CCLabelBMFont* m_label = nullptr;
        ChangeCallback m_callback;
        std::string m_title, m_unit;
        int m_min = 0, m_max = 100, m_value = 100;
        bool m_dragging = false;
    };

    class FrameGuardMenu final : public cocos2d::CCLayer {
    public:
        static FrameGuardMenu* create() {
            auto* node = new FrameGuardMenu();
            if (node && node->init()) {
                node->autorelease();
                return node;
            }
            CC_SAFE_DELETE(node);
            return nullptr;
        }

        bool init() override {
            if (!CCLayer::init()) return false;
            this->setTouchEnabled(true);
            auto* director = cocos2d::CCDirector::sharedDirector();
            const auto size = director->getWinSize();

            m_menu = cocos2d::CCMenu::create();
            if (!m_menu) return false;
            m_menu->setPosition(cocos2d::CCPointZero);
            this->addChild(m_menu, 1);

            auto* floatingSprite = cocos2d::CCSprite::createWithSpriteFrameName("GJ_button_01.png");
            if (!floatingSprite) floatingSprite = cocos2d::CCSprite::create("GJ_button_01.png");
            if (!floatingSprite) return false;
            floatingSprite->setOpacity(0);
            floatingSprite->setScale(0.72f);
            m_openButton = CCMenuItemSpriteExtra::create(
                floatingSprite, this, menu_selector(FrameGuardMenu::onToggleMenu));
            if (m_openButton) {
                auto* circle = cocos2d::CCDrawNode::create();
                if (circle) {
                    circle->drawCircle(ccp(0.f, 0.f), 19.f, cocos2d::ccc4f(0.15f, 0.72f, 1.f, 0.38f),
                        1.5f, cocos2d::ccc4f(0.45f, 0.88f, 1.f, 0.68f), 48);
                    m_openButton->addChild(circle, 1);
                }
                auto* openLabel = cocos2d::CCLabelBMFont::create("FG", "bigFont.fnt");
                if (openLabel) {
                    openLabel->setScale(0.48f);
                    openLabel->setPosition(CCPointZero);
                    m_openButton->addChild(openLabel, 2);
                }
            }
            m_hitboxButton = makeItem("", menu_selector(FrameGuardMenu::onToggleHitboxes));
            m_playerButton = makeItem("", menu_selector(FrameGuardMenu::onTogglePlayerBounds));
            m_trailButton = makeItem("", menu_selector(FrameGuardMenu::onToggleTrail));
            m_fpsButton = makeItem("", menu_selector(FrameGuardMenu::onToggleFPS));
            m_guardButton = makeItem("", menu_selector(FrameGuardMenu::onToggleFrameGuard));
            m_exportButton = makeItem("Export CSV", menu_selector(FrameGuardMenu::onExportCSV));
            m_infoButton = makeItem("", menu_selector(FrameGuardMenu::onToggleInfo));
            m_inputGuardButton = makeItem("", menu_selector(FrameGuardMenu::onToggleSafeWindow));
            m_opacitySlider = FrameGuardSlider::create("Opacity", "%", 0, 100,
                Mod::get()->getSettingValue<int>("overlay-opacity"), [](int value) {
                    Mod::get()->setSettingValue<int>("overlay-opacity", value);
                });
            m_marginSlider = FrameGuardSlider::create("Safe margin", " ticks", 0, 12,
                Mod::get()->getSettingValue<int>("safe-margin-ticks"), [](int value) {
                    Mod::get()->setSettingValue<int>("safe-margin-ticks", value);
                });
            if (!m_openButton || !m_hitboxButton || !m_playerButton || !m_trailButton ||
                !m_fpsButton || !m_guardButton || !m_exportButton || !m_infoButton ||
                !m_inputGuardButton || !m_opacitySlider || !m_marginSlider) return false;
            this->addEventListener(
                KeybindSettingPressedEventV3(Mod::get(), "toggle-menu"),
                [this](Keybind const&, bool down, bool repeat, double) {
                    if (down && !repeat) this->toggleMenu();
                }
            );

            m_openButton->setPosition(ccp(42.f, size.height - 34.f));
            m_hitboxButton->setPosition(ccp(91.f, size.height - 64.f));
            m_playerButton->setPosition(ccp(91.f, size.height - 88.f));
            m_trailButton->setPosition(ccp(91.f, size.height - 112.f));
            m_fpsButton->setPosition(ccp(91.f, size.height - 136.f));
            m_guardButton->setPosition(ccp(91.f, size.height - 160.f));
            m_exportButton->setPosition(ccp(91.f, size.height - 184.f));
            m_infoButton->setPosition(ccp(91.f, size.height - 208.f));
            m_inputGuardButton->setPosition(ccp(91.f, size.height - 232.f));
            m_opacitySlider->setPosition(ccp(112.f, size.height - 266.f));
            m_marginSlider->setPosition(ccp(112.f, size.height - 302.f));
            m_menu->addChild(m_openButton);
            m_menu->addChild(m_hitboxButton);
            m_menu->addChild(m_playerButton);
            m_menu->addChild(m_trailButton);
            m_menu->addChild(m_fpsButton);
            m_menu->addChild(m_guardButton);
            m_menu->addChild(m_exportButton);
            m_menu->addChild(m_infoButton);
            m_menu->addChild(m_inputGuardButton);
            m_menu->addChild(m_opacitySlider);
            m_menu->addChild(m_marginSlider);
            m_hitboxButton->setVisible(false);
            m_playerButton->setVisible(false);
            m_trailButton->setVisible(false);
            m_fpsButton->setVisible(false);
            m_guardButton->setVisible(false);
            m_exportButton->setVisible(false);
            m_infoButton->setVisible(false);
            m_inputGuardButton->setVisible(false);
            m_opacitySlider->setVisible(false);
            m_marginSlider->setVisible(false);

            m_fpsLabel = cocos2d::CCLabelBMFont::create("FPS: --", "bigFont.fnt");
            if (m_fpsLabel) {
                m_fpsLabel->setScale(0.42f);
                m_fpsLabel->setPosition(ccp(size.width / 2.f, size.height - 20.f));
                m_fpsLabel->setVisible(Mod::get()->getSettingValue<bool>("show-fps"));
                this->addChild(m_fpsLabel, 2);
            }
            m_guardLabel = cocos2d::CCLabelBMFont::create("FrameGuard starting...", "bigFont.fnt");
            if (m_guardLabel) {
                m_guardLabel->setScale(0.34f);
                m_guardLabel->setAnchorPoint(ccp(0.f, 0.5f));
                m_guardLabel->setPosition(ccp(8.f, size.height - 42.f));
                m_guardLabel->setVisible(Mod::get()->getSettingValue<bool>("show-frameguard-hud"));
                this->addChild(m_guardLabel, 2);
            }
            syncLabels();
            this->setID("frame-guard-in-game-menu");
            return true;
        }

        void registerWithTouchDispatcher() override {
            cocos2d::CCDirector::sharedDirector()->getTouchDispatcher()->addTargetedDelegate(this, -128, true);
        }
        bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent*) override {
            auto p = this->convertToNodeSpace(touch->getLocation());
            auto* director = cocos2d::CCDirector::sharedDirector();
            const auto size = director->getWinSize();
            if (std::abs(p.x - 42.f) > 27.f || std::abs(p.y - (size.height - 34.f)) > 27.f) return false;
            m_dragging = true;
            m_lastTouch = touch->getLocation();
            return true;
        }
        void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent*) override {
            if (!m_dragging) return;
            auto* director = cocos2d::CCDirector::sharedDirector();
            const auto size = director->getWinSize();
            auto now = touch->getLocation();
            auto delta = now - m_lastTouch;
            auto pos = this->getPosition() + delta;
            pos.x = std::clamp(pos.x, -size.width + 40.f, size.width - 40.f);
            pos.y = std::clamp(pos.y, -size.height + 40.f, size.height - 40.f);
            this->setPosition(pos);
            m_lastTouch = now;
        }
        void ccTouchEnded(cocos2d::CCTouch*, cocos2d::CCEvent*) override { m_dragging = false; }
        void ccTouchCancelled(cocos2d::CCTouch*, cocos2d::CCEvent*) override { m_dragging = false; }

        void toggleMenu() {
            m_open = !m_open;
            m_hitboxButton->setVisible(m_open);
            m_playerButton->setVisible(m_open);
            m_trailButton->setVisible(m_open);
            m_fpsButton->setVisible(m_open);
            m_guardButton->setVisible(m_open);
            m_exportButton->setVisible(m_open);
            m_infoButton->setVisible(m_open);
            m_inputGuardButton->setVisible(m_open);
            m_opacitySlider->setVisible(m_open);
            m_marginSlider->setVisible(m_open);
            syncLabels();
        }

        void syncLabels() {
            if (m_hitboxButton)
                static_cast<cocos2d::CCLabelBMFont*>(m_hitboxButton->getLabel())->setString(
                    toggleText("Hitboxes", Mod::get()->getSettingValue<bool>("show-hitboxes")).c_str());
            if (m_playerButton)
                static_cast<cocos2d::CCLabelBMFont*>(m_playerButton->getLabel())->setString(
                    toggleText("Player", Mod::get()->getSettingValue<bool>("show-player-bounds")).c_str());
            if (m_trailButton)
                static_cast<cocos2d::CCLabelBMFont*>(m_trailButton->getLabel())->setString(
                    toggleText("Trajectory", Mod::get()->getSettingValue<bool>("show-trajectory")).c_str());
            if (m_fpsButton)
                static_cast<cocos2d::CCLabelBMFont*>(m_fpsButton->getLabel())->setString(
                    toggleText("FPS", Mod::get()->getSettingValue<bool>("show-fps")).c_str());
            const bool showInfo = Mod::get()->getSettingValue<bool>("show-info-text");
            if (m_fpsLabel) m_fpsLabel->setVisible(showInfo && Mod::get()->getSettingValue<bool>("show-fps"));
            if (m_guardLabel) m_guardLabel->setVisible(showInfo && Mod::get()->getSettingValue<bool>("show-frameguard-hud"));
            if (m_infoButton)
                static_cast<cocos2d::CCLabelBMFont*>(m_infoButton->getLabel())->setString(
                    toggleText("Info text", showInfo).c_str());
            if (m_inputGuardButton)
                static_cast<cocos2d::CCLabelBMFont*>(m_inputGuardButton->getLabel())->setString(
                    toggleText("Path preview", Mod::get()->getSettingValue<bool>("show-safe-window")).c_str());
            if (m_opacitySlider) m_opacitySlider->setValue(Mod::get()->getSettingValue<int>("overlay-opacity"));
            if (m_marginSlider) m_marginSlider->setValue(Mod::get()->getSettingValue<int>("safe-margin-ticks"));
            if (m_guardButton)
                static_cast<cocos2d::CCLabelBMFont*>(m_guardButton->getLabel())->setString(
                    toggleText("FrameGuard", Mod::get()->getSettingValue<bool>("frameguard-enabled")).c_str());
            if (m_guardLabel)
                m_guardLabel->setVisible(showInfo && Mod::get()->getSettingValue<bool>("show-frameguard-hud"));
        }

        void updateFrameGuardHUD(float dt) {
            if (!m_guardLabel || !Mod::get()->getSettingValue<bool>("show-info-text") ||
                !Mod::get()->getSettingValue<bool>("show-frameguard-hud")) return;
            m_guardElapsed += dt;
            if (m_guardElapsed >= 0.10f) {
                auto text = FrameGuardTelemetry::summary();
                m_guardLabel->setString(text.c_str());
                m_guardElapsed = 0.f;
            }
        }

        void updateFPS(float dt) {
            if (!m_fpsLabel || !Mod::get()->getSettingValue<bool>("show-info-text") ||
                !Mod::get()->getSettingValue<bool>("show-fps") || dt <= 0.f) return;
            m_elapsed += dt;
            ++m_frames;
            if (m_elapsed >= 0.5f) {
                const auto fps = static_cast<int>(static_cast<float>(m_frames) / m_elapsed + 0.5f);
                auto text = fmt::format("FPS: {}", fps);
                m_fpsLabel->setString(text.c_str());
                m_elapsed = 0.f;
                m_frames = 0;
            }
        }

    private:
        cocos2d::CCMenuItemLabel* makeItem(char const* text, cocos2d::SEL_MenuHandler callback) {
            auto* label = cocos2d::CCLabelBMFont::create(text, "bigFont.fnt");
            if (!label) return nullptr;
            label->setScale(0.48f);
            return cocos2d::CCMenuItemLabel::create(label, this, callback);
        }
        void onToggleMenu(cocos2d::CCObject*) { toggleMenu(); }
        void onToggleSafeWindow(cocos2d::CCObject*) {
            Mod::get()->setSettingValue<bool>("show-safe-window",
                !Mod::get()->getSettingValue<bool>("show-safe-window"));
            syncLabels();
        }
        void onToggleInfo(cocos2d::CCObject*) {
            Mod::get()->setSettingValue<bool>("show-info-text",
                !Mod::get()->getSettingValue<bool>("show-info-text"));
            syncLabels();
        }
        void onToggleHitboxes(cocos2d::CCObject*) {
            Mod::get()->setSettingValue<bool>("show-hitboxes",
                !Mod::get()->getSettingValue<bool>("show-hitboxes"));
            syncLabels();
        }
        void onTogglePlayerBounds(cocos2d::CCObject*) {
            Mod::get()->setSettingValue<bool>("show-player-bounds",
                !Mod::get()->getSettingValue<bool>("show-player-bounds"));
            syncLabels();
        }
        void onToggleTrail(cocos2d::CCObject*) {
            Mod::get()->setSettingValue<bool>("show-trajectory",
                !Mod::get()->getSettingValue<bool>("show-trajectory"));
            syncLabels();
        }
        void onToggleFPS(cocos2d::CCObject*) {
            Mod::get()->setSettingValue<bool>("show-fps",
                !Mod::get()->getSettingValue<bool>("show-fps"));
            syncLabels();
        }
        void onToggleFrameGuard(cocos2d::CCObject*) {
            Mod::get()->setSettingValue<bool>("frameguard-enabled",
                !Mod::get()->getSettingValue<bool>("frameguard-enabled"));
            syncLabels();
        }
        void onExportCSV(cocos2d::CCObject*) {
            if (FrameGuardTelemetry::exportCSV()) {
                log::info("FrameGuard: exported recent input events to {}",
                    (Mod::get()->getSaveDir() / "frameguard-inputs.csv").string());
                if (m_guardLabel) m_guardLabel->setString("FrameGuard: CSV exported (Geode mod save folder)");
            } else {
                log::warn("FrameGuard: failed to export input events CSV");
                if (m_guardLabel) m_guardLabel->setString("FrameGuard: CSV export failed");
            }
        }

        cocos2d::CCMenu* m_menu = nullptr;
        CCMenuItemSpriteExtra* m_openButton = nullptr;
        cocos2d::CCMenuItemLabel* m_hitboxButton = nullptr;
        cocos2d::CCMenuItemLabel* m_playerButton = nullptr;
        cocos2d::CCMenuItemLabel* m_trailButton = nullptr;
        cocos2d::CCMenuItemLabel* m_fpsButton = nullptr;
        cocos2d::CCMenuItemLabel* m_guardButton = nullptr;
        cocos2d::CCMenuItemLabel* m_exportButton = nullptr;
        cocos2d::CCMenuItemLabel* m_infoButton = nullptr;
        cocos2d::CCMenuItemLabel* m_inputGuardButton = nullptr;
        FrameGuardSlider* m_opacitySlider = nullptr;
        FrameGuardSlider* m_marginSlider = nullptr;
        cocos2d::CCPoint m_lastTouch = CCPointZero;
        bool m_dragging = false;
        cocos2d::CCLabelBMFont* m_fpsLabel = nullptr;
        cocos2d::CCLabelBMFont* m_guardLabel = nullptr;
        float m_guardElapsed = 0.f;
        float m_elapsed = 0.f;
        unsigned int m_frames = 0;
        bool m_open = false;
    };
}

class $modify(FrameGuardPlayLayer, PlayLayer) {
    struct Fields {
        FrameGuardOverlay* overlay = nullptr;
        FrameGuardMenu* menu = nullptr;
    };

    void postUpdate(float dt) {
        // Advance the game-update counter before forwarding the hook so input edges
        // observed during this update are attributed to the current update, not the
        // previous one.
        FrameGuardTelemetry::nextFrame();
        PlayLayer::postUpdate(dt);
        if (!m_fields->overlay) {
            m_fields->overlay = FrameGuardOverlay::create();
            // Match QOLMod's layer placement: render in the game-layer coordinate
            // space, immediately below the UI, so geometry tracks level scrolling.
            if (m_fields->overlay)
                this->insertBefore(m_fields->overlay, m_uiLayer);
        }
        if (m_fields->overlay) m_fields->overlay->refresh(this);
        if (!m_fields->menu && m_uiLayer) {
            m_fields->menu = FrameGuardMenu::create();
            if (m_fields->menu) m_uiLayer->addChild(m_fields->menu, kOverlayZ);
        }
        if (m_fields->menu) {
            m_fields->menu->syncLabels();
            m_fields->menu->updateFPS(dt);
            m_fields->menu->updateFrameGuardHUD(dt);
        }
    }

    void resetLevel() {
        if (m_fields->overlay) m_fields->overlay->resetTrail();
        FrameGuardTelemetry::reset();
        PlayLayer::resetLevel();
    }
};

$on_mod(Loaded) {
    log::info("Frame-Guard v0.14.1 loaded: live collision geometry, actual player-box history, rectangle-based hitbox culling and eight-step trajectory estimate. Inputs are never delayed or automated.");
}

class $modify(FrameGuardPlayerObject, PlayerObject) {
    bool pushButton(PlayerButton button) {
        const bool result = PlayerObject::pushButton(button);
        if (button == PlayerButton::Jump)
            FrameGuardTelemetry::setHeld(this->m_isSecondPlayer, true);
        FrameGuardTelemetry::record(button, true);
        return result;
    }

    bool releaseButton(PlayerButton button) {
        const bool result = PlayerObject::releaseButton(button);
        if (button == PlayerButton::Jump)
            FrameGuardTelemetry::setHeld(this->m_isSecondPlayer, false);
        FrameGuardTelemetry::record(button, false);
        return result;
    }
};
