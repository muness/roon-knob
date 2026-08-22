#include "../m5_beta_app/main/kizz_semantic_layout.h"
#include "../m5_beta_app/main/kizz_semantic_native_layout.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <set>
#include <string>

namespace {

int apply_calls = 0;
uint8_t last_family = 0;

std::string id() { return "slc1-0000000000000000000000000000000000000000000000000000000000000000"; }

std::string demand(const char *id, const char *role, const char *target,
                  const char *necessity = "required",
                  const char *emphasis = "standard", int priority = 0,
                  const char *parent = nullptr) {
    std::string value = "{\"demandId\":\"" + std::string(id) +
                        "\",\"role\":\"" + role +
                        "\",\"target\":{\"kind\":\"" + target +
                        "\",\"id\":\"opaque-" + id +
                        "\"},\"necessity\":\"" + necessity +
                        "\",\"priority\":" + std::to_string(priority) +
                        ",\"emphasis\":\"" + emphasis + "\"";
    if (parent) value += std::string(",\"parentDemandId\":\"") + parent + "\"";
    return value + "}";
}

std::string contract(const std::string &demands) {
    return "{\"version\":\"semantic-layout-contract-v1\",\"contractId\":\"" +
           id() + "\",\"intentId\":\"test-intent\",\"demands\":[" +
           demands + "]}";
}

kizz_semantic_context_t context() {
    kizz_semantic_context_t value = {};
    value.has_artwork = true;
    value.has_transport = true;
    value.has_volume = true;
    value.has_zone = true;
    value.content_available = true;
    value.touch_input = true;
    value.voice_input = true;
    value.button_input = true;
    return value;
}

std::string admit(const std::string &json, kizz_semantic_context_t value,
                  bool expected = true) {
    kizz_semantic_set_context(&value);
    char evidence[4096] = {};
    const bool accepted = kizz_semantic_admit_json(
        json.data(), json.size(), evidence, sizeof(evidence));
    assert(accepted == expected);
    return evidence;
}

void assert_status(const std::string &evidence, const char *status) {
    assert(evidence.find(std::string("\"status\":\"") + status + "\"") !=
           std::string::npos);
}

void assert_family(const std::string &json, kizz_semantic_context_t value,
                   uint8_t expected) {
    assert_status(admit(json, value), "accepted");
    assert(kizz_semantic_apply(id().c_str()));
    assert(kizz_semantic_active_family_token() == expected);
    assert(last_family == expected);
}

} // namespace

extern "C" void touch_ui_apply_semantic_family(uint8_t family_token) {
    ++apply_calls;
    last_family = family_token;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    std::set<std::string> signatures;
    for (uint8_t family = 1; family <= 12; ++family) {
        assert(kizz_semantic_family_name(family)[0] != '\0');
        signatures.insert(kizz_semantic_family_signature(family));
    }
    assert(signatures.size() == 12);

    // The renderer and process_input consume this same table. Every native
    // touch region is reachable at 320x240, disjoint from its siblings, and
    // has a real existing action rather than a decorative label.
    for (uint8_t family = 1; family <= 12; ++family) {
        for (size_t i = 0; i < kizz_semantic_hit_region_count(family); ++i) {
            KizzSemanticHitRegion region = {};
            assert(kizz_semantic_hit_region(family, i, &region));
            assert(region.x >= 0 && region.y >= 0);
            assert(region.x + region.width <= 320);
            assert(region.y + region.height <= 240);
            assert(region.action != KizzSemanticAction::None);
            for (size_t j = i + 1; j < kizz_semantic_hit_region_count(family); ++j) {
                KizzSemanticHitRegion other = {};
                assert(kizz_semantic_hit_region(family, j, &other));
                const bool separated = region.x + region.width <= other.x ||
                                       other.x + other.width <= region.x ||
                                       region.y + region.height <= other.y ||
                                       other.y + other.height <= region.y;
                assert(separated);
            }
            KizzSemanticAction action = KizzSemanticAction::None;
            assert(kizz_semantic_hit_test(family, region.x + region.width / 2,
                                          region.y + region.height / 2,
                                          &action));
            assert(action == region.action);
        }
    }

    auto artwork = context();
    assert_family(contract(demand("hero", "hero", "section", "required",
                                  "dominant")), artwork, 1);

    auto transport = context();
    transport.has_zone = false;
    assert_family(contract(demand("hero", "hero", "section", "required",
                                  "dominant") + "," +
                            demand("play", "primary-control", "control",
                                   "required", "dominant", 1, "hero")),
                  transport, 2);

    auto side = transport;
    side.touch_input = false;
    assert_family(contract(demand("hero", "hero", "section", "required",
                                  "dominant") + "," +
                            demand("play", "primary-control", "control",
                                   "required", "dominant")),
                  side, 3);

    auto band = context();
    band.has_transport = false;
    band.has_volume = false;
    band.has_zone = false;
    assert_family(contract(demand("hero", "hero", "section", "required", "dominant") + "," +
                            demand("title", "primary-content", "section", "required", "standard", 1, "hero")),
                  band, 4);

    auto balanced = transport;
    balanced.touch_input = false;
    assert_family(contract(demand("hero", "hero", "section", "required", "standard") + "," +
                            demand("title", "primary-content", "section", "required", "standard", 1, "hero") + "," +
                            demand("play", "primary-control", "control", "required", "standard", 2, "hero")),
                  balanced, 5);

    auto metadata = context();
    metadata.has_transport = false;
    metadata.has_volume = false;
    metadata.has_zone = false;
    assert_family(contract(demand("title", "primary-content", "section", "required", "dominant")),
                  metadata, 6);

    auto transport_focus = transport;
    assert_family(contract(demand("play", "primary-control", "control", "required", "dominant")),
                  transport_focus, 7);

    auto volume = context();
    volume.has_transport = false;
    volume.has_zone = false;
    assert_family(contract(demand("level", "primary-control", "control", "required", "dominant")),
                  volume, 8);

    auto zones = context();
    zones.has_transport = false;
    zones.has_volume = false;
    assert_family(contract(demand("choose", "primary-control", "control", "required", "dominant")),
                  zones, 9);

    auto listening = context();
    listening.voice_active = true;
    assert_family(contract(demand("state", "status", "section", "required", "dominant")),
                  listening, 10);

    auto review = context();
    review.review_active = true;
    assert_family(contract(demand("decision", "confirmation", "control", "required", "dominant")),
                  review, 11);

    auto recovery = context();
    recovery.recovery_active = true;
    recovery.has_transport = false;
    recovery.has_volume = false;
    recovery.has_zone = false;
    assert_family(contract(demand("status", "status", "section", "required", "dominant")),
                  recovery, 12);

    auto degraded = context();
    degraded.has_artwork = false;
    const std::string degraded_evidence = admit(
        contract(demand("hero", "hero", "section", "optional", "dominant") + "," +
                 demand("play", "primary-control", "control", "required", "standard")),
        degraded);
    assert_status(degraded_evidence, "accepted");
    assert(degraded_evidence.find("optional-target-omitted") != std::string::npos);

    auto missing_art = context();
    missing_art.has_artwork = false;
    const std::string missing_evidence = admit(
        contract(demand("hero", "hero", "section", "required", "dominant")),
        missing_art, false);
    assert_status(missing_evidence, "no-fit");

    auto no_controls = context();
    no_controls.has_transport = false;
    no_controls.has_volume = false;
    no_controls.has_zone = false;
    const std::string unavailable = admit(
        contract(demand("action", "primary-control", "control", "required")),
        no_controls, false);
    assert_status(unavailable, "no-fit");

    auto overflow = context();
    overflow.metadata_overflow = true;
    const std::string overflow_evidence = admit(
        contract(demand("title", "primary-content", "section", "required")),
        overflow);
    assert_status(overflow_evidence, "accepted");
    assert(overflow_evidence.find("metadata-overflow-accepted") != std::string::npos);

    const std::string uppercase = contract(demand("hero", "hero", "section"));
    std::string bad_id = uppercase;
    bad_id.replace(bad_id.find(id()), id().size(),
                   "slc1-000000000000000000000000000000000000000000000000000000000000000A");
    assert_status(admit(bad_id, context(), false), "no-fit");

    auto stable = context();
    const std::string stable_contract = contract(demand("hero", "hero", "section"));
    assert_status(admit(stable_contract, stable), "accepted");
    const int calls_before = apply_calls;
    assert(kizz_semantic_apply(id().c_str()));
    assert(kizz_semantic_apply_changed());
    assert(kizz_semantic_apply(id().c_str()));
    assert(!kizz_semantic_apply_changed());
    assert(apply_calls == calls_before + 1);
    assert(!kizz_semantic_apply("slc1-ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"));

    std::cout << "kizz semantic layout conformance: 12 families, admission, native hit regions, selection, and idempotence passed\n";
}
