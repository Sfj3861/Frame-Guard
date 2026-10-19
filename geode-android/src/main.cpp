#include <Geode/Geode.hpp>

using namespace geode::prelude;

// Minimal, non-invasive Android Geode entry point.
// Later diagnostics should operate on synthetic/replay events only.
$on_mod(Loaded) {
    log::info("Frame-Guard Android Geode module loaded.");
}
