// Internal: the behaviour compiler (04-compilation.md, section 4.5).
#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "resolve.hpp"
#include "twin/authoring/model.hpp"
#include "twin/json/canonical.hpp"

namespace twin::model::detail {

/// Where a generated element comes from.
struct Source {
    std::string id;    // model element id
    std::string rule;  // rule id, e.g. "B2.timeout"
};

/// What a channel of V_D stands for.
struct ChannelInfo {
    enum class Kind { Event, Action, Timeout };
    std::string name;                  // channel name
    Kind kind{Kind::Event};
    const Instance* owner{nullptr};    // asset owning the member (or the state, for timeouts)
    const Event* event{nullptr};
    const Action* action{nullptr};
    const State* state{nullptr};       // timeout source state
    char direction{'!'};
};

/// A location of V_D and the states it combines.
struct LocationInfo {
    std::string name;
    std::vector<std::pair<const Instance*, const State*>> components;  // machine order
};

struct BehaviorOutput {
    std::optional<authoring::Model> automaton;
    std::vector<LocationInfo> locations;   // automaton order
    std::vector<ChannelInfo> channels;     // declaration order
    json::Json trace = json::Json::array();
    std::size_t machines{0};
};

/// Compile every behaviour of the model into V_D. Findings go to @p diagnostics.
[[nodiscard]] BehaviorOutput compile_behavior(const Resolved& model, std::vector<Diagnostic>& diagnostics);

/// Trace entry JSON.
[[nodiscard]] json::Json trace_entry(const std::string& kind, const std::string& name, const std::vector<Source>& sources);

}  // namespace twin::model::detail
