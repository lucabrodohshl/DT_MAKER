/**
 * @file ledger_command.cpp
 * @brief `twin ledger verify|anchor|show` and `twin replay`.
 */
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "cli.hpp"
#include "commands.hpp"
#include "twin/ledger/replay.hpp"
#include "twin/ledger/verifier.hpp"
#include "twin/package/package.hpp"

namespace twin::cli {
namespace {

constexpr const char* kLedgerHelp =
    "twin ledger verify <ledger.jsonl> [--package <dir>] [--anchor <anchor.json>] [--require-end] [--json]\n"
    "twin ledger anchor <ledger.jsonl> [--out <anchor.json>]\n"
    "twin ledger show <ledger.jsonl> [--last <n>]\n\n"
    "verify  check the hash chain, sequence continuity, identity and (optionally) an anchor\n"
    "anchor  print the head {seq, hash} to keep in external (e.g. WORM) storage\n"
    "show    print a compact view of the records";

constexpr const char* kReplayHelp =
    "twin replay --package <dir> --ledger <ledger.jsonl> [--json]\n\n"
    "Verifies the package and the ledger, re-executes every recorded input with the\n"
    "semantic kernel and checks that every recorded state, transition and rejection is\n"
    "reproduced exactly. Exit code 0 iff the replay is identical.";

Result<std::string> read_text(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return make_error(ErrorCode::IoError, "cannot read file").with("file", path);
    std::ostringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

void print_report(const ledger::VerificationReport& r) {
    for (const ledger::Issue& i : r.issues) {
        std::cout << "  [FAIL] line " << i.line << " " << i.code << ": " << i.message << "\n";
    }
    std::cout << (r.valid ? "LEDGER VALID" : "LEDGER INVALID") << " — " << r.records << " records, session "
              << r.session << "\n"
              << "  package " << r.package_hash << "\n"
              << "  head    seq " << r.head_seq << " " << r.head_hash << "\n"
              << "  end record: " << (r.has_end_record ? "present" : "absent (session open or truncated)") << "\n";
}

int verify(int argc, char** argv) {
    Args args(argc, argv, 0, {"package", "anchor"});
    if (args.error()) return usage_error(*args.error(), kLedgerHelp);
    if (args.positionals().size() != 1) return usage_error("expected a ledger file", kLedgerHelp);
    ledger::VerifyOptions opts;
    opts.require_end_record = args.flag("require-end");
    if (auto dir = args.option("package")) {
        Result<package::LoadedPackage> pkg = package::load_and_verify(*dir, package::VerifyOptions{true});
        if (!pkg) {
            std::cerr << "error: package: " << pkg.error().to_string() << "\n";
            return kInputError;
        }
        opts.expected_package_hash = pkg.value().package_hash;
    }
    if (auto anchor_path = args.option("anchor")) {
        Result<std::string> text = read_text(*anchor_path);
        if (!text) {
            std::cerr << "error: " << text.error().to_string() << "\n";
            return kInputError;
        }
        Result<json::Json> j = json::parse(text.value());
        Result<ledger::Anchor> anchor = j ? ledger::anchor_from_json(j.value()) : Result<ledger::Anchor>(j.error());
        if (!anchor) {
            std::cerr << "error: anchor: " << anchor.error().to_string() << "\n";
            return kInputError;
        }
        opts.anchor = anchor.value();
    }
    const ledger::VerificationReport r = ledger::verify_file(args.positionals()[0], opts);
    if (args.flag("json")) {
        std::cout << ledger::to_json(r).dump(2) << "\n";
    } else {
        print_report(r);
    }
    return r.valid ? kOk : kFailure;
}

int anchor(int argc, char** argv) {
    Args args(argc, argv, 0, {"out"});
    if (args.positionals().size() != 1) return usage_error("expected a ledger file", kLedgerHelp);
    const ledger::VerificationReport r = ledger::verify_file(args.positionals()[0]);
    if (!r.valid) {
        print_report(r);
        std::cerr << "refusing to anchor an invalid ledger\n";
        return kFailure;
    }
    const std::string text = ledger::to_json(ledger::Anchor{r.session, r.head_seq, r.head_hash}).dump() + "\n";
    if (auto out = args.option("out")) {
        std::ofstream(*out, std::ios::binary | std::ios::trunc) << text;
    }
    std::cout << text;
    return kOk;
}

int show(int argc, char** argv) {
    Args args(argc, argv, 0, {"last"});
    if (args.positionals().size() != 1) return usage_error("expected a ledger file", kLedgerHelp);
    Result<std::string> text = read_text(args.positionals()[0]);
    if (!text) {
        std::cerr << "error: " << text.error().to_string() << "\n";
        return kInputError;
    }
    std::vector<json::Json> lines;
    std::istringstream in(text.value());
    std::string line;
    while (std::getline(in, line)) {
        Result<json::Json> j = json::parse(line);
        if (j) lines.push_back(std::move(j).value());
    }
    std::size_t last = lines.size();
    if (auto n = args.option("last")) last = std::min<std::size_t>(lines.size(), std::stoul(*n));
    for (std::size_t i = lines.size() - last; i < lines.size(); ++i) {
        const json::Json& body = lines[i].at("body");
        std::string what;
        if (body.contains("outcome") && !body.at("outcome").at("branches").empty()) {
            const json::Json& b = body.at("outcome").at("branches").at(0);
            what = b.at("source").get<std::string>() + " --" + b.at("label").get<std::string>() + "--> " +
                   b.at("target").get<std::string>();
        } else if (body.contains("error")) {
            what = "rejected " + body.at("input").at("name").get<std::string>() + ": " +
                   body.at("error").at("code").get<std::string>();
        } else if (body.contains("alarm")) {
            what = "alarm " + body.at("alarm").get<std::string>();
        }
        std::cout << std::setw(5) << body.value("seq", 0) << "  " << std::setw(7) << body.value("kind", "")
                  << "  t=" << std::setw(9) << body.value("time_after", std::int64_t{0}) << "  "
                  << body.value("prev_hash", "").substr(0, 10) << " -> "
                  << lines[i].value("hash", "").substr(0, 10) << "  " << what << "\n";
    }
    return kOk;
}

}  // namespace

int run_ledger(int argc, char** argv) {
    if (argc < 1) return usage_error("missing subcommand", kLedgerHelp);
    const std::string sub = argv[0];
    if (sub == "verify") return verify(argc - 1, argv + 1);
    if (sub == "anchor") return anchor(argc - 1, argv + 1);
    if (sub == "show") return show(argc - 1, argv + 1);
    if (sub == "--help" || sub == "help") {
        std::cout << kLedgerHelp << "\n";
        return kOk;
    }
    return usage_error("unknown subcommand '" + sub + "'", kLedgerHelp);
}

int run_replay(int argc, char** argv) {
    Args args(argc, argv, 0, {"package", "ledger"});
    if (args.error()) return usage_error(*args.error(), kReplayHelp);
    if (!args.option("package") || !args.option("ledger")) {
        return usage_error("--package and --ledger are required", kReplayHelp);
    }
    Result<package::LoadedPackage> pkg = package::load_and_verify(*args.option("package"));
    if (!pkg) {
        std::cerr << "error: package: " << pkg.error().to_string() << "\n";
        return kInputError;
    }
    Result<ledger::ReplayReport> r = ledger::replay_file(pkg.value(), *args.option("ledger"));
    if (!r) {
        std::cerr << "error: " << r.error().to_string() << "\n";
        return kInputError;
    }
    const ledger::ReplayReport& rep = r.value();
    if (args.flag("json")) {
        std::cout << ledger::to_json(rep).dump(2) << "\n";
    } else {
        if (!rep.chain.valid) print_report(rep.chain);
        for (const ledger::ReplayMismatch& m : rep.mismatches) {
            std::cout << "  [DIFF] seq " << m.seq << " (" << m.kind << "): " << m.detail << "\n";
        }
        std::cout << (rep.chain.valid && rep.identical ? "REPLAY IDENTICAL" : "REPLAY DIFFERS") << " — "
                  << rep.steps << " steps, " << rep.delays << " delays, " << rep.rejections << " rejections, "
                  << rep.alarms << " alarms re-executed\n";
    }
    return rep.chain.valid && rep.identical ? kOk : kFailure;
}

}  // namespace twin::cli
