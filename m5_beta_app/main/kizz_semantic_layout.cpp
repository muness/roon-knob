#include "kizz_semantic_layout.h"

extern "C" void touch_ui_apply_semantic_family(uint8_t family_token);

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr char kContractVersion[] = "semantic-layout-contract-v1";
constexpr char kEvidenceVersion[] = "semantic-layout-admission-v1";
constexpr size_t kMaxContractBytes = 8192;
constexpr size_t kMaxDemands = 32;

/* These are deliberately target-local.  No header, JSON evidence, or C ABI
 * value names a family.  The order is the deterministic final tie-break. */
enum class NativeFamily : uint8_t {
    ArtworkImmersive,
    ArtworkBottomTransport,
    ArtworkSideTransport,
    ArtworkMetadataBand,
    BalancedSplit,
    MetadataFocus,
    TransportFocus,
    VolumeFocus,
    ZoneSelection,
    ListeningConversation,
    ReviewConfirmation,
    StatusRecovery,
};

constexpr size_t kFamilyCount = 12;

enum class Role {
    Hero,
    PrimaryContent,
    SecondaryContent,
    PrimaryControl,
    SecondaryControl,
    Status,
    Confirmation,
    Invalid,
};

struct Demand {
    std::string id;
    Role role = Role::Invalid;
    std::string target_kind;
    std::string target_id;
    std::string necessity;
    int priority = 0;
    std::string emphasis;
    std::string parent;
};

struct Contract {
    std::string id;
    std::string intent_id;
    std::vector<Demand> demands;
};

struct Finding {
    std::string code;
    std::string demand_id;
};

struct Admission {
    bool accepted = false;
    NativeFamily family = NativeFamily::StatusRecovery;
    std::vector<std::string> required;
    std::vector<std::string> optional;
    std::vector<std::string> degraded;
    std::vector<Finding> findings;
};

struct FamilyProfile {
    bool artwork;
    bool transport;
    bool volume;
    bool zone;
    bool voice;
    bool review;
    bool recovery;
    uint8_t max_demands;
};

constexpr std::array<FamilyProfile, kFamilyCount> kProfiles{{
    {true, false, false, false, false, false, false, 6},
    {true, true, true, false, false, false, false, 12},
    {true, true, true, false, false, false, false, 10},
    {true, false, false, false, false, false, false, 10},
    {false, true, true, true, false, false, false, 12},
    {false, false, false, false, false, false, false, 10},
    {false, true, true, false, false, false, false, 10},
    {false, true, true, false, false, false, false, 10},
    {false, false, false, true, false, false, false, 10},
    {false, false, false, false, true, false, false, 12},
    {false, false, false, false, false, true, false, 10},
    {false, false, false, false, false, false, true, 10},
}};

class JsonReader {
  public:
    JsonReader(const char *data, size_t length) : p_(data), end_(data + length) {}

    bool object_start() { return punctuation('{'); }
    bool object_end() { return punctuation('}'); }
    bool array_start() { return punctuation('['); }
    bool array_end() { return punctuation(']'); }
    bool next_is(char expected) { whitespace(); return p_ != end_ && *p_ == expected; }
    bool comma() { return punctuation(','); }
    bool colon() { return punctuation(':'); }
    bool done() { whitespace(); return p_ == end_; }
    bool failed() const { return failed_; }

    bool key(std::string *value) { return string(value); }

    bool string(std::string *value) {
        if (!value) return fail();
        whitespace();
        if (p_ == end_ || *p_ != '"') return fail();
        ++p_;
        value->clear();
        while (p_ != end_) {
            const unsigned char c = static_cast<unsigned char>(*p_++);
            if (c == '"') return value->size() <= 128;
            if (c < 0x20) return fail();
            if (c == '\\') {
                if (p_ == end_) return fail();
                const char escaped = *p_++;
                switch (escaped) {
                case '"': value->push_back('"'); break;
                case '\\': value->push_back('\\'); break;
                case '/': value->push_back('/'); break;
                case 'b': value->push_back('\b'); break;
                case 'f': value->push_back('\f'); break;
                case 'n': value->push_back('\n'); break;
                case 'r': value->push_back('\r'); break;
                case 't': value->push_back('\t'); break;
                default: return fail();
                }
            } else {
                value->push_back(static_cast<char>(c));
            }
            if (value->size() > 128) return fail();
        }
        return fail();
    }

    bool integer(int *value) {
        if (!value) return fail();
        whitespace();
        const char *start = p_;
        if (p_ != end_ && *p_ == '-') ++p_;
        if (p_ == end_ || !std::isdigit(static_cast<unsigned char>(*p_))) return fail();
        while (p_ != end_ && std::isdigit(static_cast<unsigned char>(*p_))) ++p_;
        std::string text(start, p_);
        char *tail = nullptr;
        const long parsed = std::strtol(text.c_str(), &tail, 10);
        if (!tail || *tail || parsed < 0 || parsed > 100) return fail();
        *value = static_cast<int>(parsed);
        return true;
    }

  private:
    bool punctuation(char expected) {
        whitespace();
        if (p_ == end_ || *p_ != expected) return fail();
        ++p_;
        return true;
    }
    void whitespace() {
        while (p_ != end_ && std::isspace(static_cast<unsigned char>(*p_))) ++p_;
    }
    bool fail() { failed_ = true; return false; }
    const char *p_;
    const char *end_;
    bool failed_ = false;
};

bool role(const std::string &value, Role *out) {
    if (value == "hero") *out = Role::Hero;
    else if (value == "primary-content") *out = Role::PrimaryContent;
    else if (value == "secondary-content") *out = Role::SecondaryContent;
    else if (value == "primary-control") *out = Role::PrimaryControl;
    else if (value == "secondary-control") *out = Role::SecondaryControl;
    else if (value == "status") *out = Role::Status;
    else if (value == "confirmation") *out = Role::Confirmation;
    else return false;
    return true;
}

bool parse_target(JsonReader *reader, std::string *kind, std::string *id) {
    if (!reader->object_start()) return false;
    bool have_kind = false;
    bool have_id = false;
    while (true) {
        std::string key;
        if (!reader->key(&key) || !reader->colon()) return false;
        if (key == "kind") {
            if (have_kind || !reader->string(kind)) return false;
            have_kind = true;
        } else if (key == "id") {
            if (have_id || !reader->string(id)) return false;
            have_id = true;
        } else {
            return false;
        }
        if (reader->next_is('}')) { if (!reader->object_end()) return false; break; }
        if (!reader->comma()) return false;
    }
    return have_kind && have_id && (*kind == "section" || *kind == "control");
}

bool parse_demand(JsonReader *reader, Demand *demand) {
    if (!reader->object_start()) return false;
    bool have_id = false;
    bool have_role = false;
    bool have_target = false;
    bool have_necessity = false;
    bool have_priority = false;
    bool have_emphasis = false;
    while (true) {
        std::string key;
        if (!reader->key(&key) || !reader->colon()) return false;
        if (key == "demandId") {
            if (have_id || !reader->string(&demand->id)) return false;
            have_id = true;
        } else if (key == "role") {
            std::string text;
            if (have_role || !reader->string(&text) || !role(text, &demand->role)) return false;
            have_role = true;
        } else if (key == "target") {
            if (have_target || !parse_target(reader, &demand->target_kind, &demand->target_id)) return false;
            have_target = true;
        } else if (key == "necessity") {
            if (have_necessity || !reader->string(&demand->necessity) ||
                (demand->necessity != "required" && demand->necessity != "optional")) return false;
            have_necessity = true;
        } else if (key == "priority") {
            if (have_priority || !reader->integer(&demand->priority)) return false;
            have_priority = true;
        } else if (key == "emphasis") {
            if (have_emphasis || !reader->string(&demand->emphasis) ||
                (demand->emphasis != "dominant" && demand->emphasis != "standard" &&
                 demand->emphasis != "quiet")) return false;
            have_emphasis = true;
        } else if (key == "parentDemandId") {
            if (!demand->parent.empty() || !reader->string(&demand->parent)) return false;
        } else {
            return false;
        }
        if (reader->next_is('}')) { if (!reader->object_end()) return false; break; }
        if (!reader->comma()) return false;
    }
    return have_id && have_role && have_target && have_necessity && have_priority && have_emphasis;
}

bool parse_contract(const char *json, size_t length, Contract *contract) {
    if (!json || !contract || length == 0 || length > kMaxContractBytes) return false;
    JsonReader reader(json, length);
    if (!reader.object_start()) return false;
    bool have_version = false;
    bool have_id = false;
    bool have_intent = false;
    bool have_demands = false;
    while (true) {
        std::string key;
        if (!reader.key(&key) || !reader.colon()) return false;
        if (key == "version") {
            std::string version;
            if (have_version || !reader.string(&version) || version != kContractVersion) return false;
            have_version = true;
        } else if (key == "contractId") {
            if (have_id || !reader.string(&contract->id) || contract->id.size() != 69 ||
                contract->id.compare(0, 5, "slc1-") != 0) return false;
            for (size_t i = 5; i < contract->id.size(); ++i) {
                const char c = contract->id[i];
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
            }
            have_id = true;
        } else if (key == "intentId") {
            if (have_intent || !reader.string(&contract->intent_id)) return false;
            have_intent = true;
        } else if (key == "demands") {
            if (have_demands || !reader.array_start()) return false;
            have_demands = true;
            while (!reader.next_is(']')) {
                if (contract->demands.size() == kMaxDemands) return false;
                Demand demand;
                if (!parse_demand(&reader, &demand)) return false;
                contract->demands.push_back(std::move(demand));
                if (reader.next_is(']')) break;
                if (!reader.comma()) return false;
            }
            if (!reader.array_end()) return false;
        } else {
            return false;
        }
        if (reader.next_is('}')) { if (!reader.object_end()) return false; break; }
        if (!reader.comma()) return false;
    }
    if (!reader.done() || !have_version || !have_id || !have_intent || !have_demands ||
        contract->demands.empty()) return false;
    for (size_t i = 0; i < contract->demands.size(); ++i) {
        const Demand &demand = contract->demands[i];
        if (demand.id.empty() || (demand.role >= Role::Invalid) ||
            (demand.target_kind == "control" && demand.role == Role::Hero)) return false;
        for (size_t j = 0; j < i; ++j)
            if (contract->demands[j].id == demand.id) return false;
        if (!demand.parent.empty()) {
            bool found = false;
            for (const Demand &candidate : contract->demands)
                found = found || candidate.id == demand.parent;
            if (!found || demand.parent == demand.id) return false;
        }
    }
    for (const Demand &start : contract->demands) {
        std::string current = start.parent;
        for (size_t steps = 0; !current.empty() && steps <= contract->demands.size(); ++steps) {
            if (current == start.id) return false;
            for (const Demand &candidate : contract->demands)
                if (candidate.id == current) { current = candidate.parent; break; }
        }
    }
    return true;
}

enum class Capability { Other, Transport, Volume, Zone, Voice, Review };

/* Control identity is opaque.  The selector uses only the current renderer's
 * admitted inventory/context and structural demand, never an id substring. */
Capability capability(const Demand &demand, size_t control_count,
                      const kizz_semantic_context_t &context) {
    if (demand.role == Role::Confirmation) return Capability::Review;
    if (demand.role == Role::Status && context.voice_active) return Capability::Voice;
    if (demand.role == Role::Hero || demand.role == Role::PrimaryContent ||
        demand.role == Role::SecondaryContent || demand.role == Role::Status)
        return Capability::Other;
    if (demand.target_kind != "control") return Capability::Other;
    if (context.has_zone && control_count == 1) return Capability::Zone;
    if (context.has_volume && !context.has_transport && control_count == 1)
        return Capability::Volume;
    if (context.has_transport) return Capability::Transport;
    return Capability::Other;
}

bool available(Capability kind, const kizz_semantic_context_t &context) {
    switch (kind) {
    case Capability::Transport: return context.has_transport;
    case Capability::Volume: return context.has_volume;
    case Capability::Zone: return context.has_zone;
    case Capability::Voice: return context.voice_active;
    case Capability::Review: return context.review_active;
    case Capability::Other: return true;
    }
    return false;
}

bool demand_available(const Demand &demand, Capability kind,
                      const kizz_semantic_context_t &context) {
    if (demand.target_kind != "control") return true;
    if (kind == Capability::Other)
        return context.has_transport || context.has_volume || context.has_zone;
    return available(kind, context);
}

bool profile_supports(const FamilyProfile &profile, Capability kind) {
    switch (kind) {
    case Capability::Transport: return profile.transport;
    case Capability::Volume: return profile.volume;
    case Capability::Zone: return profile.zone;
    case Capability::Voice: return profile.voice;
    case Capability::Review: return profile.review;
    case Capability::Other: return true;
    }
    return false;
}

int emphasis_score(const std::string &emphasis) {
    return emphasis == "dominant" ? 3 : emphasis == "standard" ? 2 : 1;
}

Admission select(const Contract &contract, const kizz_semantic_context_t &context) {
    Admission result;
    int best_score = -1000000;
    bool hard_failure = false;
    size_t control_count = 0;
    for (const Demand &demand : contract.demands)
        if (demand.target_kind == "control") ++control_count;
    for (const Demand &demand : contract.demands) {
        const bool required = demand.necessity == "required";
        const Capability kind = capability(demand, control_count, context);
        const bool required_content_missing =
            required && demand.role == Role::Hero && !context.has_artwork;
        const bool required_text_missing =
            required && (demand.role == Role::PrimaryContent ||
                         demand.role == Role::SecondaryContent) &&
            !context.content_available;
        if (required_content_missing || required_text_missing) {
            result.findings.push_back({"missing-required-content", demand.id});
            hard_failure = true;
        } else if (!required &&
                   ((demand.role == Role::Hero && !context.has_artwork) ||
                    ((demand.role == Role::PrimaryContent ||
                      demand.role == Role::SecondaryContent) &&
                     !context.content_available))) {
            result.findings.push_back({"optional-target-omitted", demand.id});
            result.degraded.push_back(demand.id);
        } else if (!demand_available(demand, kind, context)) {
            result.findings.push_back({required ? "capability-unavailable" : "optional-target-omitted", demand.id});
            if (required) hard_failure = true;
            else result.degraded.push_back(demand.id);
        } else if (required) {
            result.required.push_back(demand.id);
        } else {
            result.optional.push_back(demand.id);
        }
    }
    for (size_t index = 0; index < kFamilyCount; ++index) {
        const FamilyProfile &profile = kProfiles[index];
        if (contract.demands.size() > profile.max_demands) continue;
        bool eligible = true;
        int score = 0;
        int heroes = 0;
        int content = 0;
        int controls = 0;
        bool required_volume = false;
        bool wants_zone = false;
        bool wants_review = false;
        bool wants_voice = false;
        bool attached_transport = false;
        bool free_transport = false;
        bool associated_content = false;
        for (const Demand &demand : contract.demands) {
            const Capability kind = capability(demand, control_count, context);
            const bool required = demand.necessity == "required";
            if (required && (!demand_available(demand, kind, context) ||
                             !profile_supports(profile, kind))) eligible = false;
            if (demand.role == Role::Hero) { ++heroes; score += 24 * emphasis_score(demand.emphasis); }
            if (demand.role == Role::PrimaryContent || demand.role == Role::SecondaryContent) {
                ++content; score += demand.role == Role::PrimaryContent ? 18 : 8;
                associated_content = associated_content || !demand.parent.empty();
            }
            if (demand.role == Role::PrimaryControl || demand.role == Role::SecondaryControl ||
                demand.role == Role::Confirmation) {
                ++controls; score += demand.role == Role::PrimaryControl ? 18 : 7;
            }
            if (kind == Capability::Volume && required) required_volume = true;
            if (kind == Capability::Zone) wants_zone = true;
            if (kind == Capability::Review || demand.role == Role::Confirmation) wants_review = true;
            if (kind == Capability::Voice) wants_voice = true;
            if (kind == Capability::Transport) {
                if (demand.parent.empty()) free_transport = true;
                else attached_transport = true;
            }
            if (demand.parent.empty()) score += 2;
            else score += 4; // associated controls/content preserve hierarchy
            score += demand.priority;
        }
        if (required_volume && !profile.volume) eligible = false;
        if (wants_zone && !profile.zone) eligible = false;
        if (wants_review && !profile.review) eligible = false;
        if (wants_voice && !profile.voice) eligible = false;
        if (profile.artwork && !context.has_artwork) score -= 40;
        if (context.voice_active) score += profile.voice ? 1000 : -300;
        if (context.review_active) score += profile.review ? 1100 : -300;
        if (context.recovery_active) score += profile.recovery ? 1200 : -300;
        if (wants_zone) score += profile.zone ? 900 : -300;
        if (required_volume) score += profile.volume ? 850 : -300;
        if (heroes && profile.artwork) score += 260;
        if (heroes && !profile.artwork) score += 60;
        if (controls && profile.transport) score += 100;
        if (content && profile.artwork) score += 60;
        if (contract.demands.size() > 8 && profile.max_demands < 12) score -= 200;
        if (index == static_cast<size_t>(NativeFamily::ArtworkBottomTransport) && heroes && controls) score += 180;
        if (index == static_cast<size_t>(NativeFamily::ArtworkSideTransport) && heroes && controls) score += 120;
        if (index == static_cast<size_t>(NativeFamily::ArtworkBottomTransport) && heroes && attached_transport) score += 90;
        if (index == static_cast<size_t>(NativeFamily::ArtworkSideTransport) && heroes && free_transport) score += 210;
        if (index == static_cast<size_t>(NativeFamily::ArtworkMetadataBand) && heroes && content) score += 160;
        if (index == static_cast<size_t>(NativeFamily::BalancedSplit) && content && controls) score += 140;
        if (index == static_cast<size_t>(NativeFamily::BalancedSplit) && associated_content && controls) score += 700;
        if (index == static_cast<size_t>(NativeFamily::MetadataFocus) && content > controls) score += 180;
        if (index == static_cast<size_t>(NativeFamily::TransportFocus) && controls > content) score += 180;
        if (index == static_cast<size_t>(NativeFamily::VolumeFocus) && required_volume) score += 220;
        if (index == static_cast<size_t>(NativeFamily::ListeningConversation) && context.voice_active) score += 250;
        if (index == static_cast<size_t>(NativeFamily::ReviewConfirmation) && context.review_active) score += 250;
        if (index == static_cast<size_t>(NativeFamily::StatusRecovery) && context.recovery_active) score += 250;
        if (index == static_cast<size_t>(NativeFamily::ArtworkBottomTransport) &&
            context.touch_input && heroes && controls) score += 260;
        if (index == static_cast<size_t>(NativeFamily::ArtworkSideTransport) &&
            !context.touch_input && heroes && controls) score += 260;
        if (index == static_cast<size_t>(NativeFamily::BalancedSplit) &&
            content && controls) score += 280;
        if (index == static_cast<size_t>(NativeFamily::ZoneSelection) && wants_zone)
            score += 420;
        if (eligible) {
            /* Stable first-wins tie break is intentional: family ordinals are
             * never exposed, but a fixed order makes repeated admission safe. */
            if (!result.accepted || score > best_score) {
                result.accepted = true;
                result.family = static_cast<NativeFamily>(index);
                best_score = score;
            }
        }
    }
    if (hard_failure) {
        result.accepted = false;
    } else if (!result.accepted) {
        result.findings.push_back({"budget-mismatch", ""});
    } else {
        const FamilyProfile &profile = kProfiles[static_cast<size_t>(result.family)];
        std::vector<std::string> retained;
        for (const Demand &demand : contract.demands) {
            if (demand.necessity == "optional" &&
                !profile_supports(profile, capability(demand, control_count, context))) {
                result.degraded.push_back(demand.id);
                result.findings.push_back({"optional-target-omitted", demand.id});
            } else if (demand.necessity == "optional") {
                retained.push_back(demand.id);
            }
        }
        result.optional = retained;
        if (context.metadata_overflow)
            result.findings.push_back({"metadata-overflow-accepted", ""});
    }
    return result;
}

std::string json_escape(const std::string &value) {
    std::string result = "\"";
    for (const char c : value) {
        if (c == '"' || c == '\\') { result.push_back('\\'); result.push_back(c); }
        else if (c == '\n') result += "\\n";
        else if (c == '\r') result += "\\r";
        else if (c == '\t') result += "\\t";
        else result.push_back(c);
    }
    result.push_back('"');
    return result;
}

std::string id_array(const std::vector<std::string> &values) {
    std::vector<std::string> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    std::string result = "[";
    for (size_t i = 0; i < sorted.size(); ++i) {
        if (i) result += ',';
        result += json_escape(sorted[i]);
    }
    result += ']';
    return result;
}

std::string finding_array(const std::vector<Finding> &values) {
    std::string result = "[";
    const size_t count = std::min<size_t>(values.size(), 32);
    for (size_t i = 0; i < count; ++i) {
        if (i) result += ',';
        result += "{\"code\":" + json_escape(values[i].code);
        if (!values[i].demand_id.empty()) result += ",\"demandId\":" + json_escape(values[i].demand_id);
        result += '}';
    }
    result += ']';
    return result;
}

const char *family_name(NativeFamily family) {
    switch (family) {
    case NativeFamily::ArtworkImmersive: return "artwork-immersive";
    case NativeFamily::ArtworkBottomTransport: return "artwork-bottom-transport";
    case NativeFamily::ArtworkSideTransport: return "artwork-side-transport";
    case NativeFamily::ArtworkMetadataBand: return "artwork-metadata-band";
    case NativeFamily::BalancedSplit: return "balanced-split";
    case NativeFamily::MetadataFocus: return "metadata-focus";
    case NativeFamily::TransportFocus: return "transport-focus";
    case NativeFamily::VolumeFocus: return "volume-focus";
    case NativeFamily::ZoneSelection: return "zone-selection";
    case NativeFamily::ListeningConversation: return "listening-conversation";
    case NativeFamily::ReviewConfirmation: return "review-confirmation";
    case NativeFamily::StatusRecovery: return "status-recovery";
    }
    return "none";
}

const char *family_signature(NativeFamily family) {
    switch (family) {
    case NativeFamily::ArtworkImmersive: return "fillScreen+artwork(0,0,320,240)+overlay(8,8)";
    case NativeFamily::ArtworkBottomTransport: return "artwork(8,8,304,154)+metadata(10,164)+transport(4,202,312,34)";
    case NativeFamily::ArtworkSideTransport: return "artwork(8,8,226,224)+side-transport(240,34,72,172)";
    case NativeFamily::ArtworkMetadataBand: return "artwork(8,8,304,144)+metadata-band(0,156,320,84)";
    case NativeFamily::BalancedSplit: return "split-art(8,42,140,144)+content(164,42,148,84)+controls(164,136,148,72)";
    case NativeFamily::MetadataFocus: return "metadata(12,40,296,106)+thumbnail(12,174,72,54)+marquee(96,180,212)";
    case NativeFamily::TransportFocus: return "transport(12,42,296,126)+context(12,184,296,42)";
    case NativeFamily::VolumeFocus: return "volume-arc(160,106,110,96)+level(34,182,252,22)+context(12,32,296,32)";
    case NativeFamily::ZoneSelection: return "zone-list(10,44,300,162)+current(10,44,300,30)+confirm(42,214,236,24)";
    case NativeFamily::ListeningConversation: return "voice-face(0,36,320,112)+transcript(8,154,304,24)+response(32,184,280,24)";
    case NativeFamily::ReviewConfirmation: return "candidate(12,36,296,100)+review(12,142,296,38)+decision(8,190,304,42)";
    case NativeFamily::StatusRecovery: return "recovery-badge(16,44,288,48)+last-good(16,104,288,50)+retry(42,176,236,32)";
    }
    return "none";
}

std::string evidence(const std::string &contract_id, const Admission &admission) {
    const auto hard_finding = [](const Finding &finding) {
        return finding.code != "optional-target-omitted" &&
               finding.code != "optional-emphasis-degraded" &&
               finding.code != "metadata-overflow-accepted";
    };
    const bool accepted = admission.accepted &&
                          std::none_of(admission.findings.begin(),
                                       admission.findings.end(), hard_finding);
    std::string result = "{\"contractId\":" + json_escape(contract_id);
    result += ",\"degradedOptionalDemandIds\":" + id_array(admission.degraded);
    result += ",\"findings\":" + finding_array(admission.findings);
    result += ",\"fulfilledOptionalDemandIds\":" + id_array(admission.optional);
    result += ",\"fulfilledRequiredDemandIds\":" + id_array(admission.required);
    result += ",\"status\":" + json_escape(accepted ? "accepted" : "no-fit");
    result += ",\"version\":" + json_escape(kEvidenceVersion) + '}';
    return result;
}

kizz_semantic_context_t s_context = {};
std::string s_pending_id;
std::string s_active_id;
NativeFamily s_pending_family = NativeFamily::StatusRecovery;
NativeFamily s_active_family = NativeFamily::ArtworkImmersive;
bool s_pending_accepted = false;
bool s_apply_changed = false;

} // namespace

extern "C" void kizz_semantic_set_context(const kizz_semantic_context_t *context) {
    if (context) s_context = *context;
}

extern "C" bool kizz_semantic_admit_json(const char *contract_json, size_t contract_len,
                                          char *evidence_json, size_t evidence_capacity) {
    if (!evidence_json || evidence_capacity == 0) return false;
    Contract contract;
    Admission admission;
    const bool parsed = parse_contract(contract_json, contract_len, &contract);
    if (parsed) {
        /* parse_contract is the single validation pass.  The host owns the
         * canonical SHA-256 digest; firmware validates the lowercase slc1-
         * shape and treats the opaque id as an authenticated handle rather
         * than duplicating host canonicalization here. */
        admission = select(contract, s_context);
    } else {
        admission.findings.push_back({"invalid-contract", ""});
    }
    const std::string contract_id = contract.id.empty() ? "invalid" : contract.id;
    const std::string output = evidence(contract_id, admission);
    if (output.size() + 1 > evidence_capacity) return false;
    std::memcpy(evidence_json, output.c_str(), output.size() + 1);
    const auto hard_finding = [](const Finding &finding) {
        return finding.code != "optional-target-omitted" &&
               finding.code != "optional-emphasis-degraded" &&
               finding.code != "metadata-overflow-accepted";
    };
    if (parsed && admission.accepted && std::none_of(
            admission.findings.begin(), admission.findings.end(), hard_finding)) {
        s_pending_id = contract.id;
        s_pending_family = admission.family;
        s_pending_accepted = true;
    } else {
        s_pending_id.clear();
        s_pending_accepted = false;
    }
    return admission.accepted && std::none_of(
        admission.findings.begin(), admission.findings.end(), hard_finding);
}

extern "C" bool kizz_semantic_apply(const char *contract_id) {
    s_apply_changed = false;
    if (!contract_id || !s_pending_accepted || s_pending_id != contract_id) return false;
    s_apply_changed = s_active_id != s_pending_id || s_active_family != s_pending_family;
    s_active_id = s_pending_id;
    s_active_family = s_pending_family;
    if (s_apply_changed)
        touch_ui_apply_semantic_family(static_cast<uint8_t>(s_active_family) + 1);
    return true;
}

extern "C" bool kizz_semantic_apply_changed(void) { return s_apply_changed; }

extern "C" uint8_t kizz_semantic_active_family_token(void) {
    return static_cast<uint8_t>(s_active_family) + 1;
}

extern "C" const char *kizz_semantic_family_name(uint8_t family_token) {
    if (family_token < 1 || family_token > kFamilyCount) return "none";
    return family_name(static_cast<NativeFamily>(family_token - 1));
}

extern "C" const char *kizz_semantic_family_signature(uint8_t family_token) {
    if (family_token < 1 || family_token > kFamilyCount) return "none";
    return family_signature(static_cast<NativeFamily>(family_token - 1));
}
