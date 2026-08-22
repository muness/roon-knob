#include "kizz_semantic_layout.h"
#include "kizz_semantic_native_layout.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

extern "C" void touch_ui_apply_semantic_family(uint8_t) {}

namespace {

std::string esc(const char *value) {
    std::string out;
    for (const char *p = value; p && *p; ++p) {
        if (*p == '&') out += "&amp;";
        else if (*p == '<') out += "&lt;";
        else if (*p == '>') out += "&gt;";
        else out += *p;
    }
    return out;
}

void rect(std::string &svg, int x, int y, int w, int h, const char *fill,
          int radius = 0) {
    svg += "<rect x=\"" + std::to_string(x) + "\" y=\"" + std::to_string(y) +
           "\" width=\"" + std::to_string(w) + "\" height=\"" +
           std::to_string(h) + "\" fill=\"" + fill + "\"";
    if (radius) svg += " rx=\"" + std::to_string(radius) + "\"";
    svg += "/>";
}

void label(std::string &svg, const char *value, int x, int y, int size = 10,
           const char *fill = "#e8edf5") {
    svg += "<text x=\"" + std::to_string(x) + "\" y=\"" + std::to_string(y) +
           "\" text-anchor=\"middle\" fill=\"" + fill +
           "\" font-family=\"sans-serif\" font-size=\"" + std::to_string(size) +
           "\">" + esc(value) + "</text>";
}

void artwork(std::string &svg, int x, int y, int w, int h) {
    rect(svg, x, y, w, h, "#28354a", 8);
    rect(svg, x + w / 5, y + h / 7, w * 3 / 5, h * 3 / 5, "#6a7fb0", 8);
    label(svg, "ARTWORK", x + w / 2, y + h / 2 + 5, 12, "#101722");
}

void button(std::string &svg, uint8_t family, KizzSemanticAction action,
            const char *value, bool primary = false) {
    KizzSemanticHitRegion region = {};
    if (!kizz_semantic_hit_region_for_action(family, action, &region)) return;
    rect(svg, region.x, region.y, region.width, region.height,
         primary ? "#7aa2ff" : "#293446", 8);
    label(svg, value, region.x + region.width / 2,
          region.y + region.height / 2 + 4, 10, primary ? "#101722" : "#e8edf5");
}

std::string render(uint8_t family) {
    std::string svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"320\" height=\"240\">";
    rect(svg, 0, 0, 320, 240, "#080b12");
    label(svg, kizz_semantic_family_name(family), 160, 16, 11, "#9db8ff");
    switch (family) {
    case 1:
        artwork(svg, 0, 26, 320, 183);
        label(svg, "NOW PLAYING", 160, 230, 10);
        break;
    case 2:
        artwork(svg, 0, 27, 320, 139);
        label(svg, "TITLE — ARTIST", 160, 187, 10);
        button(svg, 2, KizzSemanticAction::PreviousTrack, "PREV");
        button(svg, 2, KizzSemanticAction::TogglePlayback, "PLAY", true);
        button(svg, 2, KizzSemanticAction::NextTrack, "NEXT");
        break;
    case 3:
        artwork(svg, 0, 27, 235, 207);
        button(svg, 3, KizzSemanticAction::PreviousTrack, "PREV");
        button(svg, 3, KizzSemanticAction::TogglePlayback, "PLAY", true);
        button(svg, 3, KizzSemanticAction::NextTrack, "NEXT");
        break;
    case 4:
        artwork(svg, 0, 27, 320, 126);
        rect(svg, 0, 153, 320, 87, "#101722");
        label(svg, "TITLE", 160, 171, 11);
        label(svg, "ARTIST · ALBUM", 160, 193, 9, "#a9b6ca");
        button(svg, 4, KizzSemanticAction::TogglePlayback, "PLAY", true);
        break;
    case 5:
        artwork(svg, 8, 39, 126, 126);
        label(svg, "TITLE", 220, 67, 14);
        label(svg, "ARTIST · ALBUM", 220, 91, 9, "#a9b6ca");
        button(svg, 5, KizzSemanticAction::PreviousTrack, "PREV");
        button(svg, 5, KizzSemanticAction::TogglePlayback, "PLAY", true);
        button(svg, 5, KizzSemanticAction::NextTrack, "NEXT");
        break;
    case 6:
        label(svg, "NOW PLAYING", 160, 48, 11, "#7aa2ff");
        label(svg, "LONG TITLE METADATA", 160, 91, 15);
        label(svg, "ARTIST · ALBUM", 160, 118, 10, "#a9b6ca");
        artwork(svg, 12, 165, 58, 58);
        button(svg, 6, KizzSemanticAction::TogglePlayback, "PLAY", true);
        button(svg, 6, KizzSemanticAction::NextTrack, "NEXT");
        break;
    case 7:
        label(svg, "PLAYING", 160, 60, 18, "#7aa2ff");
        button(svg, 7, KizzSemanticAction::PreviousTrack, "PREV");
        button(svg, 7, KizzSemanticAction::TogglePlayback, "PAUSE", true);
        button(svg, 7, KizzSemanticAction::NextTrack, "NEXT");
        label(svg, "TITLE · ARTIST", 160, 202, 10, "#a9b6ca");
        break;
    case 8:
        label(svg, "VOLUME", 160, 56, 13, "#7aa2ff");
        rect(svg, 18, 75, 284, 48, "#293446", 14);
        rect(svg, 18, 75, 174, 48, "#7aa2ff", 14);
        label(svg, "-12.0 dB", 160, 106, 16, "#101722");
        button(svg, 8, KizzSemanticAction::VolumeDown, "-");
        button(svg, 8, KizzSemanticAction::TogglePlayback, "PLAY", true);
        button(svg, 8, KizzSemanticAction::VolumeUp, "+");
        break;
    case 9:
        label(svg, "ROOM PICKER", 160, 70, 16, "#7aa2ff");
        rect(svg, 10, 86, 300, 108, "#182235", 8);
        label(svg, "CURRENT ROOM", 160, 120, 12);
        label(svg, "OTHER ROOMS …", 160, 151, 10, "#a9b6ca");
        button(svg, 9, KizzSemanticAction::OpenZonePicker, "OPEN ROOM LIST", true);
        break;
    case 10:
        rect(svg, 72, 42, 176, 80, "#28354a", 40);
        label(svg, "LISTENING", 160, 65, 13, "#7aa2ff");
        label(svg, "SAY SOMETHING", 160, 170, 11);
        label(svg, "TRANSCRIPT / RESPONSE", 160, 202, 9, "#a9b6ca");
        break;
    case 11:
        rect(svg, 12, 40, 296, 88, "#182235", 8);
        label(svg, "CANDIDATE", 160, 76, 13);
        label(svg, "VOICE CONFIRMATION", 160, 184, 12, "#7aa2ff");
        label(svg, "SAY ACCEPT OR REVISE", 160, 207, 9, "#a9b6ca");
        break;
    case 12:
        rect(svg, 16, 48, 288, 48, "#381d28", 10);
        label(svg, "RECONNECTING", 160, 78, 14, "#ff8a8a");
        label(svg, "NETWORK · LAST GOOD METADATA", 160, 135, 9, "#a9b6ca");
        label(svg, "RECOVERY IS AUTOMATIC", 160, 191, 11, "#7aa2ff");
        break;
    default: assert(false); break;
    }
    svg += "</svg>";
    return svg;
}

std::string demand(const char *id, const char *role, const char *kind,
                   int priority = 0, const char *parent = nullptr) {
    std::string value = std::string("{\"demandId\":\"") + id +
                        "\",\"role\":\"" + role +
                        "\",\"target\":{\"kind\":\"" + kind +
                        "\",\"id\":\"opaque-" + id +
                        "\"},\"necessity\":\"required\",\"priority\":" +
                        std::to_string(priority) +
                        ",\"emphasis\":\"dominant\"";
    if (parent) value += std::string(",\"parentDemandId\":\"") + parent + "\"";
    return value + "}";
}

std::string contract(uint8_t family) {
    std::string demands;
    switch (family) {
    case 1: demands = demand("hero", "hero", "section"); break;
    case 2: demands = demand("hero", "hero", "section") + "," +
                           demand("play", "primary-control", "control", 1, "hero"); break;
    case 3: demands = demand("hero", "hero", "section") + "," +
                           demand("play", "primary-control", "control", 1); break;
    case 4: demands = demand("hero", "hero", "section") + "," +
                           demand("title", "primary-content", "section", 1, "hero"); break;
    case 5: demands = demand("hero", "hero", "section") + "," +
                           demand("title", "primary-content", "section", 1, "hero") + "," +
                           demand("play", "primary-control", "control", 2, "hero"); break;
    case 6: demands = demand("title", "primary-content", "section"); break;
    case 7: demands = demand("play", "primary-control", "control"); break;
    case 8: demands = demand("level", "primary-control", "control"); break;
    case 9: demands = demand("choose", "primary-control", "control"); break;
    case 10: demands = demand("state", "status", "section"); break;
    case 11: demands = demand("decision", "confirmation", "control"); break;
    case 12: demands = demand("status", "status", "section"); break;
    default: assert(false); break;
    }
    return std::string("{\"version\":\"semantic-layout-contract-v1\",\"contractId\":\"slc1-") +
           std::string(64, '0') + "\",\"intentId\":\"simulator\",\"demands\":[" +
           demands + "]}";
}

} // namespace

int main(int argc, char **argv) {
    const std::filesystem::path output = argc > 1 ? argv[1] : "/tmp/kizz-semantic-golden";
    std::filesystem::create_directories(output);
    std::ofstream manifest(output / "manifest.json");
    manifest << "{\"version\":\"kizz-native-semantic-simulator-v2\",\"frames\":[";
    for (uint8_t family = 1; family <= 12; ++family) {
        kizz_semantic_context_t context = {};
        context.has_artwork = family <= 4 || family == 5;
        context.has_transport = family == 2 || family == 3 || family == 5 || family == 7;
        context.has_volume = family == 8;
        context.has_zone = family == 9;
        context.content_available = true;
        context.voice_input = family == 10 || family == 11;
        context.voice_active = family == 10;
        context.review_active = family == 11;
        context.recovery_active = family == 12;
        context.touch_input = true;
        if (family == 3 || family == 5) context.touch_input = false;
        kizz_semantic_set_context(&context);
        const std::string json = contract(family);
        char evidence[4096] = {};
        assert(kizz_semantic_admit_json(json.c_str(), json.size(), evidence, sizeof(evidence)));
        assert(kizz_semantic_apply("slc1-0000000000000000000000000000000000000000000000000000000000000000"));
        assert(kizz_semantic_active_family_token() == family);
        const std::string filename = "family-" + std::to_string(family) + ".svg";
        std::ofstream frame(output / filename);
        frame << render(family);
        if (family > 1) manifest << ',';
        manifest << "{\"token\":" << static_cast<unsigned>(family)
                 << ",\"file\":\"" << filename << "\",\"signature\":\""
                 << esc(kizz_semantic_family_signature(family)) << "\",\"evidence\":"
                 << evidence << '}';
    }
    manifest << "]}\n";
    std::cout << "generated twelve layout-specific Kizz native SVG golden frames in "
              << output << '\n';
    return 0;
}
