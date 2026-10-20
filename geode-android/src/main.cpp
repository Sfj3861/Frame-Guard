#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <deque>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <vector>

using namespace geode::prelude;

namespace {
    constexpr int kOverlayZ = 9999;
    constexpr std::size_t kTrailLimit = 24;

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
        static std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();

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
            events.clear();
            started = std::chrono::steady_clock::now();
        }
        static std::string summary() {
            if (!Mod::get()->getSettingValue<bool>("frameguard-enabled")) return "FrameGuard OFF";
            if (lastButton < 0) return fmt::format("FrameGuard ON | frame {} | inputs 0", frame);
            return fmt::format("FrameGuard ON | frame {} | inputs {} | {} B{} @{} | delta {} us",
                frame, inputs, lastPressed ? "PRESS" : "RELEASE", lastButton, lastFrame, deltaMicros);
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
            this->setPosition(cocos2d::CCPointZero);
            this->setID("frame-guard-overlay");
            return true;
        }

        void resetTrail() { m_trail.clear(); }

        void refresh(PlayLayer* layer) {
            if (!layer || !m_draw) return;
            m_draw->clear();

            const bool showHitboxes = Mod::get()->getSettingValue<bool>("show-hitboxes");
            const bool showPlayerBounds = Mod::get()->getSettingValue<bool>("show-player-bounds");
            const bool showTrajectory = Mod::get()->getSettingValue<bool>("show-trajectory");

            if (showHitboxes && layer->m_objects) {
                for (unsigned int i = 0; i < layer->m_objects->count(); ++i) {
                    auto* object = typeinfo_cast<GameObject*>(layer->m_objects->objectAtIndex(i));
                    if (object && isCollisionObject(object))
                        drawObjectGeometry(object);
                }
            }

            if (showTrajectory && layer->m_player1) {
                auto* player = layer->m_player1;
                const auto pos = player->getPosition();
                m_trail.push_back(pos);
                while (m_trail.size() > kTrailLimit) m_trail.pop_front();
                for (std::size_t i = 1; i < m_trail.size(); ++i) {
                    const float fade = static_cast<float>(i) /
                        static_cast<float>(std::max<std::size_t>(1, m_trail.size() - 1));
                    m_draw->drawSegment(m_trail[i - 1], m_trail[i], 1.0f,
                        cocos2d::ccc4f(0.10f, 0.78f, 1.0f, 0.08f + 0.50f * fade));
                }
            } else {
                m_trail.clear();
            }

            if (showPlayerBounds) {
                if (layer->m_player1) drawPlayerGeometry(layer->m_player1);
                if (layer->m_player2 && layer->m_player2->isRunning())
                    drawPlayerGeometry(layer->m_player2);
            }
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
            }) || obj->m_isPassable;
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

        void drawObjectGeometry(GameObject* obj) {
            const auto colour = colourFor(obj);
            const float width = 0.7f;

            if (obj->m_objectRadius > 0.0f) {
                const float radius = obj->m_objectRadius *
                    std::max<float>(obj->m_scaleX, obj->m_scaleY);
                m_draw->drawCircle(ccp(obj->m_positionX, obj->m_positionY), radius,
                    cocos2d::ccc4f(0.f, 0.f, 0.f, 0.f), width, colour, 64);
                return;
            }

            // Keep the game's collision-rect calculation authoritative. The
            // returned rectangle is the level-space geometry used by the game.
            const auto rect = obj->getObjectRect();
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
        std::deque<cocos2d::CCPoint> m_trail;
    };

    class FrameGuardMenu final : public cocos2d::CCNode {
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
            if (!CCNode::init()) return false;
            auto* director = cocos2d::CCDirector::sharedDirector();
            const auto size = director->getWinSize();

            m_menu = cocos2d::CCMenu::create();
            if (!m_menu) return false;
            m_menu->setPosition(cocos2d::CCPointZero);
            this->addChild(m_menu, 1);

            m_openButton = makeItem("FG", menu_selector(FrameGuardMenu::onToggleMenu));
            m_hitboxButton = makeItem("", menu_selector(FrameGuardMenu::onToggleHitboxes));
            m_playerButton = makeItem("", menu_selector(FrameGuardMenu::onTogglePlayerBounds));
            m_trailButton = makeItem("", menu_selector(FrameGuardMenu::onToggleTrail));
            m_fpsButton = makeItem("", menu_selector(FrameGuardMenu::onToggleFPS));
            m_guardButton = makeItem("", menu_selector(FrameGuardMenu::onToggleFrameGuard));
            if (!m_openButton || !m_hitboxButton || !m_playerButton || !m_trailButton || !m_fpsButton || !m_guardButton) return false;

            m_openButton->setPosition(ccp(42.f, size.height - 34.f));
            m_hitboxButton->setPosition(ccp(91.f, size.height - 64.f));
            m_playerButton->setPosition(ccp(91.f, size.height - 88.f));
            m_trailButton->setPosition(ccp(91.f, size.height - 112.f));
            m_fpsButton->setPosition(ccp(91.f, size.height - 136.f));
            m_guardButton->setPosition(ccp(91.f, size.height - 160.f));
            m_menu->addChild(m_openButton);
            m_menu->addChild(m_hitboxButton);
            m_menu->addChild(m_playerButton);
            m_menu->addChild(m_trailButton);
            m_menu->addChild(m_fpsButton);
            m_menu->addChild(m_guardButton);
            m_hitboxButton->setVisible(false);
            m_playerButton->setVisible(false);
            m_trailButton->setVisible(false);
            m_fpsButton->setVisible(false);
            m_guardButton->setVisible(false);

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

        void syncLabels() {
            if (m_hitboxButton)
                static_cast<cocos2d::CCLabelBMFont*>(m_hitboxButton->getLabel())->setString(
                    toggleText("Hitboxes", Mod::get()->getSettingValue<bool>("show-hitboxes")).c_str());
            if (m_playerButton)
                static_cast<cocos2d::CCLabelBMFont*>(m_playerButton->getLabel())->setString(
                    toggleText("Player", Mod::get()->getSettingValue<bool>("show-player-bounds")).c_str());
            if (m_trailButton)
                static_cast<cocos2d::CCLabelBMFont*>(m_trailButton->getLabel())->setString(
                    toggleText("Trail", Mod::get()->getSettingValue<bool>("show-trajectory")).c_str());
            if (m_fpsButton)
                static_cast<cocos2d::CCLabelBMFont*>(m_fpsButton->getLabel())->setString(
                    toggleText("FPS", Mod::get()->getSettingValue<bool>("show-fps")).c_str());
            if (m_fpsLabel)
                m_fpsLabel->setVisible(Mod::get()->getSettingValue<bool>("show-fps"));
            if (m_guardButton)
                static_cast<cocos2d::CCLabelBMFont*>(m_guardButton->getLabel())->setString(
                    toggleText("FrameGuard", Mod::get()->getSettingValue<bool>("frameguard-enabled")).c_str());
            if (m_guardLabel)
                m_guardLabel->setVisible(Mod::get()->getSettingValue<bool>("show-frameguard-hud"));
        }

        void updateFrameGuardHUD(float dt) {
            if (!m_guardLabel || !Mod::get()->getSettingValue<bool>("show-frameguard-hud")) return;
            m_guardElapsed += dt;
            if (m_guardElapsed >= 0.10f) {
                auto text = FrameGuardTelemetry::summary();
                m_guardLabel->setString(text.c_str());
                m_guardElapsed = 0.f;
            }
        }

        void updateFPS(float dt) {
            if (!m_fpsLabel || !Mod::get()->getSettingValue<bool>("show-fps") || dt <= 0.f) return;
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
        void onToggleMenu(cocos2d::CCObject*) {
            m_open = !m_open;
            m_hitboxButton->setVisible(m_open);
            m_playerButton->setVisible(m_open);
            m_trailButton->setVisible(m_open);
            m_fpsButton->setVisible(m_open);
            m_guardButton->setVisible(m_open);
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

        cocos2d::CCMenu* m_menu = nullptr;
        cocos2d::CCMenuItemLabel* m_openButton = nullptr;
        cocos2d::CCMenuItemLabel* m_hitboxButton = nullptr;
        cocos2d::CCMenuItemLabel* m_playerButton = nullptr;
        cocos2d::CCMenuItemLabel* m_trailButton = nullptr;
        cocos2d::CCMenuItemLabel* m_fpsButton = nullptr;
        cocos2d::CCMenuItemLabel* m_guardButton = nullptr;
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
        if (!m_fields->overlay) {
            m_fields->overlay = FrameGuardOverlay::create();
            if (m_fields->overlay) this->addChild(m_fields->overlay, kOverlayZ);
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
    log::info("Frame-Guard loaded: collision geometry, player hitboxes, trail, FPS and in-game controls.");
}

class $modify(FrameGuardPlayerObject, PlayerObject) {
    bool pushButton(PlayerButton button) {
        bool result = PlayerObject::pushButton(button);
        FrameGuardTelemetry::record(button, true);
        return result;
    }

    bool releaseButton(PlayerButton button) {
        bool result = PlayerObject::releaseButton(button);
        FrameGuardTelemetry::record(button, false);
        return result;
    }
};
