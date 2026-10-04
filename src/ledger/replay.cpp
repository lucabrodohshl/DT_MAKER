/**
 * @file replay.cpp
 * @brief Re-execution of recorded inputs and comparison with the recorded fields.
 */
#include "twin/ledger/replay.hpp"

#include <array>
#include <fstream>
#include <sstream>

#include "twin/kernel/semantics.hpp"
#include "twin/ledger/record.hpp"

namespace twin::ledger {
namespace {

using json::Json;

constexpr std::array<std::string_view, 8> kChainKeys = {
    "schema", "session", "seq", "kind", "prev_hash", "package", "kernel_version", "wall_time"};

/// The kind-specific part of a recorded body (chain fields removed).
Json semantic_fields(const Json& body) {
    Json out = body;
    for (std::string_view key : kChainKeys) {
        out.erase(std::string(key));
    }
    return out;
}

/// First differing top-level key between two objects (for readable mismatches).
std::string first_difference(const Json& recorded, const Json& recomputed) {
    for (const auto& [key, value] : recorded.items()) {
        if (!recomputed.contains(key)) return "member '" + key + "' not reproduced";
        if (recomputed.at(key) != value) return "member '" + key + "' differs";
    }
    for (const auto& [key, value] : recomputed.items()) {
        if (!recorded.contains(key)) return "unexpected member '" + key + "'";
    }
    return "records differ";
}

class Replayer {
public:
    Replayer(std::shared_ptr<const kernel::Model> model, kernel::StateSet initial)
        : model_(std::move(model)), state_(std::move(initial)) {}

    void run(std::string_view text, ReplayReport& report) {
        std::size_t pos = 0;
        while (pos < text.size()) {
            std::size_t eol = text.find('\n', pos);
            if (eol == std::string_view::npos) eol = text.size();
            Result<Json> line = json::parse_canonical(text.substr(pos, eol - pos));
            pos = eol + 1;
            if (!line) continue;  // already reported by chain verification
            replay_record(line.value().at("body"), report);
        }
        report.final_state = encode_state(*model_, state_);
    }

private:
    static void mismatch(ReplayReport& r, const Json& body, std::string detail) {
        r.mismatches.push_back(ReplayMismatch{body.value("seq", std::uint64_t{0}),
                                              body.value("kind", std::string()), std::move(detail)});
    }

    static void compare(ReplayReport& r, const Json& body, const Json& recomputed) {
        const Json recorded = semantic_fields(body);
        if (recorded != recomputed) {
            mismatch(r, body, first_difference(recorded, recomputed));
        }
    }

    void replay_record(const Json& body, ReplayReport& r) {
        const std::string kind = body.value("kind", std::string());
        if (kind == kind::kGenesis) {
            compare(r, body, genesis_fields(*model_, state_));
            return;
        }
        if (kind == kind::kAlarm) {
            ++r.alarms;
            compare(r, body,
                    alarm_fields(*model_, body.value("alarm", std::string()), body.value("detail", std::string()),
                                 state_));
            return;
        }
        if (kind == kind::kEnd) {
            compare(r, body, end_fields(*model_, body.value("reason", std::string()), state_));
            return;
        }
        if (!body.contains("input")) {
            mismatch(r, body, "record has no input");
            return;
        }
        Result<Input> input = decode_input(body.at("input"));
        if (!input) {
            mismatch(r, body, "undecodable input: " + input.error().message);
            return;
        }
        // The same function the runtime session uses defines the input's meaning.
        Result<InputEffect> effect = evaluate_input(*model_, state_, input.value());
        if (kind == kind::kReject) {
            ++r.rejections;
            if (effect) {
                mismatch(r, body, "recorded rejection was accepted on replay");
                return;
            }
            compare(r, body, reject_fields(*model_, input.value(), effect.error(), state_));
            return;
        }
        if (kind != kind::kStep && kind != kind::kDelay) {
            mismatch(r, body, "unknown record kind '" + kind + "'");
            return;
        }
        if (!effect) {
            mismatch(r, body, "recorded " + kind + " was refused on replay: " + effect.error().message);
            return;
        }
        if (kind == kind::kStep && effect.value().outcome) {
            ++r.steps;
            compare(r, body, step_fields(*model_, input.value(), *effect.value().outcome));
        } else if (kind == kind::kDelay && !effect.value().outcome) {
            ++r.delays;
            compare(r, body, delay_fields(*model_, input.value(), state_, effect.value().after));
        } else {
            mismatch(r, body, "record kind does not match the input kind");
        }
        state_ = std::move(effect).value().after;
    }

    std::shared_ptr<const kernel::Model> model_;
    kernel::StateSet state_;
};

}  // namespace

Json to_json(const ReplayReport& r) {
    Json mismatches = Json::array();
    for (const ReplayMismatch& m : r.mismatches) {
        mismatches.push_back(Json{{"seq", m.seq}, {"kind", m.kind}, {"detail", m.detail}});
    }
    return Json{{"chain", to_json(r.chain)}, {"identical", r.identical},     {"steps", r.steps},
                {"delays", r.delays},        {"rejections", r.rejections},   {"alarms", r.alarms},
                {"mismatches", mismatches},  {"final_state", r.final_state}};
}

Result<ReplayReport> replay_text(const package::LoadedPackage& package, std::string_view text) {
    ReplayReport report;
    report.chain = verify_text(text, VerifyOptions{package.package_hash, std::nullopt, false});
    if (!report.chain.valid) {
        return report;  // never re-execute an unverified ledger
    }
    Result<std::shared_ptr<const kernel::Model>> model = kernel::Model::create(package.model);
    if (!model) return std::move(model).error();
    Result<kernel::Configuration> init = kernel::initial_configuration(*model.value());
    if (!init) return std::move(init).error();
    Replayer(model.value(), kernel::StateSet::of(std::move(init).value())).run(text, report);
    report.identical = report.mismatches.empty();
    return report;
}

Result<ReplayReport> replay_file(const package::LoadedPackage& package, const std::filesystem::path& ledger) {
    std::ifstream in(ledger, std::ios::binary);
    if (!in) {
        return make_error(ErrorCode::IoError, "cannot read ledger").with("file", ledger.string());
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    return replay_text(package, buf.str());
}

}  // namespace twin::ledger
