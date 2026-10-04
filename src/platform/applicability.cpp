/**
 * @file applicability.cpp
 * @brief Evidence applicability (see applicability.hpp).
 */
#include "twin/platform/applicability.hpp"

namespace twin::platform {

std::string_view to_string(Applicability a) noexcept {
    switch (a) {
        case Applicability::Valid: return "valid";
        case Applicability::Stale: return "stale";
        case Applicability::Invalidated: return "invalidated";
        case Applicability::Unknown: return "unknown";
    }
    return "unknown";
}

Result<ApplicabilityReport> applicability(const EvidenceRecord& evidence, const std::vector<Binding>& bindings,
                                          const ArtifactRepository& artifacts) {
    ApplicabilityReport r;
    bool shared = false;
    for (const auto& in : evidence.inputs) {
        auto v = artifacts.version(in.ref);
        if (v && v.value().state == Lifecycle::Rejected) {
            r.state = Applicability::Invalidated;
            r.reasons.push_back("input " + in.ref.str() + " (" + in.role + ") was rejected");
        }
        for (const auto& b : bindings) {
            if (b.role != in.role) continue;
            shared = true;
            if (b.sha256 != in.sha256) {
                r.changed.push_back({in.role, in.ref.str(), in.sha256, b.ref.str(), b.sha256});
                r.reasons.push_back(in.role + " dependency changed: the evidence examined " + in.ref.str() + " (" +
                                    in.sha256.substr(0, 12) + "…) but " + b.ref.str() + " (" + b.sha256.substr(0, 12) +
                                    "…) is now used");
            }
        }
    }
    if (r.state == Applicability::Invalidated) return r;
    if (!shared) {
        r.state = Applicability::Unknown;
        r.reasons.push_back("the evidence does not examine any artefact this context uses");
        return r;
    }
    r.state = r.changed.empty() ? Applicability::Valid : Applicability::Stale;
    if (r.state == Applicability::Valid) {
        r.reasons.push_back("every examined artefact is byte-identical to the one in use");
    }
    return r;
}

json::Json to_json(const ApplicabilityReport& r) {
    json::Json changed = json::Json::array();
    for (const auto& c : r.changed) {
        changed.push_back({{"role", c.role},
                           {"evidenceRef", c.evidence_ref},
                           {"evidenceSha256", c.evidence_sha256},
                           {"currentRef", c.current_ref},
                           {"currentSha256", c.current_sha256}});
    }
    return {{"state", std::string(to_string(r.state))}, {"changed", changed}, {"reasons", r.reasons}};
}

}  // namespace twin::platform
