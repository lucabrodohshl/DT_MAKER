/**
 * @file world_port.cpp
 * @brief Transport-independent interpretation of world-API replies.
 */
#include "twin/runtime/world_port.hpp"

namespace twin::runtime {

Result<json::Json> world_reply(int status, json::Json body, const std::string& route) {
    if (status >= 200 && status < 300) return body;
    const json::Json error = body.is_object() ? body.value("error", json::Json::object()) : json::Json::object();
    const std::string code_name = error.is_object() ? error.value("code", std::string()) : std::string();
    const ErrorCode code = error_code_from_string(code_name).value_or(ErrorCode::Unavailable);
    const std::string message =
        error.is_object() ? error.value("message", std::string("request refused")) : std::string("request refused");
    return make_error(code, message).with("route", route).with("status", std::to_string(status));
}

}  // namespace twin::runtime
