// Internal: validation of a resolved model (TML diagnostics, 04-compilation.md section 4.7).
#pragma once

#include <string>
#include <vector>

#include "resolve.hpp"

namespace twin::model::detail {

/// Append every validation finding of @p document to @p diagnostics.
void validate(const Document& document, const Resolved& resolved, std::vector<Diagnostic>& diagnostics);

/// Exact comparison of two decimals: -1, 0 or 1.
[[nodiscard]] int compare_decimal(const std::string& a, const std::string& b);

}  // namespace twin::model::detail
