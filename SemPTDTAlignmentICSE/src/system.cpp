#include "dtpta/system.h"
#include "dtpta/configs.h"
#include "utap/utap.hpp"
#include <iostream>
#include <stdexcept>

#include "dtpta/context.h"

namespace dtpta {

// =================================================================
// System Class Implementation
// =================================================================

System::System(const std::string& fileName) {
        
        //std::cout << "Loading system from file: " << fileName << std::endl;
        
        // if file doesnt exist, throw error    
        // For now, we'll throw an exception indicating this needs proper UTAP integration
        //check if file doesnt exist
        if (!std::filesystem::exists(fileName))
        {
            throw std::runtime_error("File not found: " + fileName);
        }
        
        
        // Future implementation would be:
        UTAP::Document doc;

        DEV_PRINT("Parsing XML file..." << std::endl);
        try {
            int result = parse_XML_file(fileName, doc, true);
            DEV_PRINT("Parse result: " << result << std::endl);

            // Check if parsing was successful
            if (result != 0) {
                throw std::runtime_error("Failed to parse XML file: " + fileName + " (error code: " + std::to_string(result) + ")");
            }

            DEV_PRINT("Document parsed successfully" << std::endl);
            DEV_PRINT("Number of templates: " << doc.get_templates().size() << std::endl);

            // Check global declarations
            auto& globals = doc.get_globals();
            DEV_PRINT("Global declarations variables count: " << globals.variables.size() << std::endl);

            // Print some debug info about global declarations
            DEV_PRINT("Globals object address: " << &globals << std::endl);

            build_from_utap_document(doc);
        } catch (const std::exception& e) {
            std::cerr << "Exception during parsing: " << e.what() << std::endl;
            std::cerr << "Exception type: " << typeid(e).name() << std::endl;
            throw; // Re-throw to maintain error propagation
        }

}

// Convenience overload to allow passing C-string paths directly
System::System(const char* fileNameCStr) : System(std::string(fileNameCStr)) {}


void System::add_automaton(std::unique_ptr<TimedAutomaton> automaton, const std::string& template_name) {
    if (!automaton) {
        throw std::invalid_argument("Cannot add null automaton to system");
    }
    
    if (template_name.empty()) {
        throw std::invalid_argument("Template name cannot be empty");
    }
    
    if (has_template(template_name)) {
        throw std::invalid_argument("Template '" + template_name + "' already exists in system");
    }
    
    name_to_index_[template_name] = automata_.size();
    template_names_.push_back(template_name);
    automata_.push_back(std::move(automaton));

    DEV_PRINT("Added automaton '" << template_name << "' to system (index " << (automata_.size() - 1) << ")" << std::endl);
}

const TimedAutomaton& System::get_automaton(size_t index) const {
    validate_index(index);
    return *automata_[index];
}

TimedAutomaton& System::get_automaton(size_t index) {
    validate_index(index);
    return *automata_[index];
}

const TimedAutomaton& System::get_automaton(const std::string& template_name) const {
    auto it = name_to_index_.find(template_name);
    if (it == name_to_index_.end()) {
        throw std::invalid_argument("Template '" + template_name + "' not found in system");
    }
    return *automata_[it->second];
}

TimedAutomaton& System::get_automaton(const std::string& template_name) {
    auto it = name_to_index_.find(template_name);
    if (it == name_to_index_.end()) {
        throw std::invalid_argument("Template '" + template_name + "' not found in system");
    }
    return *automata_[it->second];
}

bool System::has_template(const std::string& template_name) const {
    return name_to_index_.find(template_name) != name_to_index_.end();
}

const std::string& System::get_template_name(size_t index) const {
    validate_index(index);
    return template_names_[index];
}

void System::construct_all_zone_graphs() {
    //std::cout << "Constructing zone graphs for all " << automata_.size() << " automata..." << std::endl;
    
    for (size_t i = 0; i < automata_.size(); ++i) {
        //std::cout << "  Constructing zone graph for '" << template_names_[i] << "'..." << std::endl;
        automata_[i]->construct_zone_graph();
    }
    
    std::cout << "Zone graph construction complete for all automata." << std::endl;
}

void System::print_all_statistics() const {
    std::cout << "=== System Statistics ===" << std::endl;
    std::cout << "Number of automata: " << automata_.size() << std::endl;
    std::cout << std::endl;
    
    for (size_t i = 0; i < automata_.size(); ++i) {
        std::cout << "--- Automaton " << i << ": " << template_names_[i] << " ---" << std::endl;
        automata_[i]->print_statistics();
        std::cout << std::endl;
    }
    //print a summ of all zone graph statistics
    size_t total_zones = 0;
    size_t total_transitions_zg = 0;
    for (const auto& automaton : automata_) {
        total_zones += automaton->get_num_zones();
        total_transitions_zg += automaton->get_num_transition_zg();

    }
        std::cout << "=== Total System Statistics ===" << std::endl;
        std::cout << "Number of Automata: " << automata_.size() << std::endl;
    std::cout << "Total zones: " << total_zones << std::endl;
    std::cout << "Total zone graph transitions: " << total_transitions_zg << std::endl;

    std::cout << "=========================" << std::endl;
}

void System::print_system_overview() const {
    std::cout << "=== System Overview ===" << std::endl;
    std::cout << "Total automata: " << automata_.size() << std::endl;
    
    for (size_t i = 0; i < automata_.size(); ++i) {
        std::cout << "  [" << i << "] " << template_names_[i] 
                  << " (dimension: " << automata_[i]->get_dimension() 
                  << ", states: " << automata_[i]->get_num_states() << ")" << std::endl;
    }
    std::cout << "======================" << std::endl;
}

void System::remove_automaton(size_t index) {
    validate_index(index);
    
    std::string template_name = template_names_[index];
    
    // Remove from containers
    automata_.erase(automata_.begin() + index);
    template_names_.erase(template_names_.begin() + index);
    
    // Update name_to_index map
    name_to_index_.erase(template_name);
    for (auto& pair : name_to_index_) {
        if (pair.second > index) {
            pair.second--;
        }
    }
    
    std::cout << "Removed automaton '" << template_name << "' from system" << std::endl;
}

void System::remove_automaton(const std::string& template_name) {
    auto it = name_to_index_.find(template_name);
    if (it == name_to_index_.end()) {
        throw std::invalid_argument("Template '" + template_name + "' not found in system");
    }
    
    size_t index = it->second;
    remove_automaton(index);
}

void System::clear() {
    automata_.clear();
    template_names_.clear();
    name_to_index_.clear();
    
    std::cout << "Cleared all automata from system" << std::endl;
}

void System::build_from_utap_document(UTAP::Document& doc) {
    // This is where you would parse the UTAP document and create automata
    // For each template in the document, create a separate TimedAutomaton
    
    //std::cout << "Building system from UTAP document..." << std::endl;
    
    // Simplified implementation - in practice this would:
    // 1. Iterate through all templates in the document
    // 2. For each template, create a TimedAutomaton
    // 3. Parse the template's locations, transitions, etc.
    // 4. Add the automaton to the system
    
    std::unordered_map<std::string, cindex_t> clock_map;
    std::unordered_map<std::string, double> constants;
    std::unordered_map<std::string, double> variables;  // Non-constant variables like 'id'
    
    // Extract clocks from global declarations
    auto& global_declarations = doc.get_globals();
    cindex_t clock_count = 0;
    Context context;
    
    DEV_PRINT("   Analyzing global declarations..." << std::endl);
    
    DEV_PRINT("   Global declarations has " << global_declarations.variables.size() << " variables:" << std::endl);
    
    context.parse_global_declarations(global_declarations);



    // Set dimension = reference clock (index 0) + number of clocks from global context
    cindex_t total_clocks = context.clocks_.size() + 1; // +1 for reference clock

    
    // Set dimension = reference clock (index 0) + number of clocks

    //TimedAutomaton automaton(template_ref, clock_count+1, clock_map, constants, variables);
    // Future implementation outline:
    
    //throw std::runtime_error("UTAP document parsing not fully implemented - use manual construction");

    for (const auto& template_ref : doc.get_templates()) {

            
            // Create automaton from template
            auto automaton = 
                std::make_unique<TimedAutomaton>(template_ref, total_clocks, context); 
        // Add to system
        add_automaton(std::move(automaton), template_ref.uid.get_name());
    }
    
}



void System::validate_index(size_t index) const {
    if (index >= automata_.size()) {
        throw std::out_of_range("Automaton index " + std::to_string(index) + 
                               " out of range (system has " + std::to_string(automata_.size()) + " automata)");
    }
}

} // namespace dtpta
