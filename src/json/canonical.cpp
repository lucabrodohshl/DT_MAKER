/**
 * @file canonical.cpp
 * @brief Canonical JSON implementation on top of nlohmann::json.
 */
#include "twin/json/canonical.hpp"

#include <algorithm>

#include "twin/core/sha256.hpp"

namespace twin::json {
namespace {

/// Reject floating-point numbers anywhere in the document (recursive).
Status check_no_floats(const Json& value, std::string& path) {
    if (value.is_number_float()) {
        return make_error(ErrorCode::ValidationError,
                          "floating-point numbers are not allowed in canonical documents")
            .with("path", path.empty() ? "/" : path);
    }
    if (value.is_object()) {
        for (const auto& [key, child] : value.items()) {
            const std::size_t saved = path.size();
            path += '/';
            path += key;
            Status s = check_no_floats(child, path);
            path.resize(saved);
            if (!s) {
                return s;
            }
        }
    } else if (value.is_array()) {
        for (std::size_t i = 0; i < value.size(); ++i) {
            const std::size_t saved = path.size();
            path += '/';
            path += std::to_string(i);
            Status s = check_no_floats(value[i], path);
            path.resize(saved);
            if (!s) {
                return s;
            }
        }
    }
    return ok_status();
}

std::string type_name(const Json& value) { return std::string(value.type_name()); }

}  // namespace

Result<std::string> canonical_dump(const Json& value) {
    std::string path;
    if (Status s = check_no_floats(value, path); !s) {
        return s.error();
    }
    try {
        // indent = -1 (compact), ensure_ascii = false, strict UTF-8 handling.
        return value.dump(-1, ' ', false, Json::error_handler_t::strict);
    } catch (const nlohmann::json::exception& e) {
        return make_error(ErrorCode::ValidationError, "cannot serialise document")
            .with("detail", e.what());
    }
}

Result<std::string> canonical_sha256(const Json& value) {
    Result<std::string> text = canonical_dump(value);
    if (!text) {
        return std::move(text).error();
    }
    return sha256_hex(text.value());
}

Result<Json> parse(std::string_view text) {
    try {
        return Json::parse(text.begin(), text.end());
    } catch (const nlohmann::json::parse_error& e) {
        return make_error(ErrorCode::ParseError, "malformed JSON")
            .with("byte", std::to_string(e.byte))
            .with("detail", e.what());
    } catch (const nlohmann::json::exception& e) {
        return make_error(ErrorCode::ParseError, "malformed JSON").with("detail", e.what());
    }
}

Result<Json> parse_canonical(std::string_view text) {
    Result<Json> parsed = parse(text);
    if (!parsed) {
        return parsed;
    }
    Result<std::string> again = canonical_dump(parsed.value());
    if (!again) {
        return std::move(again).error();
    }
    if (again.value() != text) {
        // Locate the first differing byte to make the diagnostic actionable.
        const auto mismatch = std::mismatch(text.begin(), text.end(), again.value().begin(),
                                            again.value().end());
        const auto offset = static_cast<std::size_t>(mismatch.first - text.begin());
        return make_error(ErrorCode::IntegrityError,
                          "document is not in canonical JSON form (duplicate keys, whitespace, "
                          "key order or number formatting differ)")
            .with("first_difference_at_byte", std::to_string(offset));
    }
    return parsed;
}

Result<const Json*> member(const Json& object, std::string_view key) {
    if (!object.is_object()) {
        return make_error(ErrorCode::ValidationError, "expected a JSON object")
            .with("found", type_name(object));
    }
    const auto it = object.find(key);
    if (it == object.end()) {
        return make_error(ErrorCode::ValidationError, "missing required member")
            .with("member", std::string(key));
    }
    return &*it;
}

Result<std::string> get_string(const Json& object, std::string_view key) {
    Result<const Json*> m = member(object, key);
    if (!m) {
        return std::move(m).error();
    }
    if (!m.value()->is_string()) {
        return make_error(ErrorCode::ValidationError, "member must be a string")
            .with("member", std::string(key))
            .with("found", type_name(*m.value()));
    }
    return m.value()->get<std::string>();
}

Result<std::int64_t> get_int(const Json& object, std::string_view key) {
    Result<const Json*> m = member(object, key);
    if (!m) {
        return std::move(m).error();
    }
    const Json& v = *m.value();
    if (v.is_number_unsigned()) {
        const auto u = v.get<std::uint64_t>();
        if (u > static_cast<std::uint64_t>(INT64_MAX)) {
            return make_error(ErrorCode::ArithmeticOverflow, "integer member out of range")
                .with("member", std::string(key));
        }
        return static_cast<std::int64_t>(u);
    }
    if (v.is_number_integer()) {
        return v.get<std::int64_t>();
    }
    return make_error(ErrorCode::ValidationError, "member must be an integer")
        .with("member", std::string(key))
        .with("found", type_name(v));
}

Result<bool> get_bool(const Json& object, std::string_view key) {
    Result<const Json*> m = member(object, key);
    if (!m) {
        return std::move(m).error();
    }
    if (!m.value()->is_boolean()) {
        return make_error(ErrorCode::ValidationError, "member must be a boolean")
            .with("member", std::string(key));
    }
    return m.value()->get<bool>();
}

Result<const Json*> get_array(const Json& object, std::string_view key) {
    Result<const Json*> m = member(object, key);
    if (!m) {
        return m;
    }
    if (!m.value()->is_array()) {
        return make_error(ErrorCode::ValidationError, "member must be an array")
            .with("member", std::string(key));
    }
    return m;
}

Result<const Json*> get_object(const Json& object, std::string_view key) {
    Result<const Json*> m = member(object, key);
    if (!m) {
        return m;
    }
    if (!m.value()->is_object()) {
        return make_error(ErrorCode::ValidationError, "member must be an object")
            .with("member", std::string(key));
    }
    return m;
}

Status expect_keys(const Json& object, const std::vector<std::string_view>& required,
                   const std::vector<std::string_view>& optional) {
    if (!object.is_object()) {
        return make_error(ErrorCode::ValidationError, "expected a JSON object");
    }
    for (std::string_view key : required) {
        if (!object.contains(key)) {
            return make_error(ErrorCode::ValidationError, "missing required member")
                .with("member", std::string(key));
        }
    }
    for (const auto& [key, unused] : object.items()) {
        const bool known =
            std::find(required.begin(), required.end(), key) != required.end() ||
            std::find(optional.begin(), optional.end(), key) != optional.end();
        if (!known) {
            return make_error(ErrorCode::ValidationError, "unknown member")
                .with("member", key);
        }
    }
    return ok_status();
}

}  // namespace twin::json
