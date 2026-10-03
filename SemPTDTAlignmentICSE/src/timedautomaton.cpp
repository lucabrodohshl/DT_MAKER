#include "dtpta/timedautomaton.h"

#include "dtpta/context.h"
#include "utap/utap.hpp"
#include <iostream>
#include <cstdlib> // strtol
#include <sstream>
#include <algorithm>
#include <fstream>
#include <regex>
#include <mutex>
#include <numeric>

namespace dtpta {

// ─── Copy / Move constructors ─────────────────────────────────────────────

TimedAutomaton::TimedAutomaton(const TimedAutomaton& other)
    : name_(other.name_),
      dimension_(other.dimension_),
      locations_(other.locations_),
      transitions_(other.transitions_),
      outgoing_transitions_(other.outgoing_transitions_),
      location_map_(other.location_map_),
      channels_(other.channels_),
      sender_transitions_(other.sender_transitions_),
      receiver_transitions_(other.receiver_transitions_),
      // Zone-graph fields (states_, state_map_, zone_transitions_,
      // waiting_list_) are intentionally NOT copied; they will be rebuilt
      // lazily.  state_mutex_ is default-constructed (new, unlocked mutex).
      constructed_(false),
      timing_constants_(other.timing_constants_),
      clock_max_bounds_(other.clock_max_bounds_),
      clock_min_lower_bounds_(other.clock_min_lower_bounds_),
      context_(other.context_)
{}

TimedAutomaton& TimedAutomaton::operator=(const TimedAutomaton& other) {
    if (this == &other) return *this;
    name_                   = other.name_;
    dimension_              = other.dimension_;
    locations_              = other.locations_;
    transitions_            = other.transitions_;
    outgoing_transitions_   = other.outgoing_transitions_;
    location_map_           = other.location_map_;
    channels_               = other.channels_;
    sender_transitions_     = other.sender_transitions_;
    receiver_transitions_   = other.receiver_transitions_;
    // Zone-graph fields reset; state_mutex_ stays (it's non-assignable).
    states_.clear();
    state_map_.clear();
    zone_transitions_.clear();
    while (!waiting_list_.empty()) waiting_list_.pop();
    constructed_            = false;
    timing_constants_       = other.timing_constants_;
    clock_max_bounds_       = other.clock_max_bounds_;
    clock_min_lower_bounds_ = other.clock_min_lower_bounds_;
    context_                = other.context_;
    return *this;
}

TimedAutomaton::TimedAutomaton(TimedAutomaton&& other) noexcept
    : name_(std::move(other.name_)),
      dimension_(other.dimension_),
      locations_(std::move(other.locations_)),
      transitions_(std::move(other.transitions_)),
      outgoing_transitions_(std::move(other.outgoing_transitions_)),
      location_map_(std::move(other.location_map_)),
      channels_(std::move(other.channels_)),
      sender_transitions_(std::move(other.sender_transitions_)),
      receiver_transitions_(std::move(other.receiver_transitions_)),
      // Zone-graph state cannot be moved due to shared_mutex; reset instead.
      constructed_(false),
      timing_constants_(std::move(other.timing_constants_)),
      clock_max_bounds_(std::move(other.clock_max_bounds_)),
      clock_min_lower_bounds_(std::move(other.clock_min_lower_bounds_)),
      context_(std::move(other.context_))
{
    other.dimension_ = 0;
    other.constructed_ = false;
}

TimedAutomaton& TimedAutomaton::operator=(TimedAutomaton&& other) noexcept {
    if (this == &other) return *this;
    name_                   = std::move(other.name_);
    dimension_              = other.dimension_;
    locations_              = std::move(other.locations_);
    transitions_            = std::move(other.transitions_);
    outgoing_transitions_   = std::move(other.outgoing_transitions_);
    location_map_           = std::move(other.location_map_);
    channels_               = std::move(other.channels_);
    sender_transitions_     = std::move(other.sender_transitions_);
    receiver_transitions_   = std::move(other.receiver_transitions_);
    states_.clear();
    state_map_.clear();
    zone_transitions_.clear();
    while (!waiting_list_.empty()) waiting_list_.pop();
    constructed_            = false;
    timing_constants_       = std::move(other.timing_constants_);
    clock_max_bounds_       = std::move(other.clock_max_bounds_);
    clock_min_lower_bounds_ = std::move(other.clock_min_lower_bounds_);
    context_                = std::move(other.context_);
    other.dimension_ = 0;
    other.constructed_ = false;
    return *this;
}

// ─── Original constructors ────────────────────────────────────────────────

TimedAutomaton::TimedAutomaton(const char* xml_content):constructed_(false) {
    DEV_PRINT("TimedAutomaton: Parsing XML content..." << std::endl);
    
    // Create a UTAP document and parse the XML
    UTAP::Document doc;
    int32_t result = parse_XML_buffer(xml_content, doc, true);
    
    if (result != 0) {
        throw std::runtime_error("Failed to parse XML with error code: " + std::to_string(result));
    }
    
    DEV_PRINT("   XML parsed successfully!" << std::endl);
    DEV_PRINT("   Document contains " << doc.get_templates().size() << " template(s)" << std::endl);
    
    if (doc.get_templates().empty()) {
        throw std::runtime_error("No templates found in document!");
    }
    
    // Build the automaton from the UTAP document
    build_from_utap_document(doc);
    
    DEV_PRINT("   TimedAutomaton construction complete!" << std::endl);
}

TimedAutomaton::TimedAutomaton(const std::string& fileName):constructed_(false)
{
    // Create a UTAP document and parse the XML
    UTAP::Document doc;
    int res = parse_XML_file(fileName, doc, true);
    if (res != 0) {
        throw std::runtime_error("Failed to parse XML file: " + fileName);
    }

    // Build the automaton from the UTAP document
    build_from_utap_document(doc);
}

///TO DO DEAL WITH GLOBAL VARIABLES

void TimedAutomaton::build_from_utap_document(UTAP::Document& doc) {
    DEV_PRINT("   Converting UTAP document to TimedAutomaton..." << std::endl);
    
    // Debug: Check document-level information
    DEV_PRINT("   Document has " << doc.get_templates().size() << " templates" << std::endl);
    
    // Get the first template
    auto& templates = doc.get_templates();
    auto& template_ref = templates.front();


    // Parse global declarations first
    auto& global_declarations = doc.get_globals();
    context_.parse_global_declarations(global_declarations);

    // Build the automaton using the global context
    build_from_template(template_ref, 0); // Pass 0 as placeholder, dimension will be calculated inside
}





void TimedAutomaton::build_from_template(const UTAP::Template& template_ref, int initial_dimensions) {
    DEV_PRINT("Building automaton from template: " << template_ref.uid.get_name() << std::endl);
    

    name_ = template_ref.uid.get_name();
    // Phase 1: Parse template declarations and functions
    parse_template_declarations(template_ref);
    
    // Phase 2: Parse template parameters
    parse_template_parameters(template_ref);
    
    // Phase 3: Set automaton dimension
    finalize_dimension();
    
    // Phase 4: Build automaton structure
    build_locations(template_ref);
    build_transitions(template_ref);
}

void TimedAutomaton::parse_template_declarations(const UTAP::Template& template_ref) {
    // Parse template variables
    for (const auto& variable : template_ref.variables) {
        context_.parse_declaration(variable);
    }
    
    // Parse template functions
    DEV_PRINT("   Template has " << template_ref.functions.size() << " functions:" << std::endl);
    for (const auto& function : template_ref.functions) {
        context_.parse_function(function);
    }
}

void TimedAutomaton::parse_template_parameters(const UTAP::Template& template_ref) {
    DEV_PRINT("   Template has " << template_ref.unbound << " unbound parameters" << std::endl);
    
    if (template_ref.parameters.get_size() == 0) {
        return;
    }
    
    DEV_PRINT("   Template has " << template_ref.parameters.get_size() << " parameters:" << std::endl);
    
    for (size_t i = 0; i < template_ref.parameters.get_size(); ++i) {
        const auto& param = template_ref.parameters[i];
        std::string param_name = param.get_name();
        
        DEV_PRINT("   Template Parameter: " << param_name 
                  << ", type: " << param.get_type().str() << std::endl);

        if (param.get_type().is_clock()) {
            context_.clocks_[param_name] = context_.next_clock_index_++;
            DEV_PRINT("   Found template parameter clock: " << param_name << std::endl);
        } else if (param.get_type().is_constant()) {
            context_.constants_[param_name] = 0.0; // Default value
            DEV_PRINT("   Found template parameter constant: " << param_name << " (value to be determined)" << std::endl);
        } else {
            context_.variables_[param_name] = 0.0; // Initialize to 0
            DEV_PRINT("   Found template parameter (treated as variable): " << param_name << std::endl);
        }
    }
}

void TimedAutomaton::finalize_dimension() {
    // Calculate final dimension: reference clock (0) + all discovered clocks
    dimension_ = context_.next_clock_index_;
    
    DEV_PRINT("   Total clocks found: " << (context_.next_clock_index_ - 1) << std::endl);
    DEV_PRINT("   Setting dimension to: " << dimension_ << std::endl);

    clock_max_bounds_.assign(dimension_, 0); // initialize per-clock maxima (0 for ref clock)
    clock_min_lower_bounds_.assign(dimension_, 0); // initialize per-clock minima (0 default)
}

void TimedAutomaton::build_locations(const UTAP::Template& template_ref) {
    location_map_.clear();
    
    for (size_t i = 0; i < template_ref.locations.size(); ++i) {
        const auto& location = template_ref.locations[i];
        std::string loc_id = location.uid.get_name();
        int loc_int_id = static_cast<int>(i);
        
        location_map_[loc_id] = loc_int_id;
        add_location(loc_int_id, loc_id);
        
        DEV_PRINT("   Added location: " << loc_id << " (ID: " << loc_int_id << ")" << std::endl);
        
        // Parse location invariants
        parse_location_invariant(location, loc_int_id);
    }
}

void TimedAutomaton::parse_location_invariant(const UTAP::Location& location, int loc_int_id) {
    if (location.invariant.empty()) {
        DEV_PRINT("     No invariant on this location" << std::endl);
        return;
    }
    
    std::string invariant_str = location.invariant.str();
    DEV_PRINT("     Invariant: " << invariant_str << std::endl);

    // Helper to add clock-difference constraint using DBM semantics
    auto add_diff_constraint = [&](const std::string& left, const std::string& right, const std::string& op, int val) {
        if (context_.clocks_.find(left) == context_.clocks_.end() || context_.clocks_.find(right) == context_.clocks_.end()) {
            DEV_PRINT("     Unknown clock in difference: " << left << " or " << right << std::endl);
            return;
        }
        cindex_t i = context_.clocks_[left];
        cindex_t j = context_.clocks_[right];
        timing_constants_.insert(val);
        if (op == "<=") {
            add_invariant(loc_int_id, i, j, val, dbm_WEAK);
        } else if (op == "<") {
            add_invariant(loc_int_id, i, j, val, dbm_STRICT);
        } else if (op == ">=") {
            // i - j >= c  <=>  j - i <= -c
            add_invariant(loc_int_id, j, i, -val, dbm_WEAK);
        } else if (op == ">") {
            // i - j > c  <=>  j - i < -c
            add_invariant(loc_int_id, j, i, -val, dbm_STRICT);
        } else if (op == "==") {
            add_invariant(loc_int_id, i, j, val, dbm_WEAK);
            add_invariant(loc_int_id, j, i, -val, dbm_WEAK);
        }
        DEV_PRINT("     Added invariant difference: " << left << " - " << right << " " << op << " " << val << std::endl);
    };

    auto get_op_str = [&](int kind) -> std::string {
        switch (kind) {
            case UTAP::Constants::GE: return ">="; 
            case UTAP::Constants::GT: return ">";  
            case UTAP::Constants::LE: return "<="; 
            case UTAP::Constants::LT: return "<";  
            case UTAP::Constants::EQ: return "=="; 
            default: return "";
        }
    };

    auto flip_op = [&](const std::string& op) -> std::string {
        if (op == ">=") return "<=";
        if (op == ">")  return "<";
        if (op == "<=") return ">=";
        if (op == "<")  return ">";
        return op; // == stays the same
    };

    std::function<bool(const UTAP::Expression&)> process = [&](const UTAP::Expression& expr) -> bool {
        if (expr.empty()) return false;
        int kind = expr.get_kind();

        // Conjunctions/Disjunctions: process all parts
        if (kind == UTAP::Constants::AND || kind == UTAP::Constants::OR) {
            bool any = false;
            for (size_t i = 0; i < expr.get_size(); ++i) any |= process(expr[i]);
            return any;
        }
        // Some UTAPs represent sequences/comma with other kinds
        if (kind == 12 || kind == 11 || kind == 13 || kind == 14 || kind == 15) {
            bool any = false;
            for (size_t i = 0; i < expr.get_size(); ++i) any |= process(expr[i]);
            return any;
        }

        // Comparisons
        if (expr.get_size() == 2) {
            std::string op = get_op_str(kind);
            if (op.empty()) return false;

            const auto& left = expr[0];
            const auto& right = expr[1];

            // Helper: parse simple clock vs const
            auto try_simple = [&](const UTAP::Expression& clkExpr, const UTAP::Expression& valExpr, const std::string& opStr, bool flipped) -> bool {
                if (clkExpr.get_kind() == UTAP::Constants::IDENTIFIER) {
                    std::string name = clkExpr.get_symbol().get_name();
                    if (context_.clocks_.find(name) != context_.clocks_.end()) {
                        int v;
                        if (evaluate_expression(valExpr, v)) {
                            std::string final_op = flipped ? flip_op(opStr) : opStr;
                            if (flipped) {
                                DEV_PRINT("     Found flipped clock constraint: " << name << " " << final_op << " " << v << std::endl);
                            } else {
                                DEV_PRINT("     Found clock constraint: " << name << " " << final_op << " " << v << std::endl);
                            }
                            add_dbm_constraint(name, final_op, v, context_.clocks_, loc_int_id, -1);
                            DEV_PRINT("     Added invariant constraint to location" << std::endl);
                            return true;
                        }
                    }
                }
                return false;
            };

            // Helper: parse difference (x - y) vs const
            auto try_diff = [&](const UTAP::Expression& diffExpr, const UTAP::Expression& valExpr, const std::string& opStr, bool flipped) -> bool {
                if (diffExpr.get_kind() == UTAP::Constants::MINUS && diffExpr.get_size() == 2) {
                    const auto& a = diffExpr[0];
                    const auto& b = diffExpr[1];
                    if (a.get_kind() == UTAP::Constants::IDENTIFIER && b.get_kind() == UTAP::Constants::IDENTIFIER) {
                        std::string an = a.get_symbol().get_name();
                        std::string bn = b.get_symbol().get_name();
                        int v;
                        if (evaluate_expression(valExpr, v)) {
                            std::string final_op = flipped ? flip_op(opStr) : opStr;
                            DEV_PRINT("     Found clock-difference constraint: " << an << " - " << bn << " " << final_op << " " << v << std::endl);
                            add_diff_constraint(an, bn, final_op, v);
                            return true;
                        }
                    }
                }
                return false;
            };

            // Try all patterns
            if (try_simple(left, right, op, /*flipped*/false)) return true;
            if (try_simple(right, left, op, /*flipped*/true)) return true;
            if (try_diff(left, right, op, /*flipped*/false)) return true;
            if (try_diff(right, left, op, /*flipped*/true)) return true;
        }

        return false;
    };

    bool ok = process(location.invariant);
    if (!ok) {
        DEV_PRINT("     Failed to parse invariant via UTAP expression: " << invariant_str << std::endl);
    }
}

void TimedAutomaton::build_transitions(const UTAP::Template& template_ref) {

    //std::cout << "Building transitions..." << template_ref.edges.size() <<std::endl;
    for (size_t i = 0; i < template_ref.edges.size(); ++i) {
        const auto& edge = template_ref.edges[i];
        
        // Get source and destination locations
        std::string source_id, target_id;
        if (!get_edge_locations(edge, source_id, target_id)) {
            DEV_PRINT("   Skipping edge with missing source or destination" << std::endl);
            continue;
        }
        
        int source_int = location_map_[source_id];
        int target_int = location_map_[target_id];
        
    // Parse synchronization to get channel name and direction.
    // NOTE: parse_edge_synchronization calls add_synchronization(i, ...) which
    // uses i as the transition index — but the transition hasn't been added yet,
    // so that call silently does nothing (bounds check in add_synchronization).
    // We therefore extract channel/sender info here and re-apply after add_transition.
    std::string channel;
    bool is_sender = false;
    bool has_sync = !edge.sync.empty() && parse_synchronization_from_expr(edge.sync, channel, is_sender);
    if (has_sync) add_channel(channel);
    std::string action = has_sync ? channel : TA_CONFIG.tau_action_name;
    add_transition(source_int, target_int, action);
    // Use actual transition index (not edge index i, which diverges when edges are skipped).
    size_t t_idx = transitions_.size() - 1;
    // Apply synchronization to the transition that was just appended.
    if (has_sync) {
        add_synchronization(t_idx, channel, is_sender);
    }

        DEV_PRINT("   Added transition: " << source_id << " -> " << target_id
                  << " (" << source_int << " -> " << target_int << ")" << std::endl);

        // Parse guard constraints and clock resets
        parse_edge_assignment(edge, t_idx);
        if (!parse_edge_guard(edge, t_idx)) {
            // If guard parsing fails and constraints are not satisfied, skip this transition
            DEV_PRINT("     Skipping transition due to unsatisfied constraints" << std::endl);
            // Remove the transition we just added
            if (!transitions_.empty()) {
                transitions_.pop_back();
                // Also remove from outgoing_transitions_ map
                auto& outgoing = outgoing_transitions_[source_int];
                if (!outgoing.empty() && outgoing.back() == static_cast<int>(transitions_.size())) {
                    outgoing.pop_back();
                }
            }
            continue;
        }
        
        
    }
}

bool TimedAutomaton::get_edge_locations(const UTAP::Edge& edge, std::string& source_id, std::string& target_id) {
    if (edge.src != nullptr) {
        source_id = edge.src->uid.get_name();
    }
    if (edge.dst != nullptr) {
        target_id = edge.dst->uid.get_name();
    }
    
    return !source_id.empty() && !target_id.empty();
}

std::string TimedAutomaton::parse_edge_assignment(const UTAP::Edge& edge, size_t edge_index) {
    if (edge.assign.empty()) {
        return TA_CONFIG.tau_action_name;
    }

    std::string assign_str = edge.assign.str();
    DEV_PRINT("     Assignment: " << assign_str << std::endl);

    // Simple string-based parsing to support sequences like "x = 0, v = 4" and ":="
    auto trim = [](std::string &s){
        size_t b = s.find_first_not_of(" \t\n\r");
        size_t e = s.find_last_not_of(" \t\n\r");
        if(b==std::string::npos){ s.clear(); return; }
        s = s.substr(b, e-b+1);
    };

    bool any_parsed = false;
    size_t start = 0;
    while(start < assign_str.size()){
        size_t comma = assign_str.find(',', start);
        std::string token = assign_str.substr(start, (comma==std::string::npos? assign_str.size(): comma) - start);
        start = (comma==std::string::npos? assign_str.size(): comma + 1);
        trim(token);
        if(token.empty()) continue;

        // Normalize := to =
        size_t coloneq = token.find(":=");
        if(coloneq != std::string::npos){
            token.replace(coloneq, 2, "=");
        }

        size_t eq = token.find('=');
        if(eq == std::string::npos){
            // Not an assignment piece; ignore
            continue;
        }
        std::string lhs = token.substr(0, eq);
        std::string rhs = token.substr(eq+1);
        trim(lhs); trim(rhs);
        if(lhs.empty() || rhs.empty()) continue;

        // Parse integer value on rhs (best-effort)
        char *endp = nullptr;
        long val = std::strtol(rhs.c_str(), &endp, 10);
        if(endp==rhs.c_str() || *endp!='\0'){
            // Not a pure integer; skip applying
            DEV_PRINT("     Skipping non-integer assignment: " << token << std::endl);
            continue;
        }

        // Apply to clocks or variables if known
        if(context_.clocks_.find(lhs) != context_.clocks_.end()){
            cindex_t clock_idx = context_.clocks_[lhs];
            if(val == 0){
                add_reset(edge_index, clock_idx);
                DEV_PRINT("     Added reset from string assignment: " << lhs << " -> 0" << std::endl);
            } else {
                DEV_PRINT("     Warning: Non-zero clock reset '" << lhs << " = " << val << "' not supported; ignoring" << std::endl);
            }
            any_parsed = true;
        } else if(context_.variables_.find(lhs) != context_.variables_.end()){
            context_.variables_[lhs] = static_cast<int>(val);
            DEV_PRINT("     Parsed variable assignment (string): " << lhs << " := " << val << std::endl);
            any_parsed = true;
        } else if(context_.constants_.find(lhs) != context_.constants_.end()){
            DEV_PRINT("     Warning: Assignment to constant '" << lhs << "' ignored" << std::endl);
            any_parsed = true; // still treat as handled
        } else {
            DEV_PRINT("     Unknown assignment target '" << lhs << "'" << std::endl);
        }
    }

    // Assignments are internal; return tau label to keep them silent
    if(any_parsed) return TA_CONFIG.tau_action_name;

    // Fallback: if nothing parsed, return the raw string
    DEV_PRINT("    NOTHING PARSED '" << assign_str << "'" << std::endl);
    return TA_CONFIG.tau_action_name;;
}

std::string TimedAutomaton::handle_clock_reset(const std::string& clock_name, int reset_value, 
                                             size_t edge_index, const std::string& assign_str) {
    if (context_.clocks_.find(clock_name) != context_.clocks_.end()) {
        cindex_t clock_idx = context_.clocks_[clock_name];
        if (reset_value == 0) {
            add_reset(edge_index, clock_idx);
            DEV_PRINT("     Added reset to transition: " << clock_name << " -> 0" << std::endl);
        } else {
            DEV_PRINT("     Warning: Non-zero reset value " << reset_value << " not supported" << std::endl);
        }
        return TA_CONFIG.tau_action_name;
    }
    
    // Check if it's a variable assignment instead
    //if (context_.variables_.find(clock_name) != context_.variables_.end()) {
    //    context_.variables_[clock_name] = reset_value;
    //    DEV_PRINT("     Parsed clock assignment: " << clock_name << " := " << reset_value << std::endl);
    //    return TA_CONFIG.tau_action_name;
    //}
    
    // Not a known clock or variable, use assignment as action name
    return TA_CONFIG.tau_action_name;
}

std::string TimedAutomaton::handle_variable_assignment(const std::string& var_name, int var_value, 
                                                     const std::string& assign_str) {
    if (context_.variables_.find(var_name) != context_.variables_.end()) {
        context_.variables_[var_name] = var_value;
        DEV_PRINT("     Parsed variable assignment: " << var_name << " := " << var_value << std::endl);
        return assign_str;
    }
    
    // Unknown variable, use tau as action name
    return TA_CONFIG.tau_action_name;
}

bool TimedAutomaton::parse_edge_guard(const UTAP::Edge& edge, size_t edge_index) {
    if (edge.guard.empty()) {
        DEV_PRINT("     No guard on this transition" << std::endl);
        return true;
    }
    
    std::string guard_str = edge.guard.str();
    DEV_PRINT("     Guard: " << guard_str << std::endl);
    DEV_PRINT("     Guard expression kind: " << edge.guard.get_kind() << std::endl);
    DEV_PRINT("     Guard expression size: " << edge.guard.get_size() << std::endl);
    
    // Extract all constraints from the guard expression
    std::vector<Constraint> constraints;
    extract_all_constraints(edge.guard, constraints);
    
    bool has_clock_constraints = false;
    bool variable_constraints_satisfied = true; // Do not prune transitions based on variables at parse time
    
    // Process each constraint
    for (const auto& constraint : constraints) {
        if (constraint.is_clock) {
            if (context_.clocks_.find(constraint.name) != context_.clocks_.end()) {
                add_dbm_constraint(constraint.name, constraint.op, constraint.value, 
                                 context_.clocks_, -1, edge_index);
                has_clock_constraints = true;
                DEV_PRINT("     Added clock constraint: " << constraint.name << " " 
                         << constraint.op << " " << constraint.value << std::endl);
            }
        } else {
            // Skip evaluating variable constraints at parse time; runtime semantics apply them before assignments
            DEV_PRINT("     Skipping variable constraint at parse-time: " << constraint.name << " "
                     << constraint.op << " " << constraint.value << std::endl);
        }
    }
    
    // Fallback parsing if no constraints were extracted
    if (constraints.empty()) {
    // Ensure we never drop transitions due to variables in fallback either
    (void)parse_guard_fallback(edge.guard, edge_index, has_clock_constraints);
    variable_constraints_satisfied = true;
    }
    
    if (has_clock_constraints || !constraints.empty()) {
        DEV_PRINT("     Added guard constraints to transition" << std::endl);
    }
    
    // Always keep the transition; we've added any clock constraints and ignore variables here
    return true;
}

bool TimedAutomaton::parse_guard_fallback(const UTAP::Expression& guard, size_t edge_index, bool& has_clock_constraints) {
    std::string clock_name, var_name, op;
    int value;
    
    // Try clock constraint parsing
    if (parse_clock_constraint_from_expr(guard, clock_name, op, value) 
        && context_.clocks_.find(clock_name) != context_.clocks_.end()) {
        add_dbm_constraint(clock_name, op, value, context_.clocks_, -1, edge_index);
        DEV_PRINT("     Added fallback clock constraint: " << clock_name << " " << op << " " << value << std::endl);
        has_clock_constraints = true;
        return true;
    }
    
    // Try variable constraint parsing
    if (parse_variable_constraint_from_expr(guard, var_name, op, value)) {
        DEV_PRINT("     Fallback: skipping variable constraint at parse-time: " << var_name << " "
                 << op << " " << value << std::endl);
        return true; // don't prune
    }
    
    DEV_PRINT("     Failed to parse guard constraint: " << guard.str() << std::endl);
    return true; // If we can't parse it, assume it's satisfied
}

std::string TimedAutomaton::parse_edge_synchronization(const UTAP::Edge& edge, size_t edge_index) {
    if (edge.sync.empty()) {
        DEV_PRINT("     No synchronization on this transition" << std::endl);
        return TA_CONFIG.tau_action_name;
    }
    
    std::string sync_str = edge.sync.str();
    DEV_PRINT("     Synchronization: " << sync_str << std::endl);
    
    std::string channel;
    bool is_sender;
    
    if (parse_synchronization_from_expr(edge.sync, channel, is_sender)) {
        DEV_PRINT("     Parsed synchronization: " << channel << (is_sender ? "!" : "?") << std::endl);
        add_synchronization(edge_index, channel, is_sender);
        add_channel(channel);
        DEV_PRINT("     Added synchronization to transition" << std::endl);
    } else {
        DEV_PRINT("     Failed to parse synchronization: " << sync_str << std::endl);
    }
    return channel;
}

bool TimedAutomaton::evaluate_variable_constraint(const std::string& var_name, const std::string& op, int value) {
    // Check if variable exists in our context
    auto var_it = context_.variables_.find(var_name);
    if (var_it == context_.variables_.end()) {
        // Check constants
        auto const_it = context_.constants_.find(var_name);
        if (const_it == context_.constants_.end()) {
            DEV_PRINT("     Variable/constant " << var_name << " not found, assuming constraint is satisfied" << std::endl);
            return true; // If we don't know the variable, assume it's satisfied
        }
        
        // Evaluate constraint with constant value
        double const_value = const_it->second;
        return evaluate_comparison(const_value, op, value);
    }
    
    // Evaluate constraint with variable value
    double var_value = var_it->second;
    return evaluate_comparison(var_value, op, value);
}

bool TimedAutomaton::evaluate_comparison(double lhs, const std::string& op, int rhs) {
    if (op == "<=") return lhs <= rhs;
    if (op == "<") return lhs < rhs;
    if (op == ">=") return lhs >= rhs;
    if (op == ">") return lhs > rhs;
    if (op == "==") return lhs == rhs;
    if (op == "!=") return lhs != rhs;
    
    DEV_PRINT("     Unknown operator: " << op << std::endl);
    return true; // If we don't understand the operator, assume it's satisfied
}

bool TimedAutomaton::evaluate_expression(const UTAP::Expression& expr, int& result) {
    if (expr.empty()) {
        return false;
    }
    
    auto kind = expr.get_kind();
    
    switch (kind) {
        case UTAP::Constants::CONSTANT:
            result = expr.get_value();
            return true;
            
        case UTAP::Constants::IDENTIFIER: {
            // For variables, we need to get their value from the symbol
            auto symbol = expr.get_symbol();
            if (symbol.get_type().is_integer()) {
                // Check if it's a known constant
                if (context_.constants_.count(symbol.get_name())) {
                    result = context_.constants_.at(symbol.get_name());
                    return true;
                }
                // Check if it's a known variable
                if (context_.variables_.count(symbol.get_name())) {
                    result = context_.variables_.at(symbol.get_name());
                    return true;
                }
                // For integer variables with initializers
                auto* var_data = static_cast<const UTAP::Variable*>(symbol.get_data());
                if (var_data && !var_data->init.empty()) {
                    return evaluate_expression(var_data->init, result);
                }
            }
            return false;
        }
        
        case UTAP::Constants::PLUS:
            if (expr.get_size() == 2) {
                int left, right;
                if (evaluate_expression(expr[0], left) && evaluate_expression(expr[1], right)) {
                    result = left + right;
                    return true;
                }
            }
            return false;
            
        case UTAP::Constants::MINUS:
            if (expr.get_size() == 2) {
                int left, right;
                if (evaluate_expression(expr[0], left) && evaluate_expression(expr[1], right)) {
                    result = left - right;
                    return true;
                }
            }
            return false;
            
        case UTAP::Constants::MULT:
            if (expr.get_size() == 2) {
                int left, right;
                if (evaluate_expression(expr[0], left) && evaluate_expression(expr[1], right)) {
                    result = left * right;
                    return true;
                }
            }
            return false;
            
        default:
            return false;
    }
}

bool TimedAutomaton::parse_clock_constraint_from_expr(const UTAP::Expression& expr, std::string& clock_name, 
                                                     std::string& op, int& value) {
    if (expr.empty()) {
        return false;
    }
    
    auto kind = expr.get_kind();
    
    // Handle logical operators recursively (AND, OR)
    if (kind == UTAP::Constants::AND || kind == UTAP::Constants::OR) {
        // Try to find any clock constraint in the expression tree
        for (size_t i = 0; i < expr.get_size(); ++i) {
            if (parse_clock_constraint_from_expr(expr[i], clock_name, op, value)) {
                return true; // Found a clock constraint
            }
        }
        return false; // No clock constraints found
    }
    
    // Handle potential comma operator
    if (kind == 12 || kind == 11 || kind == 13 || kind == 14 || kind == 15) {
        DEV_PRINT("     Found potential comma/sequence in clock constraint parsing (kind " << kind << ")" << std::endl);
        for (size_t i = 0; i < expr.get_size(); ++i) {
            if (parse_clock_constraint_from_expr(expr[i], clock_name, op, value)) {
                return true;
            }
        }
        return false;
    }
    
    // Handle NOT expressions
    if (kind == UTAP::Constants::NOT && expr.get_size() == 1) {
        // For NOT expressions, try to parse the inner expression
        // Note: This might need special handling depending on the logic
        return parse_clock_constraint_from_expr(expr[0], clock_name, op, value);
    }
    
    // Handle comparison operators
    if (expr.get_size() == 2) {
        // Map UTAP comparison operators to strings
        switch (kind) {
            case UTAP::Constants::GE:
                op = ">=";
                break;
            case UTAP::Constants::GT:
                op = ">";
                break;
            case UTAP::Constants::LE:
                op = "<=";
                break;
            case UTAP::Constants::LT:
                op = "<";
                break;
            case UTAP::Constants::EQ:
                op = "==";
                break;
            default:
                return false; // Not a comparison operator
        }
        
        // Get left side - try both orders in case of different expression structures
        const auto& left = expr[0];
        const auto& right = expr[1];
        
        // Case 1: left side is a clock identifier
        if (left.get_kind() == UTAP::Constants::IDENTIFIER) {
            std::string identifier_name = left.get_symbol().get_name();
            
            // Check if this identifier is a clock (we need to check both global and template clocks)
            if (context_.clocks_.find(identifier_name) != context_.clocks_.end()) {
                clock_name = identifier_name;
                
                // Evaluate right side
                if (evaluate_expression(right, value)) {
                    DEV_PRINT("     Found clock constraint: " << clock_name << " " << op << " " << value << std::endl);
                    return true;
                }
            }
        }
        
        // Case 2: right side is a clock identifier (for expressions like "5 < x")
        if (right.get_kind() == UTAP::Constants::IDENTIFIER) {
            std::string identifier_name = right.get_symbol().get_name();
            
            // Check if this identifier is a clock
            if (context_.clocks_.find(identifier_name) != context_.clocks_.end()) {
                clock_name = identifier_name;
                
                // Evaluate left side and flip the operator
                int left_value;
                if (evaluate_expression(left, left_value)) {
                    value = left_value;
                    
                    // Flip the operator for expressions like "5 < x" -> "x > 5"
                    if (op == ">=") op = "<=";
                    else if (op == ">") op = "<";
                    else if (op == "<=") op = ">=";
                    else if (op == "<") op = ">";
                    // == stays the same
                    
                    DEV_PRINT("     Found flipped clock constraint: " << clock_name << " " << op << " " << value << std::endl);
                    return true;
                }
            }
        }
    }
    
    return false;
}

bool TimedAutomaton::parse_clock_reset_from_expr(const UTAP::Expression& expr, std::string& clock_name, int& value) {
    if (expr.empty() || expr.get_size() != 2) {
        return false;
    }
    
    auto kind = expr.get_kind();
    
    // Check if this is an assignment
    if (kind != UTAP::Constants::ASSIGN) {
        return false;
    }
    
    // Get left side (should be a clock identifier)
    const auto& left = expr[0];
    if (left.get_kind() != UTAP::Constants::IDENTIFIER) {
        return false;
    }
    
    clock_name = left.get_symbol().get_name();
    // check if this is a clock
    if (context_.clocks_.find(clock_name) == context_.clocks_.end()) {
        return false; // Not a known clock
    }
    
    // Get right side (should evaluate to an integer)
    const auto& right = expr[1];
    if (!evaluate_expression(right, value)) {
        return false;
    }
    
    return true;
}

bool TimedAutomaton::parse_variable_assignment_from_expr(const UTAP::Expression& expr, std::string& var_name, int& value) {
    if (expr.empty() || expr.get_size() != 2) {
        return false;
    }
    
    auto kind = expr.get_kind();
    
    // Check if this is an assignment
    if (kind != UTAP::Constants::ASSIGN) {
        return false;
    }
    
    // Get left side (should be a variable identifier)
    const auto& left = expr[0];
    if (left.get_kind() != UTAP::Constants::IDENTIFIER) {
        return false;
    }
    
    var_name = left.get_symbol().get_name();
    
    // Get right side (should evaluate to an integer)
    const auto& right = expr[1];
    if (!evaluate_expression(right, value)) {
        return false;
    }
    
    return true;
}

bool TimedAutomaton::parse_variable_constraint_from_expr(const UTAP::Expression& expr, std::string& var_name, 
                                                        std::string& op, int& value) {
    if (expr.empty()) {
        return false;
    }
    
    auto kind = expr.get_kind();
    
    // Handle logical operators recursively (AND, OR)
    if (kind == UTAP::Constants::AND || kind == UTAP::Constants::OR) {
        // Try to find any variable constraint in the expression tree
        for (size_t i = 0; i < expr.get_size(); ++i) {
            if (parse_variable_constraint_from_expr(expr[i], var_name, op, value)) {
                return true; // Found a variable constraint
            }
        }
        return false; // No variable constraints found
    }
    
    // Handle potential comma operator
    if (kind == 12 || kind == 11 || kind == 13 || kind == 14 || kind == 15) {
        DEV_PRINT("     Found potential comma/sequence in variable constraint parsing (kind " << kind << ")" << std::endl);
        for (size_t i = 0; i < expr.get_size(); ++i) {
            if (parse_variable_constraint_from_expr(expr[i], var_name, op, value)) {
                return true;
            }
        }
        return false;
    }
    
    // Handle NOT expressions
    if (kind == UTAP::Constants::NOT && expr.get_size() == 1) {
        // For NOT expressions, try to parse the inner expression
        // Note: This might need special handling depending on the logic
        return parse_variable_constraint_from_expr(expr[0], var_name, op, value);
    }
    
    // Handle comparison operators
    if (expr.get_size() == 2) {
        // Map UTAP comparison operators to strings
        switch (kind) {
            case UTAP::Constants::GE:
                op = ">=";
                break;
            case UTAP::Constants::GT:
                op = ">";
                break;
            case UTAP::Constants::LE:
                op = "<=";
                break;
            case UTAP::Constants::LT:
                op = "<";
                break;
            case UTAP::Constants::EQ:
                op = "==";
                break;
            case UTAP::Constants::NEQ:
                op = "!=";
                break;
            default:
                return false; // Not a comparison operator
        }
        
        // Get left and right sides
        const auto& left = expr[0];
        const auto& right = expr[1];
        
        // Case 1: left side is a variable identifier
        if (left.get_kind() == UTAP::Constants::IDENTIFIER) {
            std::string identifier_name = left.get_symbol().get_name();
            
            // Check if this identifier is a variable or constant (not a clock)
            if ((context_.variables_.find(identifier_name) != context_.variables_.end() || 
                 context_.constants_.find(identifier_name) != context_.constants_.end()) &&
                context_.clocks_.find(identifier_name) == context_.clocks_.end()) {

                var_name = identifier_name;
                
                // Evaluate right side
                if (evaluate_expression(right, value)) {
                    DEV_PRINT("     Found variable constraint: " << var_name << " " << op << " " << value << std::endl);
                    return true;
                }
            }
        }
        
        // Case 2: right side is a variable identifier (for expressions like "5 == id")
        if (right.get_kind() == UTAP::Constants::IDENTIFIER) {
            std::string identifier_name = right.get_symbol().get_name();
            
            // Check if this identifier is a variable or constant (not a clock)
            if ((context_.variables_.find(identifier_name) != context_.variables_.end() || 
                 context_.constants_.find(identifier_name) != context_.constants_.end()) &&
                context_.clocks_.find(identifier_name) == context_.clocks_.end()) {

                var_name = identifier_name;
                
                // Evaluate left side and flip the operator
                int left_value;
                if (evaluate_expression(left, left_value)) {
                    value = left_value;
                    
                    // Flip the operator for expressions like "5 == id" -> "id == 5"
                    if (op == ">=") op = "<=";
                    else if (op == ">") op = "<";
                    else if (op == "<=") op = ">=";
                    else if (op == "<") op = ">";
                    // == and != stay the same
                    
                    DEV_PRINT("     Found flipped variable constraint: " << var_name << " " << op << " " << value << std::endl);
                    return true;
                }
            }
        }
    }
    
    return false;
}

bool TimedAutomaton::evaluate_variable_constraint(const std::string& var_name, const std::string& op, int value) const {
    // Check if variable exists in our variable map
    if (context_.variables_.find(var_name) == context_.variables_.end() && context_.constants_.find(var_name) == context_.constants_.end()) {
        DEV_PRINT("     Variable " << var_name << " not found in variables or constants" << std::endl);
        return false;
    }
    
    int var_value;
    if (context_.variables_.find(var_name) != context_.variables_.end()) {
        var_value = context_.variables_.at(var_name);
    } else {
        var_value = context_.constants_.at(var_name);
    }
    
    DEV_PRINT("     Evaluating variable constraint: " << var_name << " (" << var_value << ") " << op << " " << value << std::endl);
    
    // Evaluate the constraint
    if (op == "==") {
        return var_value == value;
    } else if (op == "!=") {
        return var_value != value;
    } else if (op == "<") {
        return var_value < value;
    } else if (op == "<=") {
        return var_value <= value;
    } else if (op == ">") {
        return var_value > value;
    } else if (op == ">=") {
        return var_value >= value;
    }
    
    DEV_PRINT("     Unknown operator: " << op << std::endl);
    return false;
}

bool TimedAutomaton::extract_all_constraints(const UTAP::Expression& expr, std::vector<Constraint>& constraints) {
    if (expr.empty()) {
        return false;
    }
    
    auto kind = expr.get_kind();
    
    // Handle logical operators recursively
    if (kind == UTAP::Constants::AND || kind == UTAP::Constants::OR) {
        bool found_any = false;
        for (size_t i = 0; i < expr.get_size(); ++i) {
            if (extract_all_constraints(expr[i], constraints)) {
                found_any = true;
            }
        }
        return found_any;
    }
    
    // Handle potential comma operator - try different common representations
    // In some UTAP versions, comma might be represented differently
    if (kind == 12 || kind == 11 || kind == 13 || kind == 14 || kind == 15) { // Try various potential comma kinds
        DEV_PRINT("   Found potential comma/sequence operator (kind " << kind << ")" << std::endl);
        bool found_any = false;
        for (size_t i = 0; i < expr.get_size(); ++i) {
            if (extract_all_constraints(expr[i], constraints)) {
                found_any = true;
            }
        }
        return found_any;
    }
    
    // Handle comparison operators
    if (expr.get_size() == 2) {
        std::string op;
        switch (kind) {
            case UTAP::Constants::GE: op = ">="; break;
            case UTAP::Constants::GT: op = ">"; break;
            case UTAP::Constants::LE: op = "<="; break;
            case UTAP::Constants::LT: op = "<"; break;
            case UTAP::Constants::EQ: op = "=="; break;
            case UTAP::Constants::NEQ: op = "!="; break;
            default: return false;
        }
        
        const auto& left = expr[0];
        const auto& right = expr[1];
        
        // Check if left side is an identifier
        if (left.get_kind() == UTAP::Constants::IDENTIFIER) {
            std::string name = left.get_symbol().get_name();
            int value;
            
            if (evaluate_expression(right, value)) {
                Constraint constraint;
                constraint.name = name;
                constraint.op = op;
                constraint.value = value;
                // Check if this is a clock
                constraint.is_clock = (context_.clocks_.find(name) != context_.clocks_.end());

                constraints.push_back(constraint);
                DEV_PRINT("     Extracted constraint: " << name << " " << op << " " << value 
                         << " (is_clock: " << constraint.is_clock << ")" << std::endl);
                return true;
            }
        }
        
        // Check if right side is an identifier
        if (right.get_kind() == UTAP::Constants::IDENTIFIER) {
            std::string name = right.get_symbol().get_name();
            int value;
            
            if (evaluate_expression(left, value)) {
                // Flip the operator
                if (op == ">=") op = "<=";
                else if (op == ">") op = "<";
                else if (op == "<=") op = ">=";
                else if (op == "<") op = ">";
                
                Constraint constraint;
                constraint.name = name;
                constraint.op = op;
                constraint.value = value;
                // Check if this is a clock
                constraint.is_clock = (context_.clocks_.find(name) != context_.clocks_.end());

                constraints.push_back(constraint);
                DEV_PRINT("     Extracted flipped constraint: " << name << " " << op << " " << value 
                         << " (is_clock: " << constraint.is_clock << ")" << std::endl);
                return true;
            }
        }
    }
    
    return false;
}

// NOTE: Legacy helper for complex guard evaluation kept for reference; parsing now
// extracts constraints directly via extract_all_constraints and parse_guard_fallback.
// Leaving the implementation commented out to signal deprecation without affecting ABI.
// bool TimedAutomaton::evaluate_complex_guard(const UTAP::Expression& expr) {
//     ... deprecated logic ...
//     return true;
// }

bool TimedAutomaton::parse_synchronization_from_expr(const UTAP::Expression& expr, std::string& channel, bool& is_sender) {
    if (expr.empty()) {
        return false;
    }
    
    // Handle synchronization by parsing the string representation
    std::string sync_str = expr.str();
    if (!sync_str.empty()) {
        if (sync_str.back() == TA_CONFIG.sender_suffix) {
            channel = sync_str.substr(0, sync_str.length() - 1);
            is_sender = true;
            return true;
        } else if (sync_str.back() == TA_CONFIG.receiver_suffix) {
            channel = sync_str.substr(0, sync_str.length() - 1);
            is_sender = false;
            return true;
        }
    }
    
    return false;
}

void TimedAutomaton::add_dbm_constraint(const std::string& clock_name, const std::string& op, int value,
                                       std::unordered_map<std::string, cindex_t>& clock_map,
                                       int location_id, size_t transition_idx) {
    
    // Store timing constant for candidate-delay method
    timing_constants_.insert(static_cast<int>(value));
    
    // Find or create clock index
    cindex_t clock_idx;
    if (clock_map.find(clock_name) == clock_map.end()) {
        // Assign next available index, ensuring we don't exceed dimension
        clock_idx = clock_map.size() + 1; // +1 because index 0 is reserved for reference clock
        
        // Validate that we don't exceed dimension
        if (clock_idx >= dimension_) {
            std::cerr << "ERROR: Clock index " << clock_idx << " would exceed dimension " << dimension_ 
                      << " for clock " << clock_name << std::endl;
            std::cerr << "Current clock_map size: " << clock_map.size() << std::endl;
            std::cerr << "This suggests too many clocks are being discovered during constraint parsing" << std::endl;
            return;
        }
        
        clock_map[clock_name] = clock_idx;
        DEV_PRINT("     Assigned new clock index " << clock_idx << " to " << clock_name << std::endl);
    } else {
        clock_idx = clock_map[clock_name];
    }

    // Track per-clock maximal constant (upper) and minimal lower bound
    if (clock_idx < clock_max_bounds_.size()) {
        if (op == "<=" || op == "<" || op == "==") {
            clock_max_bounds_[clock_idx] = std::max(clock_max_bounds_[clock_idx], static_cast<int32_t>(value));
        }
        if (op == ">=" || op == ">" || op == "==") {
            // record lower bound candidate (if not set yet or new is larger)
            clock_min_lower_bounds_[clock_idx] = std::max(clock_min_lower_bounds_[clock_idx], static_cast<int32_t>(value));
        }
    }
    
    // Convert operator and value to DBM constraint
    if (op == ">=") {
        // x >= c becomes 0 - x <= -c
        if (location_id >= 0) {
            add_invariant(location_id, 0, clock_idx, -value, dbm_WEAK);
        } else if (transition_idx != static_cast<size_t>(-1)) {
            add_guard(transition_idx, 0, clock_idx, -value, dbm_WEAK);
        }
    } else if (op == ">") {
        // x > c becomes 0 - x <= -c with strict bound
        if (location_id >= 0) {
            add_invariant(location_id, 0, clock_idx, -value, dbm_STRICT);
        } else if (transition_idx != static_cast<size_t>(-1)) {
            add_guard(transition_idx, 0, clock_idx, -value, dbm_STRICT);
        }
    } else if (op == "<=") {
        // x <= c becomes x - 0 <= c
        if (location_id >= 0) {
            add_invariant(location_id, clock_idx, 0, value, dbm_WEAK);
        } else if (transition_idx != static_cast<size_t>(-1)) {
            add_guard(transition_idx, clock_idx, 0, value, dbm_WEAK);
        }
    } else if (op == "<") {
        // x < c becomes x - 0 < c
        if (location_id >= 0) {
            add_invariant(location_id, clock_idx, 0, value, dbm_STRICT);
        } else if (transition_idx != static_cast<size_t>(-1)) {
            add_guard(transition_idx, clock_idx, 0, value, dbm_STRICT);
        }
    } else if (op == "==") {
        // x == c becomes x - 0 <= c AND 0 - x <= -c
        if (location_id >= 0) {
            add_invariant(location_id, clock_idx, 0, value, dbm_WEAK);
            add_invariant(location_id, 0, clock_idx, -value, dbm_WEAK);
        } else if (transition_idx != static_cast<size_t>(-1)) {
            add_guard(transition_idx, clock_idx, 0, value, dbm_WEAK);
            add_guard(transition_idx, 0, clock_idx, -value, dbm_WEAK);
        }
    }
}

// Function implementations moved from header

void TimedAutomaton::add_location(int id, const std::string& name) {
    locations_.emplace_back(id, name);
}

// Helper: ensure bound-tracking arrays are initialised to dimension_ and record
// the timing constant carried by a DBM constraint (i, j, bound).
// Convention (DBM): constraint means  xi - xj <= bound.
//   j==0, i>0  →  xi <= bound          → upper bound on clock i
//   i==0, j>0  →  xj >= -bound         → lower bound on clock j
static inline void track_dbm_bound(std::vector<int32_t>& max_bounds,
                                   std::vector<int32_t>& min_bounds,
                                   std::set<int>&        constants,
                                   cindex_t dim,
                                   cindex_t i, cindex_t j, int32_t bound)
{
    if (max_bounds.size() < dim) {
        max_bounds.assign(dim, 0);
        min_bounds.assign(dim, 0);
    }
    if (j == 0 && i > 0 && i < dim) {
        // xi - x0 <= bound  →  xi <= bound  (upper bound)
        // Ignore near-infinity "no constraint" sentinels.
        if (bound > 0 && bound < dbm_INFINITY) {
            max_bounds[i] = std::max(max_bounds[i], bound);
            constants.insert(static_cast<int>(bound));
        }
    } else if (i == 0 && j > 0 && j < dim) {
        // x0 - xj <= bound  →  xj >= -bound  (lower bound)
        int32_t lb = -bound;
        if (lb > 0 && lb < dbm_INFINITY) {
            min_bounds[j] = std::max(min_bounds[j], lb);
            constants.insert(static_cast<int>(lb));
        }
    }
}

void TimedAutomaton::add_invariant(int location_id, cindex_t i, cindex_t j, int32_t bound, strictness_t strict) {
    for (auto& loc : locations_) {
        if (loc.id == location_id) {
            constraint_t inv = {i, j, dbm_bound2raw(bound, strict)};
            loc.invariants.push_back(inv);
            track_dbm_bound(clock_max_bounds_, clock_min_lower_bounds_,
                            timing_constants_, dimension_, i, j, bound);
            break;
        }
    }
}

void TimedAutomaton::add_transition(int from, int to, const std::string& action) {
    if (action == TA_CONFIG.tau_action_name || action.empty()) 
            transitions_.emplace_back(from, to, TA_CONFIG.tau_action_name);
    else 
            transitions_.emplace_back(from, to, action);
    int transition_idx = transitions_.size() - 1;
    outgoing_transitions_[from].push_back(transition_idx);
}

void TimedAutomaton::add_guard(size_t transition_idx, cindex_t i, cindex_t j, int32_t bound, strictness_t strict) {
    if (transition_idx < transitions_.size()) {
        constraint_t guard = {i, j, dbm_bound2raw(bound, strict)};
        transitions_[transition_idx].guards.push_back(guard);
        track_dbm_bound(clock_max_bounds_, clock_min_lower_bounds_,
                        timing_constants_, dimension_, i, j, bound);
    }
}

void TimedAutomaton::add_reset(size_t transition_idx, cindex_t clock) {
    if (transition_idx < transitions_.size()) {
        transitions_[transition_idx].resets.push_back(clock);
    }
}

void TimedAutomaton::add_synchronization(size_t transition_idx, const std::string& channel, bool is_sender) {
    if (transition_idx < transitions_.size()) {
        transitions_[transition_idx].channel = channel;
        transitions_[transition_idx].is_sender = is_sender;
        transitions_[transition_idx].is_receiver = !is_sender; // Set the receiver flag
        
        // Add to appropriate synchronization map
        if (is_sender) {
            sender_transitions_[channel].push_back(transition_idx);
        } else {
            receiver_transitions_[channel].push_back(transition_idx);
        }
    }
}

void TimedAutomaton::add_channel(const std::string& channel_name) {
    channels_.insert(channel_name);
}

const std::unordered_set<std::string>& TimedAutomaton::get_channels() const {
    return channels_;
}

const std::vector<Transition>& TimedAutomaton::get_transitions() const {
    return transitions_;
}

std::vector<std::pair<int, int>> TimedAutomaton::find_synchronized_pairs(const std::string& channel) const {
    std::vector<std::pair<int, int>> pairs;
    
    auto sender_it = sender_transitions_.find(channel);
    auto receiver_it = receiver_transitions_.find(channel);
    
    if (sender_it != sender_transitions_.end() && receiver_it != receiver_transitions_.end()) {
        for (int sender_idx : sender_it->second) {
            for (int receiver_idx : receiver_it->second) {
                pairs.emplace_back(sender_idx, receiver_idx);
            }
        }
    }
    
    return pairs;
}

void TimedAutomaton::construct_zone_graph(int initial_location, const std::vector<raw_t>& initial_zone,const size_t MAX_STATES, bool force ) {
    if (constructed_ && !force) {
        DEV_PRINT("Zone graph already constructed. Use force=true to rebuild." << std::endl);
        return;
    }
    // Clear previous exploration state
    states_.clear();
    state_map_.clear();
    zone_transitions_.clear();
    waiting_list_ = std::queue<int>();
    
    // Guard: if the initial location doesn't exist in this automaton, there is nothing to explore.
    {
        bool has_initial_loc = false;
        for (const auto& loc : locations_)
            if (loc.id == initial_location) { has_initial_loc = true; break; }
        if (!has_initial_loc) { constructed_ = true; return; }
    }

    // Create initial state: intersect initial zone with invariants of the initial location
    // If inconsistent, nothing to explore
    std::vector<raw_t> init_with_inv = apply_invariants(initial_zone, initial_location);
    if (init_with_inv.empty()) {
        DEV_PRINT("Initial zone violates invariants; nothing to explore." << std::endl);
        constructed_ = true;
        return;
    }
    add_state(initial_location, init_with_inv);
    
    
    
    // Explore using BFS
    while (!waiting_list_.empty() && states_.size() < MAX_STATES) {
        int current_state_id = waiting_list_.front();
        waiting_list_.pop();
        
        explore_state(current_state_id);
        
        // Progress reporting
        if (states_.size() % 10000 == 0) {
            std::cout << "Explored " << states_.size() << " states, " 
                     << waiting_list_.size() << " states in queue" << std::endl;
        }

    }
    
    if (states_.size() >= MAX_STATES) {
        DEV_PRINT("Warning: Reached maximum state limit (" << MAX_STATES 
                 << "). Zone graph exploration stopped." << std::endl);
    }
    constructed_ = true;
}

std::vector<raw_t> TimedAutomaton::time_elapse(const std::vector<raw_t>& zone) const {
    // Validate zone size
    size_t expected_size = dimension_ * dimension_;
    if (zone.size() != expected_size) {
        std::cerr << "ERROR: Zone size mismatch in time_elapse! Expected " << expected_size 
                  << ", got " << zone.size() << std::endl;
        return {}; // Return empty zone
    }
    
    std::vector<raw_t> result = zone;

    // First intersect with non-negative clocks (implicitly ensured by DBM but keep for safety)
    // Then perform the standard Up operation (let time pass)
    dbm_up(result.data(), dimension_);

    // If we have per-clock bounds collected, apply extrapolation respecting each clock's M(x)
    if (!clock_max_bounds_.empty() && clock_max_bounds_.size() == dimension_) {
        // Build LU arrays for potential LU-extrapolation
        int global_max = get_max_timing_constant();
        if (global_max <= 0) global_max = 100;
        std::vector<int32_t> U = clock_max_bounds_;
        std::vector<int32_t> L = clock_min_lower_bounds_;
        for (cindex_t i = 1; i < dimension_; ++i) {
            if (U[i] <= 0) U[i] = global_max;
            // If no explicit lower bound, leave as 0 (clocks non-negative)
        }
        // If LU extrapolation API exists (not detected), fallback to max-bounds.
        //dbm_extrapolateMaxBounds(result.data(), dimension_, U.data());
        dbm_diagonalExtrapolateLUBounds(result.data(), dimension_, L.data(), U.data());
    }

    return result;
}

std::vector<raw_t> TimedAutomaton::time_elapse(const std::vector<raw_t>& zone, double delay) const {
    // Validate zone size
    size_t expected_size = dimension_ * dimension_;
    if (zone.size() != expected_size) {
        std::cerr << "ERROR: Zone size mismatch in time_elapse(zone, delay)! Expected " << expected_size 
                  << ", got " << zone.size() << std::endl;
        return {}; // Return empty zone
    }
    
    if (delay < 0.0) {
        std::cerr << "ERROR: Negative delay in time_elapse! Delay: " << delay << std::endl;
        return {}; // Return empty zone
    }
    
    if (delay == 0.0) {
        return zone; // No time advancement needed
    }
    
    // For integer delays only (Uppaal clocks are integers)
    int32_t delay_int = static_cast<int32_t>(delay);
    if (std::abs(delay - static_cast<double>(delay_int)) > 1e-9) {
        // Non-integer delay: fall back to general time elapse
        // This avoids the complexity of exact fractional delays in DBMs
        return time_elapse(zone);
    }
    
    if (delay_int == 0) {
        return zone; // No advancement for zero delay
    }
    
    // For small integer delays, use a conservative approach:
    // Apply general time elapse (Up operation) then intersect with delay bounds
    std::vector<raw_t> result = time_elapse(zone);
    
    // For very large delays, just return the general time elapse result
    // Exact delay encoding would require auxiliary clocks for full correctness
    if (delay_int > 1000) {
        return result;
    }
    
    // For moderate integer delays, we can try to constrain the result more precisely
    // by adding bounds that represent "at least delay_int time has passed"
    // This is still an approximation without auxiliary elapsed-time variables
    
    // Constraint: for each clock i, x_i >= x_i_initial + delay_int
    // In DBM form: x_0 - x_i <= -delay_int (reference clock falls behind by delay_int)
    for (cindex_t i = 1; i < dimension_; ++i) {
        cindex_t idx_0i = 0 * dimension_ + i;
        raw_t current_bound = result[idx_0i];
        
        if (current_bound != dbm_LE_ZERO) {
            int32_t current_value = dbm_raw2bound(current_bound);
            strictness_t current_strict = dbm_raw2strict(current_bound);
            
            // Make bound more restrictive: old_bound AND new_bound
            int32_t new_bound = -delay_int;
            if (current_value < new_bound || 
                (current_value == new_bound && current_strict == dbm_STRICT)) {
                // Keep the more restrictive bound
                continue;
            } else {
                // Apply the new bound
                result[idx_0i] = dbm_bound2raw(new_bound, dbm_WEAK);
            }
        } else {
            // No existing bound, add the delay constraint
            result[idx_0i] = dbm_bound2raw(-delay_int, dbm_WEAK);
        }
    }
    
    // Close the DBM to maintain canonical form and check consistency
    if (!dbm_close(result.data(), dimension_)) {
        // If zone becomes inconsistent due to the delay constraints,
        // fall back to general time elapse
        return time_elapse(zone);
    }
    
    return result;
}

std::vector<raw_t> TimedAutomaton::apply_invariants(const std::vector<raw_t>& zone, int location_id) const {
    std::vector<raw_t> result = zone;
    
    for (const auto& loc : locations_) {
        if (loc.id == location_id) {
            for (const auto& inv : loc.invariants) {
                dbm_constrain1(result.data(), dimension_, inv.i, inv.j, inv.value);
            }
            break;
        }
    }
    
    // Close the DBM and check consistency
    if (!dbm_close(result.data(), dimension_)) {
        // Return empty zone if inconsistent
        result.clear();
    }
    
    return result;
}

bool TimedAutomaton::is_transition_enabled(const std::vector<raw_t>& zone, const Transition& transition) const {
    // Validate zone size
    size_t expected_size = dimension_ * dimension_;
    if (zone.size() != expected_size) {
        std::cerr << "ERROR: Zone size mismatch! Expected " << expected_size 
                  << ", got " << zone.size() << std::endl;
        return false;
    }
    
    std::vector<raw_t> test_zone = zone;
    
    for (const auto& guard : transition.guards) {
        // Validate guard indices
        if (guard.i >= dimension_ || guard.j >= dimension_) {
            std::cerr << "ERROR: Guard index out of bounds! i=" << guard.i 
                      << ", j=" << guard.j << ", dimension=" << dimension_ << std::endl;
            return false;
        }
        dbm_constrain1(test_zone.data(), dimension_, guard.i, guard.j, guard.value);
    }
    
    return dbm_close(test_zone.data(), dimension_) && !dbm_isEmpty(test_zone.data(), dimension_);
}

std::vector<raw_t> TimedAutomaton::apply_transition(const std::vector<raw_t>& zone, const Transition& transition) const {
    // Validate zone size
    size_t expected_size = dimension_ * dimension_;
    if (zone.size() != expected_size) {
        std::cerr << "ERROR: Zone size mismatch in apply_transition! Expected " << expected_size 
                  << ", got " << zone.size() << std::endl;
        return {}; // Return empty zone
    }
    
    std::vector<raw_t> result = zone;
    
    // Apply guards
    for (const auto& guard : transition.guards) {
        // Validate guard indices
        if (guard.i >= dimension_ || guard.j >= dimension_) {
            std::cerr << "ERROR: Guard index out of bounds in apply_transition! i=" << guard.i 
                      << ", j=" << guard.j << ", dimension=" << dimension_ << std::endl;
            return {}; // Return empty zone
        }
        dbm_constrain1(result.data(), dimension_, guard.i, guard.j, guard.value);
    }
    
    // Apply resets
    for (cindex_t reset_clock : transition.resets) {
        if (reset_clock >= dimension_) {
            std::cerr << "ERROR: Reset clock index out of bounds! clock=" << reset_clock 
                      << ", dimension=" << dimension_ << std::endl;
            return {}; // Return empty zone
        }
        dbm_updateValue(result.data(), dimension_, reset_clock, 0);
    }
    
    // Close the DBM
    if (!dbm_close(result.data(), dimension_)) {
        result.clear();  // Return empty zone if inconsistent
    }
    
    return result;
}

void TimedAutomaton::print_statistics() const {
    std::cout << "Zone Graph Statistics:\n";
    std::cout << "======================\n";
   
    std::cout << "Number of locations: " << locations_.size() << "\n";
    std::cout << "Number of transitions: " << transitions_.size() << "\n";
    std::cout << "Dimension: " << dimension_ << "\n\n";
    

    std::cout << "Number of zones: " << states_.size() << "\n";
    // Count zone graph transitions
    int total_zone_transitions = 0;
    for (const auto& trans_list : zone_transitions_) {
        total_zone_transitions += trans_list.size();
    }
    std::cout << "Number of zone graph transitions: " << total_zone_transitions << "\n\n";
}

void TimedAutomaton::print_state(size_t state_id) const {
    if (state_id < states_.size()) {
        const auto& state = *states_[state_id];
        std::cout << "State " << state_id << " (Location " << state.location_id << "):\n";
        dbm_print(stdout, state.zone.data(), state.dimension);
        std::cout << "\n";
    }
}

void TimedAutomaton::print_all_states() const {
    for (size_t i = 0; i < states_.size(); ++i) {
        print_state(i);
    }
}

size_t TimedAutomaton::get_num_states() const { 
    return states_.size(); 
}

const std::vector<int>& TimedAutomaton::get_successors(size_t state_id) const {
    static const std::vector<int> empty;
    return (state_id < zone_transitions_.size()) ? zone_transitions_[state_id] : empty;
}

int TimedAutomaton::add_state(int location_id, const std::vector<raw_t>& zone) {
    // Validate zone size
    size_t expected_size = dimension_ * dimension_;
    if (zone.size() != expected_size) {
        std::cerr << "ERROR: Zone size mismatch in add_state! Expected " << expected_size 
                  << ", got " << zone.size() << std::endl;
        std::cerr << "Dimension: " << dimension_ << std::endl;
        return -1; // Invalid state
    }
    
    // Create the state
    auto new_state = std::make_unique<ZoneState>(location_id, zone, dimension_);
    
    // Check if state already exists
    auto it = state_map_.find(*new_state);
    if (it != state_map_.end()) {
        return it->second;  // Return existing state ID
    }
    
    // Add new state
    int state_id = states_.size();
    state_map_[*new_state] = state_id;
    states_.push_back(std::move(new_state));
    zone_transitions_.emplace_back();
    waiting_list_.push(state_id);  // Only add new states to waiting list
    
    return state_id;
}

void TimedAutomaton::explore_state(int state_id) {
    const auto& current_state = *states_[state_id];
    // Standard semantics: (Z ∩ Inv(l))^{up} before firing transitions
    auto zone_with_inv = apply_invariants(current_state.zone, current_state.location_id);
    if (zone_with_inv.empty()) return;

    auto elapsed_zone = time_elapse(zone_with_inv);
    if (elapsed_zone.empty()) return;
    
    // IMPORTANT: after Up, re-apply invariants of the current location
    // dbm_up may relax upper bounds; we must keep Inv(l) enforced during delay
    auto ready_zone = apply_invariants(elapsed_zone, current_state.location_id);
    if (ready_zone.empty()) return;
    
    // Try all outgoing transitions from current location
    auto transition_it = outgoing_transitions_.find(current_state.location_id);
    if (transition_it != outgoing_transitions_.end()) {
        for (int transition_idx : transition_it->second) {
            const auto& transition = transitions_[transition_idx];
            
            // Direct attempt: apply transition on ready_zone; if result non-empty and
            // target invariants hold, then the transition is enabled and we add successor.
            auto successor_zone = apply_transition(ready_zone, transition);
            if (!successor_zone.empty()) {
                // Apply invariants in target location
                auto final_zone = apply_invariants(successor_zone, transition.to_location);
                if (!final_zone.empty()) {
                    // Add successor state
                    int successor_id = add_state(transition.to_location, final_zone);
                    zone_transitions_[state_id].push_back(successor_id);
                }
            }
        }
    }
}

void TimedAutomaton::construct_zone_graph() const {
    // Cast away const since we need to modify internal state for lazy initialization
    auto* mutable_this = const_cast<TimedAutomaton*>(this);
    
    if (constructed_) {
        return;  // Already constructed
    }

    // dbm_init asserts dim > 0; a dim-0 component has no states.
    if (dimension_ == 0) { mutable_this->constructed_ = true; return; }
    
    // Create initial zone (all clocks = 0) using proper UDBM initialization
    std::vector<raw_t> initial_zone(dimension_ * dimension_);
    dbm_init(initial_zone.data(), dimension_); // Proper zero zone initialization
    
    // Construct with default parameters (location 0, zero zone)
    mutable_this->construct_zone_graph(TA_CONFIG.default_initial_location, initial_zone, TA_CONFIG.max_states_limit, true); // Force construction
}

const ZoneState* TimedAutomaton::get_zone_state(size_t state_id) const {
    std::shared_lock<std::shared_mutex> rlock(state_mutex_);
    if (state_id >= states_.size()) {
        return nullptr;
    }
    return states_[state_id].get();
}

void TimedAutomaton::reduce_by_zone_subsumption() {
    if (!constructed_ || states_.empty() || dimension_ == 0) return;
    const size_t n = states_.size();

    // Group state indices by location.
    std::unordered_map<int, std::vector<size_t>> by_loc;
    by_loc.reserve(n);
    for (size_t i = 0; i < n; ++i)
        if (states_[i]) by_loc[states_[i]->location_id].push_back(i);

    // Compute representative map: rep[i] == i means i is canonical.
    // zone_i ⊆ zone_j  →  state i is subsumed by j.
    std::vector<size_t> rep(n);
    std::iota(rep.begin(), rep.end(), 0);
    for (auto& [loc, ids] : by_loc) {
        for (size_t i : ids) {
            if (rep[i] != i) continue;   // already redirected
            const ZoneState* si = states_[i].get();
            if (!si) continue;
            for (size_t j : ids) {
                if (j == i || !states_[j]) continue;
                const ZoneState* sj = states_[j].get();
                if (dbm_isSubsetEq(si->zone.data(), sj->zone.data(), dimension_)) {
                    rep[i] = j;
                    break;
                }
            }
        }
    }
    // Flatten chains (path compression).
    for (size_t i = 0; i < n; ++i) {
        size_t r = i;
        while (rep[r] != r) r = rep[r];
        rep[i] = r;
    }

    // Nothing to do if all states are already canonical.
    bool any_reduced = false;
    for (size_t i = 0; i < n; ++i) if (rep[i] != i) { any_reduced = true; break; }
    if (!any_reduced) return;

    // Build compact id map: canonical old_id → new dense id.
    std::vector<int> new_id(n, -1);
    size_t n_c = 0;
    for (size_t i = 0; i < n; ++i)
        if (rep[i] == i) new_id[i] = static_cast<int>(n_c++);

    // Build new states vector (only canonical entries).
    std::vector<std::unique_ptr<ZoneState>> new_states;
    new_states.reserve(n_c);
    for (size_t i = 0; i < n; ++i)
        if (rep[i] == i) new_states.push_back(std::move(states_[i]));

    // Build new zone_transitions_ with remapped + deduplicated successors.
    std::vector<std::vector<int>> new_zt(n_c);
    for (size_t i = 0; i < n; ++i) {
        if (rep[i] != i) continue;   // skip non-canonical sources
        if ((size_t)i >= zone_transitions_.size()) continue;
        int ni = new_id[i];
        std::unordered_set<int> seen;
        for (int succ : zone_transitions_[i]) {
            if (succ < 0 || (size_t)succ >= n) continue;
            int can = new_id[rep[static_cast<size_t>(succ)]];
            if (can >= 0 && seen.insert(can).second)
                new_zt[ni].push_back(can);
        }
    }

    // Rebuild state_map_ with new ids.
    state_map_.clear();
    for (size_t i = 0; i < n_c; ++i)
        if (new_states[i]) state_map_[*new_states[i]] = static_cast<int>(i);

    states_ = std::move(new_states);
    zone_transitions_ = std::move(new_zt);

    std::cerr << "[zone-subsumption] " << name_ << ": " << n << " → " << n_c << " states\n";
}

int TimedAutomaton::get_state_id(const ZoneState* state) const {
    if (!state) return -1;
    std::shared_lock<std::shared_mutex> rlock(state_mutex_);
    auto it = state_map_.find(*state);
    if (it != state_map_.end()) {
        return it->second;
    }
    return -1; // Not found
}



const ZoneState* TimedAutomaton::find_zone_state(int location_id, const std::vector<raw_t>& zone) const {
    // Create temporary zone state for lookup
    ZoneState temp_state(location_id, zone, dimension_);
    
    std::shared_lock<std::shared_mutex> rlock(state_mutex_);
    // Use the hash map for O(1) lookup
    auto it = state_map_.find(temp_state);
    if (it != state_map_.end()) {
        // Found the state, return pointer to the actual state
        int state_id = it->second;
        return states_[state_id].get();
    }
    
    return nullptr; // Not found
}

const ZoneState* TimedAutomaton::get_or_add_zone_state(int location_id, const std::vector<raw_t>& zone) {
    // Validate zone size
    size_t expected_size = static_cast<size_t>(dimension_) * dimension_;
    if (zone.size() != expected_size) {
        return nullptr;
    }

    // Create a temporary key for lookup
    ZoneState key(location_id, zone, dimension_);

    // ── Fast path: shared (read) lock ──
    {
        std::shared_lock<std::shared_mutex> rlock(state_mutex_);
        auto it = state_map_.find(key);
        if (it != state_map_.end()) {
            return states_[it->second].get();
        }
    }

    // ── Slow path: exclusive (write) lock ──
    {
        std::unique_lock<std::shared_mutex> wlock(state_mutex_);
        // Double-check after acquiring write lock (another thread may have
        // inserted the same state between the read-unlock and write-lock).
        auto it = state_map_.find(key);
        if (it != state_map_.end()) {
            return states_[it->second].get();
        }

        // Allocate and register the new state
        int state_id = static_cast<int>(states_.size());
        auto new_state = std::make_unique<ZoneState>(location_id, zone, dimension_);
        state_map_[*new_state] = state_id;
        states_.push_back(std::move(new_state));
        zone_transitions_.emplace_back(); // empty successor list
        // NOTE: we intentionally do NOT push onto waiting_list_ because the
        // on-the-fly DFS drives exploration; the waiting_list_ is only used
        // by the eager construct_zone_graph() BFS.
        return states_[state_id].get();
    }
}

const ZoneState* TimedAutomaton::get_or_create_initial_state() {
    // Guard: if the initial location doesn't exist, return nullptr.
    {
        bool has_initial_loc = false;
        for (const auto& loc : locations_)
            if (loc.id == TA_CONFIG.default_initial_location) { has_initial_loc = true; break; }
        if (!has_initial_loc) return nullptr;
    }

    // Build the zero-DBM for the initial location
    std::vector<raw_t> initial_zone(static_cast<size_t>(dimension_) * dimension_);
    dbm_init(initial_zone.data(), dimension_);

    // Apply invariants of the initial location to the zero zone
    auto inv_zone = apply_invariants(initial_zone, TA_CONFIG.default_initial_location);
    if (inv_zone.empty()) {
        return nullptr; // invariants unsatisfiable at initial location
    }

    return get_or_add_zone_state(TA_CONFIG.default_initial_location, inv_zone);
}


std::vector<const Transition*> TimedAutomaton::get_outgoing_transitions(int location_id) const {
    std::vector<const Transition*> outgoing;
    
    for (const auto& transition : transitions_) {
        if (transition.from_location == location_id) {
            outgoing.push_back(&transition);
        }
    }
    
    return outgoing;
}



Alphabet TimedAutomaton::get_observable_alphabet() const
{

    //simply return a set with all the send and receiving events
    Alphabet observable_alphabet;
    for (const auto& transition : channels_) {
        observable_alphabet.insert(transition);
    }

    return observable_alphabet;
}

void TimedAutomaton::print_all_transitions() const {
    std::cout << "Transitions:\n";
    for (const auto& transition : transitions_) {
        std::cout << "  " << transition.from_location << " --(" << transition.action << ")--> " << transition.to_location << "\n";
    }
}

bool TimedAutomaton::detect_and_expand_function_calls(const UTAP::Expression& expr, std::string& expanded_expr) {
    if (expr.empty()) {
        expanded_expr = "";
        return false;
    }
    
    // Check if this is a function call
    if (expr.get_kind() == UTAP::Constants::FUN_CALL) {
        // Extract function name
        if (expr.get_size() > 0 && expr[0].get_kind() == UTAP::Constants::IDENTIFIER) {
            std::string func_name = expr[0].get_symbol().get_name();
            
            // Check if we have this function in our context
            if (context_.functions_.count(func_name)) {
                const auto& func_info = context_.functions_[func_name];
                
                DEV_PRINT("     Found function call: " << func_name << std::endl);
                DEV_PRINT("     Function body: " << func_info.body << std::endl);
                
                // For now, just expand with the function body
                // In a more sophisticated implementation, you would:
                // 1. Extract function arguments from expr[1], expr[2], etc.
                // 2. Substitute parameter names with argument values in the body
                // 3. Handle recursive function calls
                
                expanded_expr = func_info.body;
                return true;
            } else {
                DEV_PRINT("     Unknown function call: " << func_name << std::endl);
            }
        }
    } else {
        // Check recursively for function calls in sub-expressions
        bool found_any = false;
        std::string result = expr.str(); // Start with original expression
        
        for (size_t i = 0; i < expr.get_size(); ++i) {
            std::string sub_expanded;
            if (detect_and_expand_function_calls(expr[i], sub_expanded)) {
                found_any = true;
                // In a full implementation, you'd replace the sub-expression
                // with the expanded version in the result string
            }
        }
        
        if (found_any) {
            expanded_expr = result;
            return true;
        }
    }
    
    expanded_expr = expr.str();
    return false;
}


void TimedAutomaton::save_as_simple_file(const std::string& filename) const {
    std::ofstream ofs(filename);
    if (!ofs) {
        std::cerr << "ERROR: Could not open file for writing: " << filename << std::endl;
        return;
    }
    
    ofs << "Locations:\n";
    for (const auto& loc : locations_) {
        ofs << "  " << loc.id << ": " << loc.name << "\n";
        for (const auto& inv : loc.invariants) {
            ofs << "    Invariant: x" << inv.i << " - x" << inv.j 
                << " <= " << dbm_raw2bound(inv.value) 
                << (dbm_raw2strict(inv.value) == dbm_STRICT ? " (strict)" : "") 
                << "\n";
        }
    }
    
    ofs << "\nTransitions:\n";
    for (const auto& transition : transitions_) {
        ofs << "  " << transition.from_location << " --(" << transition.action << ")--> " << transition.to_location << "\n";
    }

}


void TimedAutomaton::print_zone_graph()
{
    //prints the zones and transitions between zones
    std::cout << "Zones:{";
    for (auto& s : states_)
    {
        std::cout << s->to_string() << ",";
    }

    std::cout << "}\n Transitions{ \n";
    int count = 0;
    std::cout << "State id -> {Successor States}\n";
    for (const auto& vec : zone_transitions_) {
        std::cout << count << "->{";
        for (const auto& t : vec)
            std::cout << t << ",";
        std::cout <<"};\n";
        count ++;
    }
    std::cout << "}";

}
} // namespace dtpta
