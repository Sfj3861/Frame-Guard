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
#include <utility>

using namespace geode::prelude;

namespace {
    constexpr int kOverlayZ = 9999;

    bool spanish() { return Mod::get()->getSettingValue<bool>("ui-spanish"); }
    char const* tr(char const* en, char const* es) { return spanish() ? es : en; }
    static float opacityFactor(char const* key) {
        return static_cast<float>(std::clamp(Mod::get()->getSettingValue<int>(key), 0, 100)) / 100.f;
    }
    std::string toggleText(char const* en, char const* es, bool value) {
        return fmt::format("{}: {}", tr(en, es), value ? "ON" : "OFF");
    }
    int effectiveDelayTicks(float fps) {
        const int milliseconds = std::clamp(Mod::get()->getSettingValue<int>("max-delay-ms"), 0, 200);
        if (!std::isfinite(fps) || fps <= 0.f) return 0;
        return std::max(0, static_cast<int>(std::floor(fps * milliseconds / 1000.f)));
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

    // The scheduler is deliberately opt-in until its heuristic has been tested in-game.
    // It only considers Jump edges and only defers when the simple forward collision
    // estimate says applying the edge now is riskier than preserving the current input.
    struct PendingInput {
        PlayerObject* player = nullptr;
        PlayerButton button = PlayerButton::Jump;
        bool pressed = false;
        std::uint64_t queuedAtFrame = 0;
        std::function<void()> apply;
    };
    static std::deque<PendingInput> g_pendingInputs;
    static PlayLayer* g_activePlayLayer = nullptr;
    static float g_effectiveFPS = 60.f;

    // Get a collision rectangle without leaving dirty/cache flags changed.
    // Shared by the rendering overlay and the opt-in input predictor.
    static cocos2d::CCRect safeCollisionRect(GameObject* obj) {
        if (!obj) return cocos2d::CCRect(0.f, 0.f, 0.f, 0.f);
        if (!obj->m_isObjectRectDirty)
            return obj->m_objectRect;
        const bool wasDirty = obj->m_isObjectRectDirty;
        const bool boxOffsetWasCalculated = obj->m_boxOffsetCalculated;
        const auto rect = obj->getObjectRect();
        obj->m_isObjectRectDirty = wasDirty;
        obj->m_boxOffsetCalculated = boxOffsetWasCalculated;
        return rect;
    }

    static int predictedCollisionFrame(PlayLayer* layer, PlayerObject* player, bool held, int horizon, bool jumpEdge = false) {
        if (!layer || !player || player->m_isDead || !layer->m_objects) return horizon + 1;
        auto pos = player->getPosition();
        float x = pos.x, y = pos.y;
        double vx = player->getCurrentXVelocity();
        double vy = player->getYVelocity();
        if (!std::isfinite(vx) || !std::isfinite(vy)) return horizon + 1;

        const float gravitySign = static_cast<float>(player->flipMod());
        const float gravity = static_cast<float>(player->m_gravity * player->m_gravityMod);
        const auto rect = player->getObjectRect(player->m_vehicleSize, player->m_vehicleSize);
        const float hw = std::max(2.f, rect.size.width * .5f);
        const float hh = std::max(2.f, rect.size.height * .5f);
        const float dt = 1.f;

        // Collect candidate collision rectangles once, rather than traversing every
        // level object on every simulated tick. This path only runs for the
        // opt-in experimental input scheduler.
        const float projectedEndX = x + static_cast<float>(vx * horizon);
        const float queryMinX = std::min(x, projectedEndX) - hw - 64.f;
        const float queryMaxX = std::max(x, projectedEndX) + hw + 64.f;
        auto* objects = layer->m_objects;
        std::vector<cocos2d::CCRect> obstacles;
        obstacles.reserve(std::min<std::size_t>(objects->count(), 128));
        for (unsigned int i = 0; i < objects->count(); ++i) {
            auto* obj = typeinfo_cast<GameObject*>(objects->objectAtIndex(i));
            if (!obj || obj->m_isDecoration || obj->m_isDecoration2 || obj->m_unk3ee) continue;
            const bool collidable = obj->m_objectType == GameObjectType::Hazard ||
                obj->m_objectType == GameObjectType::AnimatedHazard ||
                obj->m_objectType == GameObjectType::Solid ||
                (obj->m_objectType == GameObjectType::Slope && obj->m_slopeIsHazard);
            if (!collidable) continue;

            constexpr float broadPhasePad = 256.f;
            if (obj->m_positionX < queryMinX - broadPhasePad ||
                obj->m_positionX > queryMaxX + broadPhasePad)
                continue;
            const auto obstacle = safeCollisionRect(obj);
            if (obstacle.getMaxX() < queryMinX || obstacle.getMinX() > queryMaxX) continue;
            obstacles.push_back(obstacle);
        }

        for (int tick = 1; tick <= horizon; ++tick) {
            if (tick == 1 && jumpEdge && !player->m_isShip && !player->m_isDart && !player->m_isSwing) {
                // Approximate the immediate vertical impulse from a fresh jump edge.
                // Mode-specific rings, ball/spider flips, and ground checks are not modeled.
                vy = 10.0 * gravitySign;
            } else if (player->m_isDart) vy = held ? 6.0 : -6.0;
            else if (player->m_isShip || player->m_isSwing)
                vy += (held ? .72 : -.72) * gravitySign;
            else {
                vy -= gravity * gravitySign * .6;
                vy = std::clamp(vy, -15.0, 15.0);
            }
            x += static_cast<float>(vx * dt);
            y += static_cast<float>(vy * dt);
            cocos2d::CCRect box(x - hw, y - hh, hw * 2.f, hh * 2.f);
            for (auto const& obstacle : obstacles) {
                if (box.intersectsRect(obstacle)) return tick;
            }
            if (x > pos.x + 900.f || y < -200.f || y > 1400.f) break;
        }
        return horizon + 1;
    }

    static bool hasPendingInputFor(PlayerObject* player) {
        return std::any_of(g_pendingInputs.begin(), g_pendingInputs.end(),
            [player](PendingInput const& input) { return input.player == player; });
    }

    static bool shouldWaitForInput(PlayLayer* layer, PendingInput const& input) {
        if (!Mod::get()->getSettingValue<bool>("input-scheduler-enabled") ||
            !Mod::get()->getSettingValue<bool>("frameguard-enabled") ||
            input.button != PlayerButton::Jump || !input.player || input.player->m_isDead)
            return false;
        const int budget = effectiveDelayTicks(g_effectiveFPS);
        if (budget <= 0 || static_cast<int>(FrameGuardTelemetry::frame - input.queuedAtFrame) >= budget)
            return false;
        const bool currentHeld = FrameGuardTelemetry::isHeld(input.player->m_isSecondPlayer);
        if (currentHeld == input.pressed) return false;
        const int horizon = std::min(12, budget + 2);
        const int currentRisk = predictedCollisionFrame(layer, input.player, currentHeld, horizon);
        const int changedRisk = predictedCollisionFrame(layer, input.player, input.pressed, horizon,
            input.pressed && !currentHeld);
        // If the unchanged trajectory is already about to collide, never wait.
        // Otherwise defer one frame only when the requested edge introduces an
        // earlier predicted collision; the decision is recalculated next frame.
        return currentRisk > 1 && changedRisk + 1 < currentRisk;
    }

    static void processScheduledInputs(PlayLayer* layer, float dt) {
        g_activePlayLayer = layer;
        if (std::isfinite(dt) && dt > 0.0001f) g_effectiveFPS = 1.f / dt;

        if (g_pendingInputs.empty()) return;

        // Geometry Dash supports two live player objects at most. Use a fixed-size
        // buffer so the normal update path never allocates memory for this bookkeeping.
        std::array<PlayerObject*, 2> appliedThisFrame{};
        std::size_t appliedCount = 0;

        // Preserve the edge sequence: do not deliver a press and its release in
        // the same update, or a short tap can be swallowed by the game.
        for (std::size_t i = 0; i < g_pendingInputs.size();) {
            auto& item = g_pendingInputs[i];
            auto appliedEnd = appliedThisFrame.begin() + static_cast<std::ptrdiff_t>(appliedCount);
            bool blockedByEarlierEdge =
                std::find(appliedThisFrame.begin(), appliedEnd, item.player) != appliedEnd;

            for (std::size_t j = 0; !blockedByEarlierEdge && j < i; ++j) {
                if (g_pendingInputs[j].player == item.player) {
                    blockedByEarlierEdge = true;
                    break;
                }
            }

            // A deferred edge must never be replayed after the player has died.
            // The layer owns the player lifetime; onExit/resetLevel also clear this queue.
            if (!item.player || item.player->m_isDead) {
                g_pendingInputs.erase(g_pendingInputs.begin() + static_cast<std::ptrdiff_t>(i));
                continue;
            }
            if (blockedByEarlierEdge || shouldWaitForInput(layer, item)) {
                ++i;
                continue;
            }

            auto* appliedPlayer = item.player;
            auto apply = std::move(item.apply);
            g_pendingInputs.erase(g_pendingInputs.begin() + static_cast<std::ptrdiff_t>(i));
            if (apply) {
                apply();
                // With at most two live players, this cannot exceed the fixed buffer.
                if (appliedCount < appliedThisFrame.size())
                    appliedThisFrame[appliedCount++] = appliedPlayer;
            }
        }
    }

    // If a release arrives while a press is queued, flush that player's queued
    // edges in order before allowing the release through. This prevents a delayed
    // press from firing after the finger has already been lifted (stuck/late jump).
    static void flushPendingInputsFor(PlayerObject* player) {
        if (!player) return;
        for (std::size_t i = 0; i < g_pendingInputs.size();) {
            auto& item = g_pendingInputs[i];
            if (item.player != player) { ++i; continue; }
            auto apply = std::move(item.apply);
            g_pendingInputs.erase(g_pendingInputs.begin() + static_cast<std::ptrdiff_t>(i));
            if (apply) apply();
        }
    }

    // v0.15.1: diagnostics are opt-in so normal play remains uncluttered.
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
            this->setPosition(cocos2d::CCPointZero);
            this->setID("frame-guard-overlay");
            return true;
        }

        void invalidateObjectCache() {
            // Level resets can replace the backing array while keeping the same count.
            m_collisionObjects.clear();
            m_cachedObjectEntries.clear();
            m_cachedObjectArray = nullptr;
            m_cachedObjectCount = std::numeric_limits<unsigned int>::max();
            m_cachedAtFrame = 0;
        }

        void refresh(PlayLayer* layer) {
            if (!layer || !m_draw) return;
            // Mobile-first workload cap: retain the previous overlay on alternate
            // frames. Cache integrity checks still run on every rendered refresh.
            if (Mod::get()->getSettingValue<bool>("mobile-performance-mode") &&
                (FrameGuardTelemetry::frame % 2u) != 0u) return;
            m_draw->clear();
            const int opacity = std::clamp(Mod::get()->getSettingValue<int>("overlay-opacity"), 0, 100);
            m_draw->setOpacity(static_cast<GLubyte>(opacity * 255 / 100));

            const bool showHitboxes = Mod::get()->getSettingValue<bool>("show-hitboxes");
            const bool showPlayerBounds = Mod::get()->getSettingValue<bool>("show-player-bounds");
            const bool showTrajectory = Mod::get()->getSettingValue<bool>("show-trajectory");

            cocos2d::CCRect viewportBounds;
            const bool needsObjectDiagnostics = showHitboxes || showTrajectory;
            const bool haveViewportBounds =
                needsObjectDiagnostics && getViewportBounds(viewportBounds, layer);

            if (needsObjectDiagnostics && layer->m_objects) {
                // Rebuild on array/count changes and periodically for levels that
                // add or replace objects dynamically without changing the array size.
                auto* objectArray = layer->m_objects;
                const auto count = objectArray->count();
                bool pointersChanged =
                    objectArray != m_cachedObjectArray ||
                    count != m_cachedObjectCount ||
                    m_cachedObjectEntries.size() != m_collisionObjects.size();
                // Detect object replacement even if the array/count stay identical.
                // Compare pointers at their old indexes before dereferencing the cached
                // pointers themselves, avoiding stale GameObject references.
                if (!pointersChanged) {
                    for (auto const& entry : m_cachedObjectEntries) {
                        if (entry.first >= count ||
                            typeinfo_cast<GameObject*>(objectArray->objectAtIndex(entry.first)) != entry.second) {
                            pointersChanged = true;
                            break;
                        }
                    }
                }
                const bool refreshCache =
                    pointersChanged || FrameGuardTelemetry::frame - m_cachedAtFrame >= 30;
                if (refreshCache) {
                    m_collisionObjects.clear();
                    m_cachedObjectEntries.clear();
                    m_collisionObjects.reserve(count);
                    m_cachedObjectEntries.reserve(count);
                    for (unsigned int i = 0; i < count; ++i) {
                        auto* object = typeinfo_cast<GameObject*>(objectArray->objectAtIndex(i));
                        if (object && isCollisionObject(object)) {
                            m_collisionObjects.push_back(object);
                            m_cachedObjectEntries.emplace_back(i, object);
                        }
                    }
                    m_cachedObjectArray = objectArray;
                    m_cachedObjectCount = count;
                    m_cachedAtFrame = FrameGuardTelemetry::frame;
                }
                if (showHitboxes) {
                    for (auto* object : m_collisionObjects) {
                        if (!object) continue;

                        // Reject distant objects before asking the game to calculate
                        // potentially dirty collision rectangles. The visible bounds
                        // are converted into this overlay's local coordinates so camera
                        // and object-layer transforms are included.
                        if (haveViewportBounds) {
                            const float radius = object->m_objectRadius > 0.f
                                ? object->m_objectRadius * std::max<float>(object->m_scaleX, object->m_scaleY)
                                : 0.f;
                            constexpr float pad = 512.f;
                            if (object->m_positionX + radius < viewportBounds.getMinX() - pad ||
                                object->m_positionX - radius > viewportBounds.getMaxX() + pad ||
                                object->m_positionY + radius < viewportBounds.getMinY() - pad ||
                                object->m_positionY - radius > viewportBounds.getMaxY() + pad)
                                continue;
                        }

                        const auto rect = collisionRect(object);
                        if (haveViewportBounds && object->m_objectRadius <= 0.f) {
                            constexpr float pad = 96.f;
                            if (rect.getMaxX() < viewportBounds.getMinX() - pad ||
                                rect.getMinX() > viewportBounds.getMaxX() + pad ||
                                rect.getMaxY() < viewportBounds.getMinY() - pad ||
                                rect.getMinY() > viewportBounds.getMaxY() + pad)
                                continue;
                        }
                        if (object->m_objectRadius > 0.f ||
                            (rect.size.width > 0.f && rect.size.height > 0.f))
                            drawObjectGeometry(object, rect);
                    }
                }
            }

            // Project a short future path from the current velocity and held input.
            // This is a lightweight approximation: it does not execute the game's
            // collision/trigger system, so portals and future contacts are not predicted.
            if (showTrajectory && layer->m_player1) {
                const bool held1 = FrameGuardTelemetry::isHeld(false);
                drawPrediction(layer->m_player1, held1,
                    cocos2d::ccc4f(0.10f, 0.82f, 1.0f, 1.0f), false);
                // Alternative path predicts the opposite input state at this frame.
                drawPrediction(layer->m_player1, !held1,
                    cocos2d::ccc4f(0.72f, 0.38f, 1.0f, 1.0f), true);
                if (layer->m_player2 && layer->m_player2->isRunning()) {
                    const bool held2 = FrameGuardTelemetry::isHeld(true);
                    drawPrediction(layer->m_player2, held2,
                        cocos2d::ccc4f(0.10f, 0.82f, 1.0f, 1.0f), false);
                    drawPrediction(layer->m_player2, !held2,
                        cocos2d::ccc4f(0.72f, 0.38f, 1.0f, 1.0f), true);
                }
            }

            if (showPlayerBounds) {
                if (layer->m_player1) drawPlayerGeometry(layer->m_player1);
                if (layer->m_player2 && layer->m_player2->isRunning())
                    drawPlayerGeometry(layer->m_player2);
            }
        }

    private:
        bool getViewportBounds(cocos2d::CCRect& bounds, PlayLayer* layer) {
            auto* director = cocos2d::CCDirector::sharedDirector();
            if (!director) return false;

            const auto origin = director->getVisibleOrigin();
            const auto size = director->getVisibleSize();
            if (!std::isfinite(origin.x) || !std::isfinite(origin.y) ||
                !std::isfinite(size.width) || !std::isfinite(size.height) ||
                size.width <= 0.f || size.height <= 0.f)
                return false;

            // Convert all four window corners: this remains correct under scroll,
            // zoom, and transformed parents instead of assuming fixed screen coords.
            cocos2d::CCPoint corners[] = {
                this->convertToNodeSpace(origin),
                this->convertToNodeSpace(ccp(origin.x + size.width, origin.y)),
                this->convertToNodeSpace(ccp(origin.x, origin.y + size.height)),
                this->convertToNodeSpace(ccp(origin.x + size.width, origin.y + size.height))
            };
            float minX = corners[0].x, maxX = corners[0].x;
            float minY = corners[0].y, maxY = corners[0].y;
            for (auto const& point : corners) {
                if (!std::isfinite(point.x) || !std::isfinite(point.y)) return false;
                minX = std::min(minX, point.x);
                maxX = std::max(maxX, point.x);
                minY = std::min(minY, point.y);
                maxY = std::max(maxY, point.y);
            }
            if (maxX <= minX || maxY <= minY ||
                maxX - minX > 100000.f || maxY - minY > 100000.f)
                return false;

            // If the conversion doesn't seem to include an active player's current
            // point (unusual viewport or camera setup), disable culling rather than
            // risk hiding every collision outline.
            bool hasLivePlayer = false;
            bool playerWithinBounds = false;
            auto testPlayer = [&](PlayerObject* player) {
                if (!player || player->m_isDead) return;
                hasLivePlayer = true;
                const auto pos = player->getPosition();
                constexpr float fallbackPad = 1600.f;
                if (pos.x >= minX - fallbackPad && pos.x <= maxX + fallbackPad &&
                    pos.y >= minY - fallbackPad && pos.y <= maxY + fallbackPad)
                    playerWithinBounds = true;
            };
            testPlayer(layer->m_player1);
            testPlayer(layer->m_player2);
            if (hasLivePlayer && !playerWithinBounds) return false;

            bounds = cocos2d::CCRect(minX, minY, maxX - minX, maxY - minY);
            return true;
        }

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

        void drawPrediction(PlayerObject* player, bool held, cocos2d::ccColor4F colour, bool alternative) {
            if (!player || player->m_isDead) return;

            constexpr int iterations = 72;
            constexpr float step = 0.5f;
            constexpr int boxEvery = 8;
            auto position = player->getPosition();
            float x = position.x;
            float y = position.y;
            double xVelocity = player->getCurrentXVelocity();
            double yVelocity = player->getYVelocity();
            const float gravitySign = static_cast<float>(player->flipMod());
            const float gravity = static_cast<float>(player->m_gravity * player->m_gravityMod);
            const auto currentRect = player->getObjectRect(player->m_vehicleSize, player->m_vehicleSize);
            const float halfWidth = std::max(2.0f, currentRect.size.width * 0.5f);
            const float halfHeight = std::max(2.0f, currentRect.size.height * 0.5f);
            const int marginTicks = std::clamp(
                Mod::get()->getSettingValue<int>("safe-margin-ticks"), 0, 12);
            const float safetyPad = static_cast<float>(marginTicks) * 1.25f;
            // Cache only collision geometry inside the predicted horizontal span.
            // The previous implementation tested every level object at every one of
            // the 120 prediction steps, multiplying work on long/high-object-count levels.
            // Reuse scratch storage across predictions instead of allocating a vector
            // for every cyan/lilac path on every refresh.
            m_nearbyObjects.clear();
            auto& nearbyObjects = m_nearbyObjects;
            if (nearbyObjects.capacity() < std::min<std::size_t>(m_collisionObjects.size(), 128))
                nearbyObjects.reserve(std::min<std::size_t>(m_collisionObjects.size(), 128));
            const float predictedEndX = x + static_cast<float>(xVelocity * step * iterations);
            const float queryMinX = std::min(x, predictedEndX) - halfWidth - 64.f;
            const float queryMaxX = std::max(x, predictedEndX) + halfWidth + 64.f;
            for (auto* object : m_collisionObjects) {
                if (!object || !typeIs(object, {
                    GameObjectType::Hazard, GameObjectType::AnimatedHazard,
                    GameObjectType::Solid, GameObjectType::Slope
                })) continue;
                // Most objects in a level are far away from the simulated segment.
                // Use position as a cheap first-stage check before rectangle calculation.
                constexpr float broadPhasePad = 192.f;
                if (object->m_positionX < queryMinX - broadPhasePad ||
                    object->m_positionX > queryMaxX + broadPhasePad) continue;
                const auto rect = collisionRect(object);
                if (rect.getMaxX() < queryMinX || rect.getMinX() > queryMaxX) continue;
                nearbyObjects.emplace_back(object, rect);
            }

            cocos2d::CCPoint previous = ccp(x, y);
            const float pathOpacity = opacityFactor(alternative ? "alternative-trajectory-opacity" : "trajectory-opacity");

            for (int i = 1; i <= iterations; ++i) {
                if (player->m_isDart) {
                    // Wave-like movement changes vertical direction directly with input.
                    yVelocity = held ? 6.0 : -6.0;
                } else if (player->m_isShip || player->m_isBird || player->m_isSwing) {
                    // Flight modes accelerate up while held and down while released.
                    const double thrust = held ? 0.72 : -0.72;
                    yVelocity += thrust * gravitySign * step;
                } else {
                    // Approximate variable-height cube/robot jumps: continuing to hold
                    // jump reduces gravity while the player is still rising. This makes
                    // the cyan and lilac alternatives diverge for ordinary jump modes.
                    // Ball/spider flips and UFO impulses are discrete events and are not
                    // modeled accurately by this lightweight preview.
                    const bool variableJumpMode = !player->m_isBall && !player->m_isSpider &&
                        !player->m_isBird && !player->m_isShip && !player->m_isDart &&
                        !player->m_isSwing;
                    const bool rising = yVelocity * gravitySign > 0.0;
                    const double gravityScale = variableJumpMode && held && rising ? 0.42 : 1.0;
                    yVelocity -= gravity * gravitySign * 0.6 * step * gravityScale;
                    yVelocity = std::clamp(yVelocity, -15.0, 15.0);
                }

                x += static_cast<float>(xVelocity * step);
                y += static_cast<float>(yVelocity * step);
                const auto current = ccp(x, y);
                cocos2d::CCRect projectedBox(
                    x - halfWidth - safetyPad, y - halfHeight - safetyPad,
                    halfWidth * 2.f + safetyPad * 2.f, halfHeight * 2.f + safetyPad * 2.f);
                bool unsafe = false;
                for (auto const& candidate : nearbyObjects) {
                    if (projectedBox.intersectsRect(candidate.second)) {
                        unsafe = true;
                        break;
                    }
                }
                auto segmentColour = unsafe
                    ? cocos2d::ccc4f(1.f, 0.12f, 0.12f, colour.a)
                    : colour;
                segmentColour.a = 0.72f * (1.0f - static_cast<float>(i) /
                    static_cast<float>(iterations) * 0.55f) * pathOpacity;
                m_draw->drawSegment(previous, current, 1.15f, segmentColour);
                previous = current;

                if (i % boxEvery == 0) {
                    cocos2d::CCPoint box[] = {
                        ccp(x - halfWidth, y - halfHeight), ccp(x + halfWidth, y - halfHeight),
                        ccp(x + halfWidth, y + halfHeight), ccp(x - halfWidth, y + halfHeight)
                    };
                    auto boxColour = colour;
                    boxColour.a = (0.10f + 0.20f * (1.0f - static_cast<float>(i) /
                        static_cast<float>(iterations))) * pathOpacity;
                    m_draw->drawPolygon(box, 4, cocos2d::ccc4f(0.f, 0.f, 0.f, 0.f),
                        0.65f, boxColour, cocos2d::BorderAlignment::Inside);
                }

                if (unsafe || x > position.x + 900.f || y < -200.f || y > 1400.f) break;
            }
        }

        static cocos2d::CCRect collisionRect(GameObject* obj) {
            return safeCollisionRect(obj);
        }

        void drawObjectGeometry(GameObject* obj, cocos2d::CCRect const& rect) {
            auto colour = colourFor(obj);
            colour.a *= opacityFactor("hitbox-opacity");
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
            const float alpha = opacityFactor("player-bounds-opacity");
            const auto normal = cocos2d::ccc4f(1.0f, 0.12f, 0.12f, 0.98f * alpha);
            const auto rotated = cocos2d::ccc4f(0.95f, 0.05f, 0.48f, 0.98f * alpha);
            const auto mini = cocos2d::ccc4f(0.12f, 0.95f, 1.0f, 0.98f * alpha);
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
        std::vector<std::pair<GameObject*, cocos2d::CCRect>> m_nearbyObjects;
        cocos2d::CCArray* m_cachedObjectArray = nullptr;
        std::vector<GameObject*> m_collisionObjects;
        std::vector<std::pair<unsigned int, GameObject*>> m_cachedObjectEntries;
        unsigned int m_cachedObjectCount = std::numeric_limits<unsigned int>::max();
        std::uint64_t m_cachedAtFrame = 0;
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
        void setCaption(char const* title, char const* unit) {
            const std::string nextTitle = title ? title : "";
            const std::string nextUnit = unit ? unit : "";
            if (m_title == nextTitle && m_unit == nextUnit) return;
            m_title = nextTitle;
            m_unit = nextUnit;
            redraw();
        }
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
            // Let CCMenu own taps on the floating FG button. A competing targeted
            // delegate on this parent layer swallowed taps before the menu could act.
            auto* director = cocos2d::CCDirector::sharedDirector();
            const auto size = director->getWinSize();

            m_menu = cocos2d::CCMenu::create();
            if (!m_menu) return false;
            m_menu->setPosition(cocos2d::CCPointZero);
            auto* panel = cocos2d::CCDrawNode::create();
            if (panel) {
                const float panelW = std::min(size.width - 20.f, 620.f);
                const float panelH = std::min(size.height - 24.f, 300.f);
                cocos2d::CCPoint corners[] = { ccp(10.f, 12.f), ccp(10.f + panelW, 12.f), ccp(10.f + panelW, 12.f + panelH), ccp(10.f, 12.f + panelH) };
                panel->drawPolygon(corners, 4, cocos2d::ccc4f(0.08f, 0.13f, 0.34f, 0.96f), 3.f, cocos2d::ccc4f(0.45f, 0.78f, 1.f, 1.f));
                panel->setVisible(false);
                this->addChild(panel, 0);
                m_panel = panel;
            }
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
            m_schedulerButton = makeItem("", menu_selector(FrameGuardMenu::onToggleScheduler));
            m_exportButton = makeItem("Export CSV", menu_selector(FrameGuardMenu::onExportCSV));
            m_infoButton = makeItem("", menu_selector(FrameGuardMenu::onToggleInfo));
            m_hudButton = makeItem("", menu_selector(FrameGuardMenu::onToggleHUD));
            m_languageButton = makeItem("", menu_selector(FrameGuardMenu::onToggleLanguage));
            m_opacityTargetButton = makeItem("", menu_selector(FrameGuardMenu::onCycleOpacityTarget));
            m_opacitySlider = FrameGuardSlider::create("Hitbox opacity", "%", 0, 100,
                Mod::get()->getSettingValue<int>("hitbox-opacity"), [this](int value) {
                    Mod::get()->setSettingValue<int>(opacitySettingKey(), value);
                });
            m_marginSlider = FrameGuardSlider::create(tr("Safe margin", "Margen seguro"), " ticks", 0, 12,
                Mod::get()->getSettingValue<int>("safe-margin-ticks"), [](int value) {
                    Mod::get()->setSettingValue<int>("safe-margin-ticks", value);
                });
            m_delaySlider = FrameGuardSlider::create(tr("Delay budget", "Límite de espera"), " ms", 0, 200,
                Mod::get()->getSettingValue<int>("max-delay-ms"), [](int value) {
                    Mod::get()->setSettingValue<int>("max-delay-ms", value);
                });
            if (!m_openButton || !m_hitboxButton || !m_playerButton || !m_trailButton ||
                !m_fpsButton || !m_guardButton || !m_schedulerButton || !m_exportButton || !m_infoButton ||
                !m_opacitySlider || !m_opacityTargetButton || !m_marginSlider || !m_hudButton || !m_languageButton || !m_delaySlider) return false;
            this->addEventListener(
                KeybindSettingPressedEventV3(Mod::get(), "toggle-menu"),
                [this](Keybind const&, bool down, bool repeat, double) {
                    if (down && !repeat) this->toggleMenu();
                }
            );

            // Use a compact two-column layout so the complete menu remains reachable
            // on the typical 480x320 landscape design size used by Geometry Dash.
            const float leftX = std::clamp(size.width * 0.27f, 100.f, 136.f);
            const float rightX = std::clamp(size.width * 0.69f, 236.f, 344.f);
            const float sliderX = std::clamp(size.width * 0.43f, 180.f, 220.f);
            const float rowTop = size.height - 64.f;
            constexpr float rowStep = 24.f;
            m_openButton->setPosition(ccp(42.f, size.height - 34.f));
            m_hitboxButton->setPosition(ccp(leftX, rowTop));
            m_playerButton->setPosition(ccp(rightX, rowTop));
            m_trailButton->setPosition(ccp(leftX, rowTop - rowStep));
            m_fpsButton->setPosition(ccp(rightX, rowTop - rowStep));
            m_guardButton->setPosition(ccp(leftX, rowTop - rowStep * 2.f));
            m_schedulerButton->setPosition(ccp(rightX, rowTop - rowStep * 2.f));
            m_exportButton->setPosition(ccp(leftX, rowTop - rowStep * 3.f));
            m_infoButton->setPosition(ccp(rightX, rowTop - rowStep * 3.f));
            m_hudButton->setPosition(ccp(leftX, rowTop - rowStep * 4.f));
            m_languageButton->setPosition(ccp(rightX, rowTop - rowStep * 4.f));
            m_opacityTargetButton->setPosition(ccp(std::clamp(size.width * 0.18f, 54.f, 92.f), size.height - 200.f));
            m_opacitySlider->setPosition(ccp(sliderX, size.height - 200.f));
            m_marginSlider->setPosition(ccp(sliderX, size.height - 236.f));
            m_delaySlider->setPosition(ccp(sliderX, size.height - 272.f));
            m_menu->addChild(m_openButton);
            m_menu->addChild(m_hitboxButton);
            m_menu->addChild(m_playerButton);
            m_menu->addChild(m_trailButton);
            m_menu->addChild(m_fpsButton);
            m_menu->addChild(m_guardButton);
            m_menu->addChild(m_schedulerButton);
            m_menu->addChild(m_exportButton);
            m_menu->addChild(m_infoButton);
            m_menu->addChild(m_hudButton);
            m_menu->addChild(m_languageButton);
            m_menu->addChild(m_opacityTargetButton);
            m_menu->addChild(m_opacitySlider);
            m_menu->addChild(m_marginSlider);
            m_menu->addChild(m_delaySlider);
            m_hitboxButton->setVisible(false);
            m_playerButton->setVisible(false);
            m_trailButton->setVisible(false);
            m_fpsButton->setVisible(false);
            m_guardButton->setVisible(false);
            m_schedulerButton->setVisible(false);
            m_exportButton->setVisible(false);
            m_infoButton->setVisible(false);
            m_hudButton->setVisible(false);
            m_languageButton->setVisible(false);
            m_opacityTargetButton->setVisible(false);
            m_opacitySlider->setVisible(false);
            m_marginSlider->setVisible(false);
            m_delaySlider->setVisible(false);

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
                m_guardLabel->setVisible((Mod::get()->getSettingValue<bool>("show-info-text") || Mod::get()->getSettingValue<bool>("debug-mode")) && Mod::get()->getSettingValue<bool>("show-frameguard-hud"));
                this->addChild(m_guardLabel, 2);
            }
            syncLabels();
            this->setID("frame-guard-in-game-menu");
            return true;
        }

        void toggleMenu() {
            m_open = !m_open;
            if (m_panel) m_panel->setVisible(m_open);
            m_hitboxButton->setVisible(m_open);
            m_playerButton->setVisible(m_open);
            m_trailButton->setVisible(m_open);
            m_fpsButton->setVisible(m_open);
            m_guardButton->setVisible(m_open);
            m_schedulerButton->setVisible(m_open);
            m_exportButton->setVisible(m_open);
            m_infoButton->setVisible(m_open);
            m_hudButton->setVisible(m_open);
            m_languageButton->setVisible(m_open);
            m_opacityTargetButton->setVisible(m_open);
            m_opacitySlider->setVisible(m_open);
            m_marginSlider->setVisible(m_open);
            m_delaySlider->setVisible(m_open);
            syncLabels();
        }

        void syncLabels() {
            if (m_hitboxButton)
                static_cast<cocos2d::CCLabelBMFont*>(m_hitboxButton->getLabel())->setString(
                    toggleText("Hitboxes", "Cajas de colisión", Mod::get()->getSettingValue<bool>("show-hitboxes")).c_str());
            if (m_playerButton)
                static_cast<cocos2d::CCLabelBMFont*>(m_playerButton->getLabel())->setString(
                    toggleText("Player bounds", "Límites del jugador", Mod::get()->getSettingValue<bool>("show-player-bounds")).c_str());
            if (m_trailButton)
                static_cast<cocos2d::CCLabelBMFont*>(m_trailButton->getLabel())->setString(
                    toggleText("Trajectory", "Trayectoria", Mod::get()->getSettingValue<bool>("show-trajectory")).c_str());
            if (m_fpsButton)
                static_cast<cocos2d::CCLabelBMFont*>(m_fpsButton->getLabel())->setString(
                    toggleText("FPS", "FPS", Mod::get()->getSettingValue<bool>("show-fps")).c_str());
            const bool showInfo = Mod::get()->getSettingValue<bool>("show-info-text");
            if (m_fpsLabel) m_fpsLabel->setVisible(showInfo && Mod::get()->getSettingValue<bool>("show-fps"));
            if (m_guardLabel) m_guardLabel->setVisible((showInfo || Mod::get()->getSettingValue<bool>("debug-mode")) && Mod::get()->getSettingValue<bool>("show-frameguard-hud"));
            if (m_infoButton)
                static_cast<cocos2d::CCLabelBMFont*>(m_infoButton->getLabel())->setString(
                    toggleText("Debug mode", "Modo debug", Mod::get()->getSettingValue<bool>("debug-mode")).c_str());
            if (m_opacityTargetButton)
                static_cast<cocos2d::CCLabelBMFont*>(m_opacityTargetButton->getLabel())->setString(
                    fmt::format("{}: {}", tr("Opacity target", "Opacidad de"), opacitySettingName()).c_str());
            if (m_opacitySlider) {
                m_opacitySlider->setCaption(opacitySettingName().c_str(), "%");
                m_opacitySlider->setValue(Mod::get()->getSettingValue<int>(opacitySettingKey()));
            }
            if (m_marginSlider) {
                m_marginSlider->setCaption(tr("Safe margin", "Margen seguro"), " ticks");
                m_marginSlider->setValue(Mod::get()->getSettingValue<int>("safe-margin-ticks"));
            }
            if (m_delaySlider) {
                m_delaySlider->setCaption(tr("Delay budget", "Límite de espera"), " ms");
                m_delaySlider->setValue(Mod::get()->getSettingValue<int>("max-delay-ms"));
            }
            if (m_exportButton)
                static_cast<cocos2d::CCLabelBMFont*>(m_exportButton->getLabel())->setString(
                    tr("Export CSV", "Exportar CSV"));
            if (m_hudButton)
                static_cast<cocos2d::CCLabelBMFont*>(m_hudButton->getLabel())->setString(
                    toggleText("FrameGuard HUD", "HUD de FrameGuard", Mod::get()->getSettingValue<bool>("show-frameguard-hud")).c_str());
            if (m_languageButton)
                static_cast<cocos2d::CCLabelBMFont*>(m_languageButton->getLabel())->setString(
                    fmt::format("{}: {}", tr("Language", "Idioma"), tr("English", "Español")).c_str());
            if (m_guardButton)
                static_cast<cocos2d::CCLabelBMFont*>(m_guardButton->getLabel())->setString(
                    toggleText("FrameGuard", "FrameGuard", Mod::get()->getSettingValue<bool>("frameguard-enabled")).c_str());
            if (m_schedulerButton)
                static_cast<cocos2d::CCLabelBMFont*>(m_schedulerButton->getLabel())->setString(
                    toggleText("Smart input delay", "Espera inteligente", Mod::get()->getSettingValue<bool>("input-scheduler-enabled")).c_str());
            if (m_guardLabel)
                m_guardLabel->setVisible(showInfo && Mod::get()->getSettingValue<bool>("show-frameguard-hud"));
        }

        void updateFrameGuardHUD(float dt) {
            if (!m_guardLabel || !Mod::get()->getSettingValue<bool>("show-frameguard-hud")) return;
            const bool debug = Mod::get()->getSettingValue<bool>("debug-mode");
            if (!debug && !Mod::get()->getSettingValue<bool>("show-info-text")) return;
            m_guardElapsed += dt;
            if (m_guardElapsed >= (debug ? 0.10f : 0.25f)) {
                auto text = FrameGuardTelemetry::summary();
                if (debug) {
                    auto* layer = PlayLayer::get();
                    const auto fps = dt > 0.f ? static_cast<int>(1.f / dt + 0.5f) : 0;
                    text = fmt::format("DEBUG | FPS:{} | P1:{} | P2:{} | objects:{} | scheduler:{} | {}",
                        fps, layer && layer->m_player1 ? "OK" : "NULL",
                        layer && layer->m_player2 && layer->m_player2->isRunning() ? "ACTIVE" : "OFF",
                        layer && layer->m_objects ? layer->m_objects->count() : 0,
                        Mod::get()->getSettingValue<bool>("input-scheduler-enabled") ? "ON" : "OFF", text);
                }
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
                const int delayTicks = effectiveDelayTicks(static_cast<float>(fps));
                auto text = fmt::format("{}: {} | {}: {} {}", tr("FPS", "FPS"), fps,
                    tr("Max delay", "Espera máx."), delayTicks, tr("ticks", "ticks"));
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
        char const* opacitySettingKey() const {
            static constexpr char const* keys[] = { "hitbox-opacity", "player-bounds-opacity", "trajectory-opacity", "alternative-trajectory-opacity" };
            return keys[m_opacityTarget % 4];
        }
        std::string opacitySettingName() const {
            static constexpr char const* en[] = { "Hitboxes", "Player bounds", "Cyan path", "Lilac path" };
            static constexpr char const* es[] = { "Hitboxes", "Límites jugador", "Trayectoria cian", "Trayectoria lila" };
            return spanish() ? es[m_opacityTarget % 4] : en[m_opacityTarget % 4];
        }
        void onCycleOpacityTarget(cocos2d::CCObject*) {
            m_opacityTarget = (m_opacityTarget + 1) % 4;
            syncLabels();
        }
        void onToggleMenu(cocos2d::CCObject*) { toggleMenu(); }
        void onToggleLanguage(cocos2d::CCObject*) {
            Mod::get()->setSettingValue<bool>("ui-spanish", !Mod::get()->getSettingValue<bool>("ui-spanish"));
            syncLabels();
        }
        void onToggleHUD(cocos2d::CCObject*) {
            Mod::get()->setSettingValue<bool>("show-frameguard-hud",
                !Mod::get()->getSettingValue<bool>("show-frameguard-hud"));
            syncLabels();
        }
        void onToggleInfo(cocos2d::CCObject*) {
            Mod::get()->setSettingValue<bool>("debug-mode",
                !Mod::get()->getSettingValue<bool>("debug-mode"));
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
        void onToggleScheduler(cocos2d::CCObject*) {
            Mod::get()->setSettingValue<bool>("input-scheduler-enabled",
                !Mod::get()->getSettingValue<bool>("input-scheduler-enabled"));
            syncLabels();
        }
        void onExportCSV(cocos2d::CCObject*) {
            if (FrameGuardTelemetry::exportCSV()) {
                log::info("FrameGuard: exported recent input events to {}",
                    (Mod::get()->getSaveDir() / "frameguard-inputs.csv").string());
                if (m_guardLabel) m_guardLabel->setString(tr("FrameGuard: CSV exported (Geode mod save folder)", "FrameGuard: CSV exportado (carpeta del mod de Geode)"));
            } else {
                log::warn("FrameGuard: failed to export input events CSV");
                if (m_guardLabel) m_guardLabel->setString(tr("FrameGuard: CSV export failed", "FrameGuard: fallo al exportar CSV"));
            }
        }

        cocos2d::CCMenu* m_menu = nullptr;
        cocos2d::CCDrawNode* m_panel = nullptr;
        CCMenuItemSpriteExtra* m_openButton = nullptr;
        cocos2d::CCMenuItemLabel* m_hitboxButton = nullptr;
        cocos2d::CCMenuItemLabel* m_playerButton = nullptr;
        cocos2d::CCMenuItemLabel* m_trailButton = nullptr;
        cocos2d::CCMenuItemLabel* m_fpsButton = nullptr;
        cocos2d::CCMenuItemLabel* m_guardButton = nullptr;
        cocos2d::CCMenuItemLabel* m_schedulerButton = nullptr;
        cocos2d::CCMenuItemLabel* m_exportButton = nullptr;
        cocos2d::CCMenuItemLabel* m_infoButton = nullptr;
        FrameGuardSlider* m_opacitySlider = nullptr;
        FrameGuardSlider* m_marginSlider = nullptr;
        FrameGuardSlider* m_delaySlider = nullptr;
        cocos2d::CCMenuItemLabel* m_hudButton = nullptr;
        cocos2d::CCMenuItemLabel* m_languageButton = nullptr;
        cocos2d::CCMenuItemLabel* m_opacityTargetButton = nullptr;
        int m_opacityTarget = 0;
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
        PlayLayer::postUpdate(dt);
        FrameGuardTelemetry::nextFrame();
        processScheduledInputs(this, dt);
        if (!m_fields->overlay && m_objectLayer) {
            m_fields->overlay = FrameGuardOverlay::create();
            // GameObject positions and collision rectangles are expressed in the
            // object-layer coordinate space. Parenting the overlay to PlayLayer itself
            // makes it drift relative to scrolling/zoom transforms on some devices.
            // Keep drawing in the same transformed layer as the level objects.
            if (m_fields->overlay)
                m_objectLayer->addChild(m_fields->overlay, 9999);
        }
        if (m_fields->overlay) m_fields->overlay->refresh(this);
        if (!m_fields->menu && m_uiLayer) {
            m_fields->menu = FrameGuardMenu::create();
            if (m_fields->menu) m_uiLayer->addChild(m_fields->menu, kOverlayZ);
        }
        if (m_fields->menu) {
            // Menu callbacks sync immediately; poll less often for changes made
            // through Geode's settings popup to avoid rebuilding every label each frame.
            if (FrameGuardTelemetry::frame % 30 == 0)
                m_fields->menu->syncLabels();
            m_fields->menu->updateFPS(dt);
            m_fields->menu->updateFrameGuardHUD(dt);
        }
    }

    void resetLevel() {
        // Drop deferred callbacks before the game resets player/level state.
        g_pendingInputs.clear();
        if (m_fields->overlay) m_fields->overlay->invalidateObjectCache();
        FrameGuardTelemetry::reset();
        g_activePlayLayer = this;
        PlayLayer::resetLevel();
    }

    void onExit() {
        // Avoid retaining PlayerObject pointers or scheduling inputs after leaving a level.
        if (g_activePlayLayer == this) {
            g_pendingInputs.clear();
            g_activePlayLayer = nullptr;
        }
        PlayLayer::onExit();
    }
};

$on_mod(Loaded) {
    log::info("Frame-Guard loaded: collision geometry, player bounds, trajectory preview, telemetry and in-game controls.");
}

class $modify(FrameGuardPlayerObject, PlayerObject) {
    bool pushButton(PlayerButton button) {
        if (Mod::get()->getSettingValue<bool>("input-scheduler-enabled") &&
            Mod::get()->getSettingValue<bool>("frameguard-enabled") &&
            button == PlayerButton::Jump && g_activePlayLayer &&
            effectiveDelayTicks(g_effectiveFPS) > 0) {
            // Avoid accumulating duplicate press edges for a player already queued.
            if (hasPendingInputFor(this)) return true;
            PendingInput pending;
            pending.player = this;
            pending.button = button;
            pending.pressed = true;
            pending.queuedAtFrame = FrameGuardTelemetry::frame;
            pending.apply = [this, button]() {
                PlayerObject::pushButton(button);
                FrameGuardTelemetry::setHeld(this->m_isSecondPlayer, true);
                FrameGuardTelemetry::record(button, true);
            };
            if (hasPendingInputFor(this) || shouldWaitForInput(g_activePlayLayer, pending)) {
                g_pendingInputs.push_back(std::move(pending));
                return true;
            }
        }
        bool result = PlayerObject::pushButton(button);
        if (button == PlayerButton::Jump) FrameGuardTelemetry::setHeld(this->m_isSecondPlayer, true);
        FrameGuardTelemetry::record(button, true);
        return result;
    }

    bool releaseButton(PlayerButton button) {
        // Releases are never delayed by the predictor. If a press is still queued,
        // deliver it first, then release immediately in this same input callback.
        // This preserves edge order and avoids leaving a button logically held.
        if (button == PlayerButton::Jump && hasPendingInputFor(this))
            flushPendingInputsFor(this);
        bool result = PlayerObject::releaseButton(button);
        if (button == PlayerButton::Jump) FrameGuardTelemetry::setHeld(this->m_isSecondPlayer, false);
        FrameGuardTelemetry::record(button, false);
        return result;
    }
};
