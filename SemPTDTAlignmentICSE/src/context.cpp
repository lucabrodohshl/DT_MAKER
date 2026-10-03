#include "dtpta/context.h"
#include <stdexcept>
#include <sstream>

namespace dtpta {

// StructValue constructor
StructValue::StructValue(const std::string& name) : type_name(name) {}

// FunctionInfo constructor
FunctionInfo::FunctionInfo(const std::string& fn_name, const std::string& ret_type) 
    : name(fn_name), return_type(ret_type) {}

// Function access methods
bool Context::has_function(const std::string& name) const {
    return functions_.count(name) > 0;
}

FunctionInfo Context::get_function_info(const std::string& name) const {
    auto it = functions_.find(name);
    if (it != functions_.end()) {
        return it->second;
    }
    throw std::runtime_error("Function not found: " + name);
}

std::vector<std::string> Context::get_function_names() const {
    std::vector<std::string> names;
    for (const auto& pair : functions_) {
        names.push_back(pair.first);
    }
    return names;
}

// Function call verification (check if parameters match expected types)
bool Context::can_call_function(const std::string& name, const std::vector<std::string>& param_types) const {
    if (!has_function(name)) return false;
    
    auto func_info = get_function_info(name);
    if (func_info.parameters.size() != param_types.size()) return false;
    
    // For now, just check parameter count - more sophisticated type checking could be added
    return true;
}

// Method to print all parsed structs (for debugging)
void Context::print_struct_info() {
    DEV_PRINT("\n=== Parsed Struct Constants ===" << std::endl);
    for (const auto& [name, struct_val] : struct_constants_) {
        DEV_PRINT("struct constant '" << name << "' (type: " << struct_val.type_name << "):" << std::endl);
        print_struct_value(struct_val, 2);
    }

    DEV_PRINT("\n=== Parsed Struct Variables ===" << std::endl);
    for (const auto& [name, struct_val] : struct_variables_) {
        DEV_PRINT("struct variable '" << name << "' (type: " << struct_val.type_name << "):" << std::endl);
        print_struct_value(struct_val, 2);
    }
}

// Method to access struct constants by name
bool Context::get_struct_constant(const std::string& name, StructValue& result) {
    auto it = struct_constants_.find(name);
    if (it != struct_constants_.end()) {
        result = it->second;
        return true;
    }
    return false;
}

// Method to access struct variables by name
bool Context::get_struct_variable(const std::string& name, StructValue& result) {
    auto it = struct_variables_.find(name);
    if (it != struct_variables_.end()) {
        result = it->second;
        return true;
    }
    return false;
}

// Parse a single variable declaration
void Context::parse_declaration(const UTAP::Variable& variable) {
    std::string var_name = variable.uid.get_name();
    DEV_PRINT("Here: " << var_name << std::endl);
    if (variable.uid.get_type().is_clock()) {
            // Handle clocks
            clocks_[var_name] = next_clock_index_++;
            DEV_PRINT("   Added global clock: " << var_name << " -> " << clocks_[var_name] << std::endl);
            
        } else if (variable.uid.get_type().is_array()) {
            // Handle arrays
            //DEV_PRINT("   Found array: " << var_name << std::endl);
            
            if (!variable.init.empty()) {
                std::vector<double> array_values;
                if (parse_array_initializer(variable.init, array_values)) {
                    arrays_[var_name] = array_values;
                    DEV_PRINT("   Added array: " << var_name << " with " << array_values.size() << " elements" << std::endl);
                } else {
                    DEV_PRINT("   Failed to parse array initializer for: " << var_name << std::endl);
                    throw std::runtime_error("Failed to parse array initializer for " + var_name);
                }
            } else {
                DEV_PRINT("   Array without initializer: " << var_name << std::endl);
            }
            
        } else if (variable.uid.get_type().is_record()) {
            // Handle struct/record types
            //DEV_PRINT("   Found struct: " << var_name << " (type: " << variable.uid.get_type().str() << ")" << std::endl);
            
            if (!variable.init.empty()) {
                DEV_PRINT("   Struct init expression kind: " << variable.init.get_kind() << std::endl);
                StructValue struct_value;
                struct_value.type_name = variable.uid.get_type().str();
                
                // Handle direct reference to another struct constant (like sig_out = empty_sig)
                if (variable.init.get_kind() == UTAP::Constants::IDENTIFIER) {
                    std::string ref_name = variable.init.get_symbol().get_name();
                    if (struct_constants_.count(ref_name)) {
                        struct_value = struct_constants_[ref_name]; // Copy the referenced struct
                        struct_value.type_name = variable.uid.get_type().str(); // Update the type name
                        
                        if (variable.uid.get_type().is_constant()) {
                            struct_constants_[var_name] = struct_value;
                            DEV_PRINT("   Added struct constant (copy): " << var_name << " = " << ref_name << std::endl);
                        } else {
                            struct_variables_[var_name] = struct_value;
                            DEV_PRINT("   Added struct variable (copy): " << var_name << " = " << ref_name << std::endl);
                        }
                    } else {
                        DEV_PRINT("   Referenced struct not found: " << ref_name << std::endl);
                        throw std::runtime_error("Referenced struct constant not found: " + ref_name);
                    }
                } else if (parse_struct_initializer(variable.init, struct_value)) {
                    if (variable.uid.get_type().is_constant()) {
                        struct_constants_[var_name] = struct_value;
                        DEV_PRINT("   Added struct constant: " << var_name << std::endl);
                    } else {
                        struct_variables_[var_name] = struct_value;
                        DEV_PRINT("   Added struct variable: " << var_name << std::endl);
                    }
                } else {
                    DEV_PRINT("   Failed to parse struct initializer: " << var_name << std::endl);
                    throw std::runtime_error("Failed to parse struct initializer for " + var_name);
                }
            } else {
                // Struct without initializer - create empty struct
                StructValue empty_struct;
                empty_struct.type_name = variable.uid.get_type().str();
                struct_variables_[var_name] = empty_struct;
                DEV_PRINT("   Added struct variable (no init): " << var_name << std::endl);
            }
            
        } else if (variable.uid.get_type().is_constant() || variable.uid.get_type().is_integer() || variable.uid.get_type().is_double()) {
            // Handle scalar constants and variables
            if (!variable.init.empty()) {
                double value;
                if (evaluate_expression(variable.init, value)) {
                    if (variable.uid.get_type().is_constant()) {
                        constants_[var_name] = value;
                        //DEV_PRINT("   Found global constant: " << var_name << " = " << value << std::endl);
                    } else {
                        variables_[var_name] = value;
                        //DEV_PRINT("   Found global variable: " << var_name << " = " << value << std::endl);
                    }
                } else {
                    DEV_PRINT("   Failed to evaluate global constant: " << var_name << std::endl);
                    DEV_PRINT("     Expression kind: " << variable.init.get_kind() << std::endl);
                    DEV_PRINT("     Expression string: " << variable.init.str() << std::endl);
                    throw std::runtime_error("Failed to evaluate expression for " + var_name);
                }
            } else {
                // Variable without initializer
                variables_[var_name] = 0.0; // Default value
                DEV_PRINT("   Found global variable: " << var_name << std::endl);
            }
        } else {
            // Other types (channels, etc.)
            DEV_PRINT("   Found other declaration: " << var_name << " (type: " << variable.uid.get_type().str() << ")" << std::endl);
            variables_[var_name] = 0.0; // Default value
        }
}

// Parse global declarations from UTAP document
void Context::parse_global_declarations(const UTAP::Declarations& global_declarations) {
    DEV_PRINT("   Parsing global declarations..." << std::endl);
    DEV_PRINT("   Global declarations has " << global_declarations.variables.size() << " variables:" << std::endl);
    
    for (const auto& variable : global_declarations.variables) {
        parse_declaration(variable);
    }
    
    // Parse functions
    DEV_PRINT("   Global declarations has " << global_declarations.functions.size() << " functions:" << std::endl);
    for (const auto& function : global_declarations.functions) {
        parse_function(function);
    }
}

// Parse function declarations
void Context::parse_function(const UTAP::Function& function) {
    std::string func_name = function.uid.get_name();
    std::string return_type = function.uid.get_type().str();
    
    DEV_PRINT("   Found function: " << func_name << " -> " << return_type << std::endl);
    
    FunctionInfo func_info(func_name, return_type);
    
    // Parse function parameters if available
    auto func_type = function.uid.get_type();
    if (func_type.is_function()) {
        // Extract parameter types and names
        for (uint32_t i = 1; i < func_type.size(); ++i) { // Skip return type (index 0)
            auto param_type = func_type[i];
            std::string param_type_str = param_type.str();
            std::string param_name = "param" + std::to_string(i-1); // Default name
            func_info.parameters.push_back({param_name, param_type_str});
            DEV_PRINT("     Parameter[" << (i-1) << "]: " << param_name << " : " << param_type_str << std::endl);
        }
    }
    
    // Store function body as string if available
    if (function.body) {
        // Use the print method to get the function body as a string
        std::ostringstream body_stream;
        function.body->print(body_stream, "");
        func_info.body = body_stream.str();
        DEV_PRINT("     Function body: " << func_info.body << std::endl);
    } else {
        func_info.body = ""; // No body (declaration only)
    }
    
    functions_[func_name] = func_info;
    DEV_PRINT("   Added function: " << func_name << std::endl);
}

// Evaluate a single expression
bool Context::evaluate_expression(const UTAP::Expression& expr, double& result) {
    if (expr.empty()) {
        DEV_PRINT("Empty expression cannot be evaluated" << std::endl);
        return false;
    }
    
    auto kind = expr.get_kind();
    DEV_PRINT("Expression kind: " << kind << std::endl);
    
    switch (kind) {
        case UTAP::Constants::CONSTANT: {

            if(expr.get_type().is_integral()) {
                    
                result = expr.get_value();
            }else{
                result = expr.get_double_value();
                DEV_PRINT("Constant value (double): " << result << std::endl);
            }
            
            return true;
        }
        
        case UTAP::Constants::IDENTIFIER: {
            auto symbol = expr.get_symbol();
            std::string name = symbol.get_name();
            
            // Check if it's a known constant
            if (constants_.count(name)) {
                result = constants_.at(name);
                return true;
            }
            // Check if it's a known variable
            if (variables_.count(name)) {
                result = variables_.at(name);
                return true;
            }
            
            // For variables with initializers, try to evaluate recursively
            auto* var_data = static_cast<const UTAP::Variable*>(symbol.get_data());
            if (var_data && !var_data->init.empty()) {
                return evaluate_expression(var_data->init, result);
            }
            
            DEV_PRINT("Identifier not found: " << name << std::endl);
            return false;
        }
        
        case UTAP::Constants::PLUS:
            if (expr.get_size() == 2) {
                double left, right;
                if (evaluate_expression(expr[0], left) && evaluate_expression(expr[1], right)) {
                    result = left + right;
                    return true;
                }
            }
            return false;
            
        case UTAP::Constants::MINUS:
            if (expr.get_size() == 2) {
                double left, right;
                if (evaluate_expression(expr[0], left) && evaluate_expression(expr[1], right)) {
                    result = left - right;
                    return true;
                }
            }
            return false;
            
        case UTAP::Constants::MULT:
            if (expr.get_size() == 2) {
                double left, right;
                if (evaluate_expression(expr[0], left) && evaluate_expression(expr[1], right)) {
                    result = left * right;
                    return true;
                }
            }
            return false;
            
        case UTAP::Constants::DIV:
            if (expr.get_size() == 2) {
                double left, right;
                if (evaluate_expression(expr[0], left) && evaluate_expression(expr[1], right) && right != 0) {
                    result = left / right;
                    return true;
                }
            }
            return false;
            
        case UTAP::Constants::UNARY_MINUS:
            if (expr.get_size() == 1) {
                double val;
                if (evaluate_expression(expr[0], val)) {
                    result = -val;
                    return true;
                }
            }
            return false;
            
        case UTAP::Constants::LIST:
            // Lists cannot be evaluated as single values
            DEV_PRINT("Cannot evaluate LIST as single value" << std::endl);
            return false;
            
        case UTAP::Constants::ARRAY: {
            // Handle array access like arr[index]
            if (expr.get_size() == 2) {
                // Get array name
                if (expr[0].get_kind() == UTAP::Constants::IDENTIFIER) {
                    std::string array_name = expr[0].get_symbol().get_name();
                    
                    // Get index
                    double index_val;
                    if (evaluate_expression(expr[1], index_val)) {
                        int index = static_cast<int>(index_val);
                        
                        if (arrays_.count(array_name) && index >= 0 && index < arrays_[array_name].size()) {
                            result = arrays_[array_name][index];
                            DEV_PRINT("Array access: " << array_name << "[" << index << "] = " << result << std::endl);
                            return true;
                        } else {
                            DEV_PRINT("Array access failed: " << array_name << "[" << index << "]" << std::endl);
                        }
                    }
                }
            }
            return false;
        }
        
        default:
            DEV_PRINT("Unsupported expression kind for evaluation: " << kind << std::endl);
            return false;
    }
}

// Parse array initializers like {1, 2, 3}
bool Context::parse_array_initializer(const UTAP::Expression& expr, std::vector<double>& values) {
    values.clear();
    
    if (expr.get_kind() == UTAP::Constants::LIST) {
        // Handle list initializers like {1, 2, 3}
        for (size_t i = 0; i < expr.get_size(); ++i) {
            double element_value;
            if (evaluate_expression(expr[i], element_value)) {
                values.push_back(element_value);
                DEV_PRINT("     Array[" << i << "] = " << element_value << std::endl);
            } else {
                DEV_PRINT("     Failed to evaluate array element[" << i << "]" << std::endl);
                values.push_back(0.0); // Default value
            }
        }
        return true;
    } else if (expr.get_kind() == UTAP::Constants::IDENTIFIER) {
        // Handle identifier references like msgDelayStandard
        auto symbol = expr.get_symbol();
        std::string array_name = symbol.get_name();
        
        DEV_PRINT("     Array init from identifier: " << array_name << std::endl);
        
        // Check if it's a known array
        if (arrays_.count(array_name)) {
            values = arrays_.at(array_name);
            DEV_PRINT("     Copied array from " << array_name << " with " << values.size() << " elements" << std::endl);
            return true;
        } else {
            DEV_PRINT("     Array identifier not found: " << array_name << std::endl);
            return false;
        }
    }
    
    return false;
}

// Parse struct initializers like {0, 0, 0} or {0, {1, 2, 3}}
bool Context::parse_struct_initializer(const UTAP::Expression& expr, StructValue& struct_value) {
    if (expr.get_kind() != UTAP::Constants::LIST) {
        return false;
    }
    
    struct_value.fields.clear();
    for (size_t i = 0; i < expr.get_size(); ++i) {
        std::string field_name = "field_" + std::to_string(i); // Generic field name
        
        if (expr[i].get_kind() == UTAP::Constants::LIST) {
            // Nested struct - recursively parse
            StructValue nested_struct;
            if (parse_struct_initializer(expr[i], nested_struct)) {
                struct_value.fields.push_back({field_name, nested_struct});
                DEV_PRINT("     Struct[" << i << "] = nested struct with " << nested_struct.fields.size() << " fields" << std::endl);
            } else {
                DEV_PRINT("     Failed to parse nested struct at field[" << i << "]" << std::endl);
                return false;
            }
        } else if (expr[i].get_kind() == UTAP::Constants::IDENTIFIER) {
            // Reference to another constant/variable
            std::string ref_name = expr[i].get_symbol().get_name();
            
            // Check if it's a struct constant
            if (struct_constants_.count(ref_name)) {
                struct_value.fields.push_back({field_name, struct_constants_[ref_name]});
                DEV_PRINT("     Struct[" << i << "] = reference to struct constant: " << ref_name << std::endl);
            } else {
                // Try to evaluate as scalar
                double scalar_value;
                if (evaluate_expression(expr[i], scalar_value)) {
                    struct_value.fields.push_back({field_name, scalar_value});
                    DEV_PRINT("     Struct[" << i << "] = " << scalar_value << " (from reference " << ref_name << ")" << std::endl);
                } else {
                    DEV_PRINT("     Could not resolve reference: " << ref_name << ", treating as forward reference" << std::endl);
                    // For forward references (e.g., when parsing sig_out = empty_sig), 
                    // we need to handle this differently - for now, just store the name
                    struct_value.fields.push_back({field_name, 0.0}); // Placeholder
                }
            }
        } else {
            // Direct scalar value
            double scalar_value;
            if (evaluate_expression(expr[i], scalar_value)) {
                struct_value.fields.push_back({field_name, scalar_value});
                DEV_PRINT("     Struct[" << i << "] = " << scalar_value << std::endl);
            } else {
                DEV_PRINT("     Failed to evaluate struct field[" << i << "]" << std::endl);
                struct_value.fields.push_back({field_name, 0.0}); // Default value
            }
        }
    }
    
    return true;
}

// Helper method to print struct values recursively
void Context::print_struct_value(const StructValue& struct_val, int indent) {
    std::string indent_str(indent, ' ');
    for (size_t i = 0; i < struct_val.fields.size(); ++i) {
        const auto& field = struct_val.fields[i];
        std::cout << indent_str << "[" << i << "] " << field.first << " = ";
        
        std::visit([&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, double>) {
                std::cout << value << std::endl;
            } else if constexpr (std::is_same_v<T, StructValue>) {
                std::cout << "struct {" << std::endl;
                print_struct_value(value, indent + 2);
                std::cout << indent_str << "}" << std::endl;
            } else if constexpr (std::is_same_v<T, std::vector<double>>) {
                std::cout << "[";
                for (size_t j = 0; j < value.size(); ++j) {
                    std::cout << value[j];
                    if (j < value.size() - 1) std::cout << ", ";
                }
                std::cout << "]" << std::endl;
            }
        }, field.second);
    }
}

} // namespace dtpta
