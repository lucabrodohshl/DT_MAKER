/**
 * @file blueprints_impl.hpp
 * @brief (private) Shared state and helpers of twin::studio::BlueprintService, split across blueprints_*.cpp.
 * @ingroup studio
 */
#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "services_impl.hpp"
#include "twin/kernel/model.hpp"
#include "twin/kernel/state_set.hpp"
#include "twin/platform/blueprints.hpp"
#include "twin/studio/blueprints.hpp"

namespace twin::studio {

/// @brief A compiled DT view, cached by the toolchain bytes' hash (the kernel model is immutable).
struct CompiledDt {
    std::shared_ptr<const kernel::Model> model;  ///< Kernel model of the IR.
    std::string ir_sha256;                       ///< IR hash.
    std::string source_sha256;                   ///< Hash of the compiled toolchain rendering.
};

/// @brief A kernel state set from a state view ({configurations: [{location, clocks, time}]}, times as
/// decimal strings or {text, ticks}), e.g. a what-if state or a runtime's GET /runtime/state.
[[nodiscard]] Result<kernel::StateSet> state_set_from_json(const kernel::Model& model, const json::Json& state);

/// @brief One finding of the Blueprint validator (sections other than formal artefacts).
struct SectionFinding {
    std::string section;   ///< "structure", "world", ...
    std::string severity;  ///< "error" or "warning".
    std::string code;      ///< TWB0xx (Blueprint), TWW/TWS (world), TWN (monitors).
    std::string message;   ///< Actionable text.
    std::string target;    ///< Element id it concerns ("" for the section).
    std::string path;      ///< JSON path inside the section.
};

/// @brief Shared state (see blueprints.hpp).
struct BlueprintService::Impl {
    BlueprintService& self;
    Services& services;
    Services::Impl& core;
    std::filesystem::path templates_dir;
    std::unique_ptr<platform::BlueprintRepository> repo;

    std::mutex compiled_mu;
    std::map<std::string, std::shared_ptr<const CompiledDt>> compiled;  ///< by toolchain-bytes hash

    Impl(BlueprintService& s, Services& svc, std::filesystem::path templates);

    // Plant connection (blueprints_connection.cpp).
    /// @brief Check and complete a requested plant connection of instance @p id (ports allocated
    /// or kept, token kept or issued, broker password stored apart); @p previous is the current one.
    [[nodiscard]] Result<json::Json> prepare_connection(const json::Json& requested, const std::string& id, const json::Json& previous);
    /// @brief Write the twin-ingest configuration of @p t for the data contract of @p document
    /// (mode 0600: it holds the token); none when the instance has no active plant connection.
    [[nodiscard]] Result<std::optional<std::filesystem::path>> write_ingest_config(const platform::Twin& t, const json::Json& document);
    /// @brief Point the instance's telemetry channels at the plant fields its bindings select.
    [[nodiscard]] Status sync_channels(const platform::Twin& t, const json::Json& document);

    /// @brief A version (NotFound otherwise); the caller holds core.lock() or tolerates races.
    [[nodiscard]] Result<platform::BlueprintVersion> load(std::string_view id, std::int64_t version);
    /// @brief A draft version whose revision is @p revision (StateError otherwise).
    [[nodiscard]] Result<platform::BlueprintVersion> load_draft(std::string_view id, std::int64_t version,
                                                                std::optional<std::int64_t> revision);
    /// @brief An editable (open) artefact version for @p role in @p v, creating the artefact or a draft
    /// version as needed; updates @p v.pins. Interpretations are kept referring to the pinned ontology.
    [[nodiscard]] Result<platform::ArtifactRef> ensure_editable(platform::BlueprintVersion& v, std::string_view role,
                                                                const Actor& actor);
    /// @brief Persist @p v's document and pins (revision check against @p v.revision).
    [[nodiscard]] Result<platform::BlueprintVersion> store(const platform::BlueprintVersion& v, const Actor& actor);
    /// @brief Bindings of all five roles (InvalidArgument naming the missing roles).
    [[nodiscard]] Result<std::vector<platform::Binding>> bindings(const platform::BlueprintVersion& v);
    /// @brief Bindings of the roles that are pinned.
    [[nodiscard]] std::vector<platform::Binding> pinned_bindings(const platform::BlueprintVersion& v);
    /// @brief Compile the pinned DT view (cached); CompileFailure diagnostics as an error context.
    [[nodiscard]] Result<std::shared_ptr<const CompiledDt>> compile_dt(const platform::BlueprintVersion& v);
    /// @brief Every non-formal section's findings.
    [[nodiscard]] std::vector<SectionFinding> validate_sections(const platform::BlueprintVersion& v);
    /// @brief The latest alignment evidence document for exactly the pinned artefacts, if any.
    [[nodiscard]] std::optional<json::Json> alignment_for(const platform::BlueprintVersion& v);
    /// @brief Evidence context string of a version ("blueprint:<id>@<v>").
    [[nodiscard]] static std::string context(const platform::BlueprintVersion& v);
    /// @brief Pseudo evidence input binding a document hash (scenario tests depend on the document).
    [[nodiscard]] static platform::EvidenceInput document_input(const platform::BlueprintVersion& v);
    /// @brief Template directory of @p template_id (NotFound otherwise).
    [[nodiscard]] Result<std::filesystem::path> template_dir(std::string_view template_id) const;
};

/// @brief Whether a Blueprint document has a twin model (model-driven Blueprint).
[[nodiscard]] bool is_model_driven(const json::Json& document);
/// @brief Whether a section is derived from the model in a model-driven Blueprint.
[[nodiscard]] bool is_derived_section(std::string_view section);
/// @brief Compile the model of @p v and write the derived pins and sections into @p v (not stored).
[[nodiscard]] Result<json::Json> apply_model(BlueprintService::Impl& impl, Services& services, platform::BlueprintVersion& v, const Actor& actor);

/// @brief API form of section findings.
[[nodiscard]] json::Json to_json(const std::vector<SectionFinding>& findings);

/// @brief A blank twin-blueprint/1 document.
[[nodiscard]] json::Json blank_document(std::string_view id, std::string_view name, std::string_view domain);

/// @brief Replace top-level {"$file": "name.json"} members with the named JSON files next to the document.
[[nodiscard]] Result<json::Json> resolve_includes(json::Json document, const std::filesystem::path& base);

/// @brief Read a whole text file.
[[nodiscard]] Result<std::string> read_text_file(const std::filesystem::path& path);

/// @brief Artefact kind of a formal role.
[[nodiscard]] platform::ArtifactKind kind_of_role(std::string_view role);

/// @brief "pt" -> "pt_model", "dt" -> "dt_model" (InvalidArgument otherwise).
[[nodiscard]] Result<std::string> model_role(std::string_view short_role);

/// @brief Key of telemetry @p telemetry_id in the runtime's telemetry stream: the plant field the
/// @p binding selects ("$." stripped from a path), else the id itself. The instance's channel
/// listens to exactly this key (runtime:<instance>/telemetry#<key>).
[[nodiscard]] std::string channel_key(const json::Json& binding, const std::string& telemetry_id);
/// @brief Whether a plant connection enables at least one input (REST, MQTT endpoint, broker).
[[nodiscard]] bool connection_active(const json::Json& connection);
/// @brief @p connectivity without its simulator sources (an instance fed by the plant).
[[nodiscard]] json::Json without_simulator_sources(const json::Json& connectivity);
/// @brief @p connectivity with the plant's own names: {telemetry: {id: "TT101_degC" | {field | topic}},
/// events: {id: ["START", ...]}} (codes are values of @p event_field). Existing bindings are
/// updated in place (units kept); an empty name returns a signal to the twin's own name.
[[nodiscard]] Result<json::Json> apply_plant_names(const json::Json& connectivity, const json::Json& names, const std::string& event_field);

}  // namespace twin::studio
