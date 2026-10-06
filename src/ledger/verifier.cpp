/**
 * @file verifier.cpp
 * @brief Line-by-line ledger verification.
 *
 * ChainChecker consumes the ledger one line at a time and checks, per record:
 * canonical form and shape, schema, sequence continuity, the hash chain
 * (prev_hash link and recomputed hash), identity (session and package), the
 * position of the genesis and end records, and an optional anchor.
 */
#include "twin/ledger/verifier.hpp"

#include <fstream>
#include <sstream>

#include "twin/core/sha256.hpp"
#include "twin/core/version.hpp"
#include "twin/ledger/record.hpp"

namespace twin::ledger {
namespace {

using json::Json;

std::string str(const Json& j, const char* key) {
    return j.is_object() && j.contains(key) && j.at(key).is_string() ? j.at(key).get<std::string>()
                                                                      : std::string();
}

class ChainChecker {
public:
    explicit ChainChecker(const VerifyOptions& options) : options_(options) {}

    void consume(std::string_view line) {
        ++line_no_;
        ++report_.records;
        if (report_.has_end_record) {
            issue("record_after_end", "records follow the session's end record");
        }
        Json body;
        std::string hash;
        if (!parse(line, body, hash)) {
            chain_ok_ = false;
            return;
        }
        check_schema_and_sequence(body);
        check_chain(body, hash);
        check_identity(body);
        check_anchor(hash);
        if (chain_ok_) {
            report_.head_seq = expected_seq_;
            report_.head_hash = hash;
        }
        prev_hash_ = hash;
        ++expected_seq_;
    }

    void torn_tail() { issue("unterminated_line", "last line has no terminating newline (torn write?)"); }

    VerificationReport finish() && {
        if (report_.records == 0) global_issue("empty", "the ledger is empty");
        if (options_.anchor && !anchor_seen_) {
            global_issue("anchor_missing", "the anchored record (seq " + std::to_string(options_.anchor->seq) +
                                               ") is missing (ledger truncated)");
        }
        if (options_.require_end_record && !report_.has_end_record) {
            global_issue("no_end_record",
                         "no end record: the ledger was truncated or the session is still running");
        }
        report_.valid = report_.issues.empty();
        return std::move(report_);
    }

private:
    void issue(std::string code, std::string message) {
        report_.issues.push_back(Issue{line_no_, std::move(code), std::move(message)});
    }
    void global_issue(std::string code, std::string message) {
        report_.issues.push_back(Issue{0, std::move(code), std::move(message)});
    }

    bool parse(std::string_view line, Json& body, std::string& hash) {
        Result<Json> parsed = json::parse_canonical(line);
        if (!parsed) {
            issue("not_canonical", "line is not canonical JSON: " + parsed.error().message);
            return false;
        }
        const Json& obj = parsed.value();
        const bool shape = obj.is_object() && obj.size() == 2 && obj.contains("body") &&
                           obj.contains("hash") && obj.at("body").is_object() &&
                           is_canonical_hex_digest(str(obj, "hash"));
        if (!shape) {
            issue("malformed_record", R"(expected {"body": {...}, "hash": <hex>})");
            return false;
        }
        body = obj.at("body");
        hash = str(obj, "hash");
        return true;
    }

    void check_schema_and_sequence(const Json& body) {
        if (str(body, "schema") != version::kLedgerSchema) {
            issue("schema", "unsupported record schema '" + str(body, "schema") + "'");
        }
        const Json seq = body.contains("seq") ? body.at("seq") : Json();
        const bool ok = seq.is_number_integer() && seq.get<std::int64_t>() >= 0 &&
                        static_cast<std::uint64_t>(seq.get<std::int64_t>()) == expected_seq_;
        if (!ok) {
            issue("sequence_gap", "expected seq " + std::to_string(expected_seq_) + ", found " + seq.dump() +
                                      " (record inserted, deleted or reordered)");
        }
    }

    void check_chain(const Json& body, const std::string& hash) {
        if (str(body, "prev_hash") != prev_hash_) {
            issue("chain_broken", "prev_hash does not match the previous record's hash");
            chain_ok_ = false;
        }
        Result<std::string> text = json::canonical_dump(body);
        Result<std::string> recomputed = text ? chain_hash(prev_hash_, text.value())
                                              : Result<std::string>(text.error());
        if (!recomputed || recomputed.value() != hash) {
            issue("hash_mismatch", "record content does not match its hash (record modified)");
            chain_ok_ = false;
        }
    }

    void check_identity(const Json& body) {
        const std::string kind = str(body, "kind");
        const std::string package = body.contains("package") ? str(body.at("package"), "hash") : "";
        if (line_no_ == 1) {
            if (kind != kind::kGenesis) issue("no_genesis", "the first record must be the genesis record");
            report_.session = str(body, "session");
            report_.package_hash = package;
        } else {
            if (kind == kind::kGenesis) issue("second_genesis", "a ledger has exactly one genesis record");
            if (str(body, "session") != report_.session) issue("session_mismatch", "record belongs to another session");
            if (package != report_.package_hash) issue("package_mismatch", "record names a different package");
        }
        if (options_.expected_package_hash && package != *options_.expected_package_hash) {
            issue("wrong_package", "record does not belong to the expected package");
        }
        if (kind == kind::kEnd) report_.has_end_record = true;
    }

    void check_anchor(const std::string& hash) {
        if (!options_.anchor || options_.anchor->seq != expected_seq_) return;
        anchor_seen_ = true;
        if (options_.anchor->hash != hash || options_.anchor->session != report_.session) {
            issue("anchor_mismatch", "record differs from the anchored record (ledger rewritten)");
        }
    }

    const VerifyOptions& options_;
    VerificationReport report_;
    std::string prev_hash_{genesis_prev_hash()};
    std::uint64_t expected_seq_{0};
    std::uint64_t line_no_{0};
    bool chain_ok_{true};
    bool anchor_seen_{false};
};

/// Increment the first decimal digit after @p key within [from, to) of @p text.
bool bump_digit_after(std::string& text, std::string_view key, std::size_t from, std::size_t to) {
    const std::size_t at = text.find(key, from);
    if (at == std::string::npos || at >= to) return false;
    for (std::size_t i = at + key.size(); i < to; ++i) {
        if (text[i] >= '0' && text[i] <= '9') {
            text[i] = text[i] == '9' ? '8' : static_cast<char>(text[i] + 1);
            return true;
        }
    }
    return false;
}

}  // namespace

json::Json to_json(const Anchor& a) {
    return Json{{"format", "twin-ledger-anchor/1"}, {"session", a.session}, {"seq", a.seq}, {"hash", a.hash}};
}

Result<Anchor> anchor_from_json(const json::Json& j) {
    if (Status s = json::expect_keys(j, {"format", "session", "seq", "hash"}); !s) return s.error();
    if (str(j, "format") != "twin-ledger-anchor/1") {
        return make_error(ErrorCode::ValidationError, "unsupported anchor format");
    }
    Result<std::int64_t> seq = json::get_int(j, "seq");
    if (!seq || seq.value() < 0) return make_error(ErrorCode::ValidationError, "bad anchor seq");
    return Anchor{str(j, "session"), static_cast<std::uint64_t>(seq.value()), str(j, "hash")};
}

Json to_json(const VerificationReport& r) {
    Json issues = Json::array();
    for (const Issue& i : r.issues) {
        issues.push_back(Json{{"line", i.line}, {"code", i.code}, {"message", i.message}});
    }
    return Json{{"valid", r.valid},           {"records", r.records},
                {"session", r.session},       {"package_hash", r.package_hash},
                {"head_seq", r.head_seq},     {"head_hash", r.head_hash},
                {"has_end_record", r.has_end_record}, {"issues", issues}};
}

VerificationReport verify_text(std::string_view text, const VerifyOptions& options) {
    ChainChecker checker(options);
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t eol = text.find('\n', pos);
        const bool torn = eol == std::string_view::npos;
        if (torn) eol = text.size();
        checker.consume(text.substr(pos, eol - pos));
        if (torn) checker.torn_tail();
        pos = eol + 1;
    }
    return std::move(checker).finish();
}

VerificationReport verify_file(const std::filesystem::path& path, const VerifyOptions& options) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        VerificationReport r;
        r.issues.push_back(Issue{0, "unreadable", "cannot read " + path.string()});
        return r;
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    return verify_text(buf.str(), options);
}

VerificationReport tamper_drill(std::string_view text, std::uint64_t line_index) {
    std::string copy(text);
    std::size_t start = 0;
    for (std::uint64_t i = 0; i < line_index && start != std::string::npos; ++i) {
        start = copy.find('\n', start);
        if (start != std::string::npos) ++start;
    }
    if (start != std::string::npos && start < copy.size()) {
        const std::size_t eol = std::min(copy.find('\n', start), copy.size());
        // The kind of silent edit an attacker would try: "it happened a bit later".
        if (!bump_digit_after(copy, "\"time_after\":", start, eol)) {
            (void)bump_digit_after(copy, "\"seq\":", start, eol);
        }
    }
    return verify_text(copy);
}

}  // namespace twin::ledger
