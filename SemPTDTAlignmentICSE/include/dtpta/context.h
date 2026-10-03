
#ifndef CONTEXT_H
#define CONTEXT_H

#include "utap/utap.hpp"
#include <unordered_map>
#include <vector>
#include <string>
#include <sstream>
#include <iostream>
#include <variant>
#include "macros.h"
#include "dbm/dbm.h"
#include "dbm/fed.h"
#include "dbm/constraints.h"
#include "dbm/print.h"

namespace dtpta {

// Structure to represent a parsed struct value
struct StructValue {
    std::string type_name;
    std::vector<std::pair<std::string, std::variant<double, StructValue, std::vector<double>>>> fields;
    
    StructValue() = default;
    StructValue(const std::string& name);
};

// Structure to represent a parsed function
struct FunctionInfo {
    std::string name;
    std::string return_type;
    std::vector<std::pair<std::string, std::string>> parameters; // name, type pairs
    std::string body; // function body as string
    
    FunctionInfo() = default;
    FunctionInfo(const std::string& fn_name, const std::string& ret_type);
};

// Global context manager that stores all variables, constants, clocks, and arrays
class Context {
public:
    // Storage for different types of declarations
    std::unordered_map<std::string, double> constants_;
    std::unordered_map<std::string, double> variables_;
    std::unordered_map<std::string, std::vector<double>> arrays_;
    std::unordered_map<std::string, StructValue> struct_constants_;  // struct constants
    std::unordered_map<std::string, StructValue> struct_variables_;  // struct variables
    std::unordered_map<std::string, FunctionInfo> functions_;        // function declarations
    std::unordered_map<std::string, cindex_t> clocks_;  // clock_name -> clock_index
    int next_clock_index_ = 1; // Reference clock is 0
    
    Context() = default;
    
    // Function access methods
    bool has_function(const std::string& name) const;
    FunctionInfo get_function_info(const std::string& name) const;
    std::vector<std::string> get_function_names() const;
    
    // Function call verification (check if parameters match expected types)
    bool can_call_function(const std::string& name, const std::vector<std::string>& param_types) const;
    
    // Method to print all parsed structs (for debugging)
    void print_struct_info();
    
    // Method to access struct constants by name
    bool get_struct_constant(const std::string& name, StructValue& result);
    
    // Method to access struct variables by name
    bool get_struct_variable(const std::string& name, StructValue& result);
    
    // Parse a single variable declaration
    void parse_declaration(const UTAP::Variable& variable);
    
    // Parse global declarations from UTAP document
    void parse_global_declarations(const UTAP::Declarations& global_declarations);
    
    // Parse function declarations
    void parse_function(const UTAP::Function& function);
    
    // Evaluate a single expression
    bool evaluate_expression(const UTAP::Expression& expr, double& result);
    
private:
    // Parse array initializers like {1, 2, 3}
    bool parse_array_initializer(const UTAP::Expression& expr, std::vector<double>& values);
    
    // Parse struct initializers like {0, 0, 0} or {0, {1, 2, 3}}
    bool parse_struct_initializer(const UTAP::Expression& expr, StructValue& struct_value);
    
    // Helper method to print struct values recursively
    void print_struct_value(const StructValue& struct_val, int indent);
};

} // namespace dtpta

#endif // CONTEXT_H