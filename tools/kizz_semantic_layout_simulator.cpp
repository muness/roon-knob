#include "kizz_semantic_layout.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

extern "C" void touch_ui_apply_semantic_family(uint8_t) {}

namespace {
std::string demand(const char *id, const char *role, const char *kind,
                   const char *necessity, int priority, const char *emphasis,
                   const char *parent = nullptr) {
    std::string value = std::string("{\"demandId\":\"") + id +
                        "\",\"role\":\"" + role +
                        "\",\"target\":{\"kind\":\"" + kind +
                        "\",\"id\":\"opaque-" + id + "\"},\"necessity\":\"" +
                        necessity + "\",\"priority\":" + std::to_string(priority) +
                        ",\"emphasis\":\"" + emphasis + "\"";
    if (parent) value += std::string(",\"parentDemandId\":\"") + parent + "\"";
    return value + "}";
}
std::string contract(const std::string &items) {
    return "{\"version\":\"semantic-layout-contract-v1\",\"contractId\":\"slc1-" +
           std::string(64, '0') + "\",\"intentId\":\"simulator\",\"demands\":[" + items + "]}";
}
std::string preview(uint8_t token, const char *name, const char *signature) {
    const int hue = static_cast<int>(token) * 29 % 360;
    std::string svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"320\" height=\"240\">";
    svg += "<rect width=\"320\" height=\"240\" fill=\"#080b12\"/>";
    svg += "<rect x=\"8\" y=\"8\" width=\"304\" height=\"24\" rx=\"12\" fill=\"hsl(" + std::to_string(hue) + ",55%,35%)\"/>";
    svg += "<text x=\"160\" y=\"25\" text-anchor=\"middle\" fill=\"white\" font-family=\"sans-serif\" font-size=\"11\">semantic native preview</text>";
    svg += std::string("<text x=\"160\" y=\"94\" text-anchor=\"middle\" fill=\"white\" font-family=\"sans-serif\" font-size=\"18\">") + name + "</text>";
    svg += std::string("<text x=\"160\" y=\"122\" text-anchor=\"middle\" fill=\"#d4dbe5\" font-family=\"monospace\" font-size=\"8\">") + signature + "</text>";
    svg += "<rect x=\"32\" y=\"198\" width=\"76\" height=\"24\" rx=\"8\" fill=\"#252c38\"/><rect x=\"122\" y=\"198\" width=\"76\" height=\"24\" rx=\"8\" fill=\"#7aa2ff\"/><rect x=\"212\" y=\"198\" width=\"76\" height=\"24\" rx=\"8\" fill=\"#252c38\"/>";
    return svg + "</svg>";
}
} // namespace

int main(int argc, char **argv) {
    const std::filesystem::path output = argc > 1 ? argv[1] : "/tmp/kizz-semantic-golden";
    std::filesystem::create_directories(output);
    kizz_semantic_context_t context = {};
    context.has_artwork = true; context.has_transport = true; context.has_volume = true;
    context.content_available = true; context.touch_input = true;
    context.voice_input = true; context.button_input = true;
    const std::string vectors[] = {
        demand("hero", "hero", "section", "required", 0, "dominant"),
        demand("hero", "hero", "section", "required", 0, "dominant") + "," + demand("play", "primary-control", "control", "required", 1, "standard", "hero"),
        demand("hero", "hero", "section", "required", 0, "dominant") + "," + demand("play", "primary-control", "control", "required", 1, "standard"),
        demand("hero", "hero", "section", "required", 0, "dominant") + "," + demand("title", "primary-content", "section", "required", 1, "standard", "hero"),
        demand("hero", "hero", "section", "required", 0, "standard") + "," + demand("title", "primary-content", "section", "required", 1, "standard", "hero") + "," + demand("play", "primary-control", "control", "required", 2, "standard", "hero"),
        demand("title", "primary-content", "section", "required", 0, "dominant"),
        demand("play", "primary-control", "control", "required", 0, "dominant"),
        demand("level", "primary-control", "control", "required", 0, "dominant"),
        demand("choose", "primary-control", "control", "required", 0, "dominant"),
        demand("state", "status", "section", "required", 0, "dominant"),
        demand("decision", "confirmation", "control", "required", 0, "dominant"),
        demand("status", "status", "section", "required", 0, "dominant"),
    };
    std::ofstream manifest(output / "manifest.json");
    manifest << "{\"version\":\"kizz-native-semantic-simulator-v1\",\"frames\":[";
    for (size_t index = 0; index < 12; ++index) {
        kizz_semantic_context_t frame = context;
        if (index == 2) frame.touch_input = false;
        if (index == 3) { frame.has_transport = false; frame.has_volume = false; frame.has_zone = false; }
        if (index == 4) frame.touch_input = false;
        if (index == 5) { frame.has_transport = false; frame.has_volume = false; frame.has_zone = false; }
        if (index == 7) { frame.has_transport = false; frame.has_zone = false; }
        if (index == 8) { frame.has_transport = false; frame.has_volume = false; frame.has_zone = true; }
        if (index == 9) frame.voice_active = true;
        if (index == 10) frame.review_active = true;
        if (index == 11) { frame.has_transport = false; frame.has_volume = false; frame.has_zone = false; frame.recovery_active = true; }
        kizz_semantic_set_context(&frame);
        const std::string json = contract(vectors[index]);
        char evidence[4096] = {};
        assert(kizz_semantic_admit_json(json.c_str(), json.size(), evidence, sizeof(evidence)));
        assert(kizz_semantic_apply("slc1-0000000000000000000000000000000000000000000000000000000000000000"));
        const uint8_t token = kizz_semantic_active_family_token();
        assert(token == index + 1);
        const std::string filename = "family-" + std::to_string(index + 1) + ".svg";
        std::ofstream frame_file(output / filename);
        frame_file << preview(token, kizz_semantic_family_name(token), kizz_semantic_family_signature(token));
        if (index) manifest << ',';
        manifest << "{\"token\":" << static_cast<unsigned>(token) << ",\"file\":\"" << filename << "\",\"evidence\":" << evidence << '}';
    }
    manifest << "]}\n";
    std::cout << "generated twelve Kizz semantic SVG/golden frames in " << output << '\n';
    return 0;
}
