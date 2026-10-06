/**
 * @file http.hpp
 * @brief Serving an engine route table with cpp-httplib (tests and tools; Studio uses its own wrapper).
 * @ingroup studio_engine
 */
#pragma once

#include <httplib.h>

#include <vector>

#include "twin/studio_engine/engine.hpp"

namespace twin::studio_engine {

/**
 * @brief Register every route of @p routes on @p server.
 *
 * Bodies are parsed as JSON (400 `parse_error` if not JSON), path and query
 * parameters are passed through, `X-Twin-Actor` becomes Request::actor, and
 * errors use error_body() with http_status().
 */
void mount(httplib::Server& server, const std::vector<Route>& routes);

}  // namespace twin::studio_engine
