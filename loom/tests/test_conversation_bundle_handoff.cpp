// Integrator-only entry for the focused existing ZIP transport gate.
#include "conversation_bundle/native_handoff.h"

TEST_SUITE("conversation_bundle_handoff") {
  TEST_CASE("selected ZIP member imports while original JSON stays byte-preserved and opaque") {
    conversation_bundle_native_handoff_fixture();
  }
}
