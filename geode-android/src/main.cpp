#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <deque>

using namespace geode::prelude;

namespace {
    constexpr int kOverlayZ = 9999;
    constexpr std::size_t kTrailLimit = 90;

    // Visual diagnostics only: no collision, physics, input, or death changes.
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

            if (showHitboxes && layer->m_objects) {
                for (unsigned int i = 0; i < layer->m_objects->count(); ++i) {
                    auto* object = typeinfo_cast<GameObject*>(layer->m_objects->objectAtIndex(i));
                    if (!object || !object->isVisible()) continue;
                    const auto rect = object->getObjectRect();
                    const auto end = ccp(rect.origin.x + rect.size.width,
                                         rect.origin.y + rect.size.height);
                    m_draw->drawRect(rect.origin, end, cocos2d::ccc4f(1.f, 0.f, 0.f, 0.06f),
                                     1.f, cocos2d::ccc4f(1.f, 0.2f, 0.2f, 0.85f));
                }
            }

            if (showTrajectory && layer->m_player1) {
                m_trail.push_back(layer->m_player1->getPosition());
                while (m_trail.size() > kTrailLimit) m_trail.pop_front();
                // This is a trail of observed positions, not a future prediction.
                for (std::size_t i = 1; i < m_trail.size(); ++i) {
                    const float alpha = static_cast<float>(i) /
                                        static_cast<float>(m_trail.size());
                    m_draw->drawSegment(m_trail[i - 1], m_trail[i], 1.5f,
                                        cocos2d::ccc4f(0.15f, 0.9f, 1.f, alpha));
                }
            } else {
                m_trail.clear();
            }

            if (showHitboxes && layer->m_player1) drawPlayerBounds(layer->m_player1);
            if (showHitboxes && layer->m_player2) drawPlayerBounds(layer->m_player2);
        }

    private:
        void drawPlayerBounds(PlayerObject* player) {
            const auto rect = player->getObjectRect();
            const auto end = ccp(rect.origin.x + rect.size.width,
                                 rect.origin.y + rect.size.height);
            m_draw->drawRect(rect.origin, end, cocos2d::ccc4f(0.f, 0.8f, 1.f, 0.08f),
                             1.5f, cocos2d::ccc4f(0.f, 0.9f, 1.f, 1.f));
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
    log::info("Frame-Guard prototype loaded. Hitboxes and observed trajectory trail default to ON.");
}
