#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <deque>

using namespace geode::prelude;

namespace {
    constexpr int kOverlayZ = 9999;
    // A short trail reads as recent movement rather than a long neon ribbon.
    constexpr std::size_t kTrailLimit = 32;

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
            this->setID("frame-guard-overlay");
            return true;
        }

        void refresh(PlayLayer* layer) {
            if (!layer || !m_draw) return;
            m_draw->clear();

            const bool showHitboxes = Mod::get()->getSettingValue<bool>("show-hitboxes");
            const bool showTrajectory = Mod::get()->getSettingValue<bool>("show-trajectory");

            // getObjectRect is expressed in the object's parent coordinate space.
            // Convert through world space so nested object layers line up with this overlay.
            if (showHitboxes && layer->m_objects) {
                for (unsigned int i = 0; i < layer->m_objects->count(); ++i) {
                    auto* object = typeinfo_cast<GameObject*>(layer->m_objects->objectAtIndex(i));
                    if (!object || !object->isVisible() || !object->getParent()) continue;

                    const auto rect = object->getObjectRect();
                    if (rect.size.width <= 0.f || rect.size.height <= 0.f) continue;
                    const auto p1 = toOverlay(object, rect.origin);
                    const auto p2 = toOverlay(object, ccp(rect.origin.x + rect.size.width,
                                                          rect.origin.y + rect.size.height));
                    // Outline only: no filled rectangles obscuring the actual level art.
                    m_draw->drawRect(p1, p2, cocos2d::ccc4f(0.f, 0.f, 0.f, 0.f),
                                     0.8f, cocos2d::ccc4f(1.f, 0.25f, 0.25f, 0.72f));
                }
            }

            if (showTrajectory && layer->m_player1) {
                auto* player = layer->m_player1;
                const auto worldPos = player->getParent()
                    ? player->getParent()->convertToWorldSpace(player->getPosition())
                    : player->convertToWorldSpace(cocos2d::CCPointZero);
                m_trail.push_back(this->convertToNodeSpace(worldPos));
                while (m_trail.size() > kTrailLimit) m_trail.pop_front();

                for (std::size_t i = 1; i < m_trail.size(); ++i) {
                    const float age = static_cast<float>(i) /
                                      static_cast<float>(m_trail.size() - 1);
                    m_draw->drawSegment(m_trail[i - 1], m_trail[i], 1.0f,
                                        cocos2d::ccc4f(0.15f, 0.78f, 0.95f, 0.08f + 0.52f * age));
                }
            } else {
                m_trail.clear();
            }

            if (showHitboxes && layer->m_player1) drawPlayerBounds(layer->m_player1);
            if (showHitboxes && layer->m_player2) drawPlayerBounds(layer->m_player2);
        }

    private:
        cocos2d::CCPoint toOverlay(cocos2d::CCNode* object, cocos2d::CCPoint point) {
            auto* parent = object->getParent();
            if (!parent) return point;
            return this->convertToNodeSpace(parent->convertToWorldSpace(point));
        }

        void drawPlayerBounds(PlayerObject* player) {
            if (!player || !player->getParent()) return;
            const auto rect = player->getObjectRect();
            if (rect.size.width <= 0.f || rect.size.height <= 0.f) return;
            const auto p1 = toOverlay(player, rect.origin);
            const auto p2 = toOverlay(player, ccp(rect.origin.x + rect.size.width,
                                                  rect.origin.y + rect.size.height));
            m_draw->drawRect(p1, p2, cocos2d::ccc4f(0.f, 0.f, 0.f, 0.f),
                             1.2f, cocos2d::ccc4f(0.1f, 0.85f, 1.f, 0.95f));
        }

        cocos2d::CCDrawNode* m_draw = nullptr;
        std::deque<cocos2d::CCPoint> m_trail;
    };
}

class $modify(FrameGuardPlayLayer, PlayLayer) {
    struct Fields {
        FrameGuardOverlay* overlay = nullptr;
    };

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (!m_fields->overlay) {
            m_fields->overlay = FrameGuardOverlay::create();
            if (m_fields->overlay) this->addChild(m_fields->overlay, kOverlayZ);
        }
        if (m_fields->overlay) m_fields->overlay->refresh(this);
    }
};

$on_mod(Loaded) {
    log::info("Frame-Guard loaded: lightweight outline diagnostics enabled.");
}
