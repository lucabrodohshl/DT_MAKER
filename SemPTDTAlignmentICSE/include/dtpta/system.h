#ifndef SYSTEM_H
#define SYSTEM_H

#include "timedautomaton.h"
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>

// Forward declarations for UTAP types
namespace UTAP {
    class Document;
    class Template;
}

namespace dtpta {

/**
 * System class that manages multiple TimedAutomaton templates
 * 
 * A System represents a collection of timed automata instantiated from templates.
 * Each template can be instantiated multiple times with different parameters.
 * This is the correct abstraction for real-world verification scenarios.
 * 
 * Key Design Principles:
 * - TimedAutomaton = individual template/component
 * - System = collection of related automata
 * - DTPTA checking = pairwise comparison between corresponding automata
 */
class System {
private:
    std::vector<std::unique_ptr<TimedAutomaton>> automata_;  // Vector of automaton instances
    std::vector<std::string> template_names_;                // Names of the templates
    std::unordered_map<std::string, size_t> name_to_index_;  // Map template names to indices
    
public:
    /**
     * Default constructor
     */
    System() = default;
    
    /**
     * Constructor that parses UPPAAL XML file and creates automata from templates
     * @param fileName: UPPAAL file name containing multiple templates
     */
    explicit System(const std::string& fileName);
    
    /**
     * Constructor that parses UPPAAL XML content and creates automata from templates
     * @param xml_content: UPPAAL XML content as string
     */
    explicit System(const char* xml_content);
    
    /**
     * Add a single automaton template to the system
     * @param automaton: Unique pointer to the automaton
     * @param template_name: Name identifier for this template
     */
    void add_automaton(std::unique_ptr<TimedAutomaton> automaton, const std::string& template_name);
    
    /**
     * Get automaton by index (const version)
     */
    const TimedAutomaton& get_automaton(size_t index) const;
    
    /**
     * Get automaton by index (mutable version)
     */
    TimedAutomaton& get_automaton(size_t index);
    
    /**
     * Get automaton by template name (const version)
     */
    const TimedAutomaton& get_automaton(const std::string& template_name) const;
    
    /**
     * Get automaton by template name (mutable version)
     */
    TimedAutomaton& get_automaton(const std::string& template_name);
    
    /**
     * Get number of automata in the system
     */
    size_t size() const { return automata_.size(); }
    
    /**
     * Check if system is empty
     */
    bool empty() const { return automata_.empty(); }
    
    /**
     * Get all template names
     */
    const std::vector<std::string>& get_template_names() const { return template_names_; }
    
    /**
     * Check if template exists
     */
    bool has_template(const std::string& template_name) const;
    
    /**
     * Get template name by index
     */
    const std::string& get_template_name(size_t index) const;
    
    /**
     * Construct zone graphs for all automata in the system
     */
    void construct_all_zone_graphs();
    
    /**
     * Print statistics for all automata
     */
    void print_all_statistics() const;
    
    /**
     * Print overview of the system
     */
    void print_system_overview() const;
    
    /**
     * Remove automaton by index
     */
    void remove_automaton(size_t index);
    
    /**
     * Remove automaton by template name
     */
    void remove_automaton(const std::string& template_name);
    
    /**
     * Clear all automata
     */
    void clear();
    
private:
    /**
     * Parse UPPAAL document and extract templates
     */
    void build_from_utap_document(UTAP::Document& doc);
    

    
    /**
     * Validate index bounds
     */
    void validate_index(size_t index) const;
};

} // namespace dtpta

#endif // SYSTEM_H
