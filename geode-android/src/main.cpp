#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <deque>

using namespace geode::prelude;

namespace {
    constexpr int kOverlayZ = 9999;
    constexpr std::size_t kTrailLimit = 24;

    std::string toggleText(char const* name, bool value) {
        return fmt::format("{}: {}", name, value ? "ON" : "OFF");
    }

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

        void resetTrail() { m_trail.clear(); }

        void refresh(PlayLayer* layer) {
            if (!layer || !m_draw) return;
            m_draw->clear();

            const bool showHitboxes = Mod::get()->getSettingValue<bool>("show-hitboxes");
            const bool showTrajectory = Mod::get()->getSettingValue<bool>("show-trajectory");

            // Geometry Dash supplies these object bounds. Convert through world space
            // so nested level objects line up with the overlay's coordinate system.
            if (showHitboxes && layer->m_objects) {
                for (unsigned int i = 0; i < layer->m_objects->count(); ++i) {
                    auto* object = typeinfo_cast<GameObject*>(layer->m_objects->objectAtIndex(i));
                    if (!object || !object->isVisible() || !object->getParent()) continue;

                    const auto rect = object->getObjectRect();
                    if (rect.size.width <= 0.f || rect.size.height <= 0.f) continue;

                    const auto p1 = toOverlay(object, rect.origin);
                    const auto p2 = toOverlay(object, ccp(rect.origin.x + rect.size.width,
                                                          rect.origin.y + rect.size.height));
                    const auto color = object->m_objectType == GameObjectType::Hazard
                        ? cocos2d::ccc4f(1.f, 0.20f, 0.20f, 0.88f)
                        : cocos2d::ccc4f(0.30f, 1.f, 0.40f, 0.72f);
                    m_draw->drawRect(p1, p2, cocos2d::ccc4f(0.f, 0.f, 0.f, 0.f),
                                     0.8f, color);
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
                        cocos2d::ccc4f(0.10f, 0.78f, 1.f, 0.08f + 0.50f * age));
                }
            } else {
                m_trail.clear();
            }

            if (showHitboxes) {
                if (layer->m_player1) drawPlayerBounds(layer->m_player1);
                if (layer->m_player2) drawPlayerBounds(layer->m_player2);
            }
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
                             1.2f, cocos2d::ccc4f(0.10f, 0.85f, 1.f, 0.98f));
        }

        cocos2d::CCDrawNode* m_draw = nullptr;
        std::deque<cocos2d::CCPoint> m_trail;
    };

    // Compact in-level menu. It edits the same settings used by Geode's mod
    // settings page, so both controls remain in sync.
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
            m_trailButton = makeItem("", menu_selector(FrameGuardMenu::onToggleTrail));
            if (!m_openButton || !m_hitboxButton || !m_trailButton) return false;

            m_openButton->setPosition(ccp(42.f, size.height - 34.f));
            m_hitboxButton->setPosition(ccp(91.f, size.height - 66.f));
            m_trailButton->setPosition(ccp(91.f, size.height - 94.f));
            m_menu->addChild(m_openButton);
            m_menu->addChild(m_hitboxButton);
            m_menu->addChild(m_trailButton);

            m_hitboxButton->setVisible(false);
            m_trailButton->setVisible(false);
            syncLabels();
            this->setID("frame-guard-in-game-menu");
            return true;
        }

        void syncLabels() {
            if (m_hitboxButton)
                static_cast<cocos2d::CCLabelBMFont*>(m_hitboxButton->getLabel())
                    ->setString(toggleText("Hitboxes",
                        Mod::get()->getSettingValue<bool>("show-hitboxes")).c_str());
            if (m_trailButton)
                static_cast<cocos2d::CCLabelBMFont*>(m_trailButton->getLabel())
                    ->setString(toggleText("Trail",
                        Mod::get()->getSettingValue<bool>("show-trajectory")).c_str());
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
            m_trailButton->setVisible(m_open);
            syncLabels();
        }

        void onToggleHitboxes(cocos2d::CCObject*) {
            const bool next = !Mod::get()->getSettingValue<bool>("show-hitboxes");
            Mod::get()->setSettingValue<bool>("show-hitboxes", next);
            syncLabels();
        }

        void onToggleTrail(cocos2d::CCObject*) {
            const bool next = !Mod::get()->getSettingValue<bool>("show-trajectory");
            Mod::get()->setSettingValue<bool>("show-trajectory", next);
            syncLabels();
        }

        cocos2d::CCMenu* m_menu = nullptr;
        cocos2d::CCMenuItemLabel* m_openButton = nullptr;
        cocos2d::CCMenuItemLabel* m_hitboxButton = nullptr;
        cocos2d::CCMenuItemLabel* m_trailButton = nullptr;
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

        if (!m_fields->overlay) {
            m_fields->overlay = FrameGuardOverlay::create();
            if (m_fields->overlay) this->addChild(m_fields->overlay, kOverlayZ);
        }
        if (m_fields->overlay) m_fields->overlay->refresh(this);

        if (!m_fields->menu && m_uiLayer) {
            m_fields->menu = FrameGuardMenu::create();
            if (m_fields->menu) m_uiLayer->addChild(m_fields->menu, kOverlayZ);
        }
        if (m_fields->menu) m_fields->menu->syncLabels();
    }

    void resetLevel() {
        if (m_fields->overlay) m_fields->overlay->resetTrail();
        PlayLayer::resetLevel();
    }
};

$on_mod(Loaded) {
    log::info("Frame-Guard loaded: engine object bounds, player bounds, trail, and in-game toggles.");
}
