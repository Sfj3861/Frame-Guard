#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <array>
#include <cmath>
#include <deque>

using namespace geode::prelude;

namespace {
    constexpr int kOverlayZ = 9999;
    constexpr std::size_t kTrailLimit = 24;
    using Quad = std::array<cocos2d::CCPoint, 4>;

    constexpr cocos2d::ccColor4F kSolidColor  = {0.20f, 0.85f, 0.35f, 0.90f};
    constexpr cocos2d::ccColor4F kHazardColor = {1.00f, 0.20f, 0.20f, 0.95f};
    constexpr cocos2d::ccColor4F kSpecialColor = {1.00f, 0.72f, 0.15f, 0.90f};
    constexpr cocos2d::ccColor4F kPlayerColor = {0.10f, 0.85f, 1.00f, 1.00f};
    constexpr cocos2d::ccColor4F kInnerColor = {0.95f, 0.95f, 1.00f, 0.95f};

    Quad rectCorners(cocos2d::CCRect const& r) {
        return {{
            cocos2d::ccp(r.getMinX(), r.getMinY()),
            cocos2d::ccp(r.getMinX(), r.getMaxY()),
            cocos2d::ccp(r.getMaxX(), r.getMaxY()),
            cocos2d::ccp(r.getMaxX(), r.getMinY())
        }};
    }

    class FrameGuardOverlay final : public cocos2d::CCDrawNode {
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

        void resetTrail() {
            m_playerTrail.clear();
        }

        void refresh(PlayLayer* layer) {
            if (!layer) return;
            this->clear();

            const bool showHitboxes = Mod::get()->getSettingValue<bool>("show-hitboxes");
            const bool showTrajectory = Mod::get()->getSettingValue<bool>("show-trajectory");

            // OBB2D vertices are supplied by Geometry Dash itself. Drawing into
            // m_objectLayer keeps these points in the same level coordinate space.
            if (showHitboxes && layer->m_objects) {
                for (unsigned int i = 0; i < layer->m_objects->count(); ++i) {
                    auto* object = typeinfo_cast<GameObject*>(layer->m_objects->objectAtIndex(i));
                    if (!object || !object->isVisible() || object->m_isDestroyed) continue;

                    auto type = object->m_objectType;
                    cocos2d::ccColor4F color;
                    bool shouldDraw = true;

                    switch (type) {
                        case GameObjectType::Solid:
                        case GameObjectType::Breakable:
                        case GameObjectType::Slope:
                            color = kSolidColor;
                            break;
                        case GameObjectType::Hazard:
                            color = kHazardColor;
                            break;
                        case GameObjectType::InverseGravityPortal:
                        case GameObjectType::NormalGravityPortal:
                        case GameObjectType::ShipPortal:
                        case GameObjectType::CubePortal:
                        case GameObjectType::YellowJumpPad:
                        case GameObjectType::PinkJumpPad:
                        case GameObjectType::GravityPad:
                        case GameObjectType::YellowJumpRing:
                        case GameObjectType::PinkJumpRing:
                        case GameObjectType::GravityRing:
                        case GameObjectType::InverseMirrorPortal:
                        case GameObjectType::NormalMirrorPortal:
                        case GameObjectType::BallPortal:
                        case GameObjectType::RegularSizePortal:
                        case GameObjectType::MiniSizePortal:
                        case GameObjectType::UfoPortal:
                        case GameObjectType::SecretCoin:
                        case GameObjectType::DualPortal:
                        case GameObjectType::SoloPortal:
                        case GameObjectType::WavePortal:
                        case GameObjectType::RedJumpPad:
                        case GameObjectType::RedJumpRing:
                        case GameObjectType::GreenRing:
                        case GameObjectType::DashRing:
                        case GameObjectType::GravityDashRing:
                        case GameObjectType::RobotPortal:
                        case GameObjectType::SpiderPortal:
                        case GameObjectType::Collectible:
                            color = kSpecialColor;
                            break;
                        default:
                            shouldDraw = false;
                            break;
                    }
                    if (!shouldDraw) continue;

                    object->updateOrientedBox();
                    auto* obb = object->m_objectOBB2D;
                    if (!obb) continue;

                    if (type == GameObjectType::Hazard && object->m_objectRadius > 0.f) {
                        drawCircle(obb->m_center, object->m_objectRadius, color);
                    } else if (type == GameObjectType::Slope) {
                        drawSlope(object, color);
                    } else {
                        Quad points = {{
                            obb->m_obVertexBottomLeft,
                            obb->m_obVertexBottomRight,
                            obb->m_obVertexTopRight,
                            obb->m_obVertexTopLeft
                        }};
                        drawQuad(points, color, 0.85f);
                    }
                }
            }

            if (showTrajectory && layer->m_player1) {
                auto* player = layer->m_player1;
                auto rect = player->getObjectRect();
                if (rect.size.width > 0.f && rect.size.height > 0.f) {
                    m_playerTrail.push_back(rectCorners(rect));
                    while (m_playerTrail.size() > kTrailLimit) m_playerTrail.pop_front();
                    for (std::size_t i = 0; i < m_playerTrail.size(); ++i) {
                        float alpha = 0.06f + 0.26f *
                            (static_cast<float>(i + 1) / static_cast<float>(m_playerTrail.size()));
                        drawQuad(m_playerTrail[i], {0.10f, 0.80f, 1.00f, alpha}, 0.65f);
                    }
                }
            } else {
                m_playerTrail.clear();
            }

            if (showHitboxes) {
                if (layer->m_player1) drawPlayer(layer->m_player1);
                if (layer->m_player2) drawPlayer(layer->m_player2);
            }
        }

    private:
        void drawQuad(Quad const& points, cocos2d::ccColor4F color, float width) {
            this->drawPolygon(points.data(), static_cast<unsigned int>(points.size()),
                              {0.f, 0.f, 0.f, 0.f}, width, color);
        }

        void drawCircle(cocos2d::CCPoint center, float radius, cocos2d::ccColor4F color) {
            constexpr unsigned int segments = 32;
            constexpr float pi = 3.14159265358979323846f;
            std::array<cocos2d::CCPoint, segments> points{};
            for (unsigned int i = 0; i < segments; ++i) {
                float angle = (2.f * pi * static_cast<float>(i)) / segments;
                points[i] = cocos2d::ccp(center.x + std::cos(angle) * radius,
                                         center.y + std::sin(angle) * radius);
            }
            this->drawPolygon(points.data(), segments, {0.f, 0.f, 0.f, 0.f}, 0.85f, color);
        }

        void drawSlope(GameObject* object, cocos2d::ccColor4F color) {
            auto* obb = object->m_objectOBB2D;
            if (!obb) return;
            cocos2d::CCPoint points[3];
            int n = 0;
            if (object->m_isFlippedX || !object->m_isFlippedY)
                points[n++] = obb->m_obVertexBottomLeft;
            if (!object->m_isFlippedX || !object->m_isFlippedY)
                points[n++] = obb->m_obVertexBottomRight;
            if (!object->m_isFlippedX || object->m_isFlippedY)
                points[n++] = obb->m_obVertexTopRight;
            if (object->m_isFlippedX || object->m_isFlippedY)
                points[n++] = obb->m_obVertexTopLeft;
            if (n == 3) this->drawPolygon(points, 3, {0.f, 0.f, 0.f, 0.f}, 0.9f, color);
        }

        void drawPlayer(PlayerObject* player) {
            if (!player) return;
            auto* obb = player->getOrientedBox();
            if (obb) {
                Quad oriented = {{
                    obb->m_obVertexBottomLeft,
                    obb->m_obVertexBottomRight,
                    obb->m_obVertexTopRight,
                    obb->m_obVertexTopLeft
                }};
                drawQuad(oriented, kPlayerColor, 1.15f);
            }

            // Geometry Dash's inset object rect is useful as a separate
            // reference for the player's inner collision region.
            auto inner = player->getObjectRect(0.25f, 0.25f);
            if (inner.size.width > 0.f && inner.size.height > 0.f)
                drawQuad(rectCorners(inner), kInnerColor, 0.85f);
        }

        std::deque<Quad> m_playerTrail;
    };
}

class $modify(FrameGuardPlayLayer, PlayLayer) {
    struct Fields {
        FrameGuardOverlay* overlay = nullptr;
    };

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        if (!m_objectLayer) return;
        if (!m_fields->overlay) {
            m_fields->overlay = FrameGuardOverlay::create();
            if (m_fields->overlay) m_objectLayer->addChild(m_fields->overlay, kOverlayZ);
        }
        if (m_fields->overlay) m_fields->overlay->refresh(this);
    }

    void resetLevel() {
        if (m_fields->overlay) m_fields->overlay->resetTrail();
        PlayLayer::resetLevel();
    }
};

$on_mod(Loaded) {
    log::info("Frame-Guard loaded: engine OBB hitboxes and observed player trail active.");
}
