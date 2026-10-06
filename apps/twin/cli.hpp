/**
 * @file cli.hpp
 * @brief Minimal, dependency-free command-line parsing for the `twin` tool.
 */
#pragma once

#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace twin::cli {

/// @brief Exit codes of the `twin` tool (stable; used by scripts and CI).
enum ExitCode : int {
    kOk = 0,              ///< Success.
    kFailure = 1,         ///< The operation ran and its verdict is negative (e.g. tampered ledger).
    kUsageError = 2,      ///< Invalid command line.
    kInputError = 3,      ///< Unreadable or malformed input.
};

/**
 * @brief Parsed arguments: positionals plus `--name value` options and `--flag` switches.
 */
class Args {
public:
    /// @brief Parse argv[first..] given the set of options that take a value.
    Args(int argc, char** argv, int first, const std::set<std::string>& valued_options) {
        for (int i = first; i < argc; ++i) {
            std::string a = argv[i];
            if (a.starts_with("--")) {
                const std::string name = a.substr(2);
                if (valued_options.contains(name)) {
                    if (i + 1 >= argc) {
                        error_ = "option --" + name + " requires a value";
                        return;
                    }
                    options_[name] = argv[++i];
                } else {
                    flags_.insert(name);
                }
            } else {
                positionals_.push_back(std::move(a));
            }
        }
    }

    /// @brief Parse error, if the command line was malformed.
    [[nodiscard]] const std::optional<std::string>& error() const { return error_; }
    /// @brief Arguments that are neither options nor flags, in order.
    [[nodiscard]] const std::vector<std::string>& positionals() const { return positionals_; }
    /// @brief True iff the flag @p name (e.g. "--overwrite") was given.
    [[nodiscard]] bool flag(const std::string& name) const { return flags_.contains(name); }
    /// @brief Value of option @p name (e.g. "--out"), if given.
    [[nodiscard]] std::optional<std::string> option(const std::string& name) const {
        const auto it = options_.find(name);
        if (it == options_.end()) return std::nullopt;
        return it->second;
    }
    /// @brief Value of option @p name, or @p fallback.
    [[nodiscard]] std::string option_or(const std::string& name, const std::string& fallback) const {
        return option(name).value_or(fallback);
    }
    /// @brief Unknown switches (flags not in @p known), for strict validation.
    [[nodiscard]] std::vector<std::string> unknown_flags(const std::set<std::string>& known) const {
        std::vector<std::string> out;
        for (const std::string& f : flags_) {
            if (!known.contains(f)) out.push_back(f);
        }
        return out;
    }

private:
    std::vector<std::string> positionals_;
    std::map<std::string, std::string> options_;
    std::set<std::string> flags_;
    std::optional<std::string> error_;
};

/// @brief Print an error to stderr and return kUsage.
inline int usage_error(const std::string& message, const std::string& usage) {
    std::cerr << "error: " << message << "\n\nusage: " << usage << "\n";
    return kUsageError;
}

}  // namespace twin::cli
