/**
 * @file http.cpp
 * @brief Serving an engine route table with cpp-httplib.
 */
#include "twin/studio_engine/http.hpp"

namespace twin::studio_engine {
namespace {

void send(httplib::Response& res, int status, const json::Json& body) {
    res.status = status;
    res.set_content(body.dump(-1, ' ', false, json::Json::error_handler_t::replace), "application/json; charset=utf-8");
}

}  // namespace

void mount(httplib::Server& server, const std::vector<Route>& routes) {
    for (const Route& route : routes) {
        auto handler = [route](const httplib::Request& http, httplib::Response& res) {
            Request req;
            for (const auto& [k, v] : http.path_params) req.path[k] = v;
            for (const auto& [k, v] : http.params) req.query[k] = v;
            if (http.has_header("X-Twin-Actor")) req.actor = http.get_header_value("X-Twin-Actor");
            if (!http.body.empty()) {
                Result<json::Json> body = json::parse(http.body);
                if (!body) {
                    send(res, 400, error_body(make_error(ErrorCode::ParseError, "request body is not valid JSON")));
                    return;
                }
                req.body = std::move(body).value();
            }
            Result<json::Json> out = route.handler(req);
            if (out) send(res, route.ok_status, out.value());
            else send(res, http_status(out.error().code), error_body(out.error()));
        };
        if (route.method == "GET") server.Get(route.pattern, handler);
        else if (route.method == "POST") server.Post(route.pattern, handler);
        else if (route.method == "DELETE") server.Delete(route.pattern, handler);
    }
}

}  // namespace twin::studio_engine
