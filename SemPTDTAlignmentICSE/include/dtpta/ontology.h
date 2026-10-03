#ifndef DTPTA_ONTOLOGY_H
#define DTPTA_ONTOLOGY_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstddef>
#include "z3++.h"

namespace dtpta {

/**
 * Declaration of an uninterpreted sort (Definition 5: S component of K = (S, F, R, Δ)).
 */
struct SortDecl {
    std::string name;  ///< Sort name, e.g. "Energy", "Hectare"
};

/**
 * Declaration of a function symbol (Definition 5: F component of K = (S, F, R, Δ)).
 */
struct FuncDecl {
    std::string              name;         ///< Function symbol name
    std::vector<std::string> arg_sorts;    ///< Argument sort names (may be empty for constants)
    std::string              return_sort;  ///< Return sort name ("Int", "Real", or declared sort)
};

/**
 * Declaration of a relation symbol (Definition 5: R component of K = (S, F, R, Δ)).
 */
struct RelDecl {
    std::string              name;       ///< Relation symbol name
    std::vector<std::string> arg_sorts;  ///< Argument sort names
};

/**
 * @class Ontology
 * @brief Represents an ontology K = (S, F, R, Δ) as a first-order theory (Definition 5).
 *
 * Owns a Z3 context and a solver pre-loaded with the axioms Δ.
 * All Z3 expressions passed to this class must come from the context
 * returned by get_context().
 *
 * Thread safety: not thread-safe; use one instance per thread.
 */
class Ontology {
public:
    /** Construct an empty ontology (no sorts, functions, relations, or axioms). */
    Ontology();

    /** Destructor. */
    ~Ontology();

    // Non-copyable, non-movable (owns z3::context).
    Ontology(const Ontology&)            = delete;
    Ontology& operator=(const Ontology&) = delete;
    Ontology(Ontology&&)                 = delete;
    Ontology& operator=(Ontology&&)      = delete;

    // ------------------------------------------------------------------ //
    //  Context access
    // ------------------------------------------------------------------ //

    /** @return Mutable reference to the Z3 context owned by this ontology. */
    z3::context& get_context();

    /** @return Const reference to the Z3 context owned by this ontology. */
    const z3::context& get_context() const;

    // ------------------------------------------------------------------ //
    //  Schema builders
    // ------------------------------------------------------------------ //

    /**
     * @brief Register an uninterpreted sort.
     * @param name Sort name (must be unique).
     */
    void add_sort(const std::string& name);

    /**
     * @brief Declare a function symbol.
     * @param name        Function name.
     * @param arg_sorts   Argument sort names (empty for a nullary constant).
     * @param return_sort Return sort name.  Use "Int" or "Real" for built-in sorts,
     *                    or a previously registered sort name.
     */
    void add_function(const std::string& name,
                      const std::vector<std::string>& arg_sorts,
                      const std::string& return_sort);

    /**
     * @brief Declare a relation symbol (maps to a function returning Bool).
     * @param name      Relation name.
     * @param arg_sorts Argument sort names.
     */
    void add_relation(const std::string& name,
                      const std::vector<std::string>& arg_sorts);

    /**
     * @brief Add an axiom to Δ.
     * @param axiom A Boolean Z3 expression created from get_context().
     */
    void add_axiom(const z3::expr& axiom);

    // ------------------------------------------------------------------ //
    //  Entailment checks
    // ------------------------------------------------------------------ //

    /**
     * @brief Check if the axiom set Δ entails φ.
     *
     * Equivalent to checking that Δ ∧ ¬φ is UNSATISFIABLE.
     *
     * @param phi Boolean Z3 expression from get_context().
     * @return true iff Δ ⊨ φ.
     */
    bool entails(const z3::expr& phi);

    /**
     * @brief Check if Δ entails lhs ↔ rhs.
     *
     * Used to decide label equivalence (Algorithm 1, Step 1) and state
     * consistency (Algorithm 1, Step 2).
     *
     * @param lhs Left-hand Boolean expression from get_context().
     * @param rhs Right-hand Boolean expression from get_context().
     * @return true iff Δ ⊨ lhs ↔ rhs.
     */
    bool entails_iff(const z3::expr& lhs, const z3::expr& rhs);

    // ------------------------------------------------------------------ //
    //  Schema accessors
    // ------------------------------------------------------------------ //

    /** @return Registered sort declarations. */
    const std::vector<SortDecl>& get_sorts()     const { return sorts_;    }
    /** @return Registered function declarations. */
    const std::vector<FuncDecl>& get_functions() const { return functions_; }
    /** @return Registered relation declarations. */
    const std::vector<RelDecl>&  get_relations()  const { return relations_; }

    /**
     * @brief Retrieve a previously registered Z3 sort by name.
     * @throws std::out_of_range if the sort was not registered.
     */
    z3::sort get_sort(const std::string& name) const;

    /**
     * @brief Retrieve a previously declared Z3 function decl by name.
     * @throws std::out_of_range if the function was not declared.
     */
    z3::func_decl get_func_decl(const std::string& name) const;

    /**
     * @brief Retrieve a previously declared Z3 function/relation decl by name.
     *
     * Searches both function and relation tables.
     * @throws std::out_of_range if not found.
     */
    z3::func_decl get_any_decl(const std::string& name) const;

    // ------------------------------------------------------------------ //
    //  Diagnostics
    // ------------------------------------------------------------------ //

    /** @return Total number of SMT solver invocations since construction. */
    size_t get_smt_call_count() const { return smt_calls_; }

    /** Reset the SMT call counter. */
    void reset_smt_call_count() { smt_calls_ = 0; }

    /**
     * @brief Return true when the ontology Δ is trivially empty
     *        (no axioms: every entailment reduces to pure validity).
     */
    bool is_empty() const { return axioms_.empty(); }

private:
    std::unique_ptr<z3::context> ctx_;     ///< Owned Z3 context
    std::unique_ptr<z3::solver>  solver_;  ///< Solver pre-loaded with Δ

    std::vector<SortDecl> sorts_;
    std::vector<FuncDecl> functions_;
    std::vector<RelDecl>  relations_;
    std::vector<z3::expr> axioms_;  ///< Copies kept for diagnostics

    // Z3 objects corresponding to registered declarations
    std::vector<std::pair<std::string, z3::sort>>      sort_map_;
    std::vector<std::pair<std::string, z3::func_decl>> func_decl_map_;

    size_t smt_calls_ = 0;

    // Internal helpers
    z3::sort   resolve_sort(const std::string& sort_name) const;
};

} // namespace dtpta

#endif // DTPTA_ONTOLOGY_H
