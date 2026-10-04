/**
 * @file pt_adapter.hpp
 * @brief Translation of Physical-Twin events into Digital-Twin observations.
 * @ingroup runtime
 *
 * The flight controller speaks the PT vocabulary of V_P ("wp_arrived!",
 * "poi_arrived!", ...). The adapter translates each event into a DT label using
 * the label-equivalence relation E recorded in the package's alignment
 * evidence (E pairs labels whose ontology interpretations are equivalent under
 * the domain axioms, decided by Z3). The translation table is therefore
 * derived from the verified semantic alignment, not written by hand.
 *
 * The adapter performs no semantic decisions: it produces candidate
 * observations, and the kernel accepts or rejects them.
 */
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "twin/core/result.hpp"
#include "twin/json/canonical.hpp"
#include "twin/ledger/record.hpp"

namespace twin::runtime {

/// @brief Result of translating one PT event.
struct Translation {
    std::optional<ledger::Input> input;  ///< DT observation, if the PT label is mapped.
    std::string pt_label;                ///< Original PT label.
    std::string note;                    ///< Why it was not translated (if not).
};

/// @brief E-driven PT -> DT event translation (see file documentation).
class PtAdapter {
public:
    /**
     * @brief Build the table from alignment evidence ("label_equivalence").
     * PT labels with no DT equivalent, or more than one, are left untranslated
     * (reported by translate()) rather than guessed.
     */
    [[nodiscard]] static Result<PtAdapter> from_evidence(const json::Json& alignment_evidence);

    /// @brief Translate a PT event {"at", "label", "detail"} into a DT input.
    [[nodiscard]] Translation translate(const json::Json& pt_event) const;

    /// @brief The translation table (PT label -> DT label).
    [[nodiscard]] const std::map<std::string, std::string>& table() const noexcept { return table_; }

private:
    std::map<std::string, std::string> table_;
};

}  // namespace twin::runtime
