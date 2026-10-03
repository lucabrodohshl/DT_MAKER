#ifndef CONFIGS_H
#define CONFIGS_H

#include <string>
#include <cstddef>
#include <iostream>

namespace dtpta {

/**
 * Configuration class for DTPTA and Timed Automata settings
 * 
 * This class provides a centralized configuration system that can be:
 * 1. Modified at runtime through code
 * 2. Thread-safe (using singleton pattern)
 * 3. Easily extended with new configuration options
 * 4. Type-safe and well-documented
 */
class Config {
public:
    // =================================================================
    // Timed Automaton Configuration
    // =================================================================
    
    struct TimedAutomatonConfig {
        // Action and transition settings
        std::string tau_action_name = "tau";               // Internal/silent action representation
        std::string empty_action_name = "";                // Alternative representation for internal actions
        
        // Zone graph construction limits
        size_t max_states_default = 60000;                  // Default maximum states in zone graph
        size_t max_states_limit = -1;                  // Hard limit for safety
        int default_initial_location = 0;                  // Default initial location ID
        
        // Synchronization settings
        char sender_suffix = '!';                          // Character marking sender actions
        char receiver_suffix = '?';                        // Character marking receiver actions
        
        // Debug and output settings
        bool enable_debug_output = false;                  // Enable debug prints during construction
        bool enable_warnings = true;                       // Enable warning messages
        bool force_construction = false;                   // Force zone graph reconstruction

        //bool abstract_non_channels = true;            // Abstract away non-channel actions in refinement checks
    };
    
    // =================================================================
    // DTPTA Configuration
    // =================================================================
    
    struct DTPTAConfig {

    };
    
    

private:
    TimedAutomatonConfig timed_automaton_config_;
    DTPTAConfig dtpta_config_;
    
    
    // Singleton pattern
    Config() = default;
    
public:
    // Delete copy constructor and assignment operator
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
    
    /**
     * Get the singleton instance
     */
    static Config& instance() {
        static Config instance;
        return instance;
    }
    
    // =================================================================
    // Accessors for configuration sections
    // =================================================================
    
    TimedAutomatonConfig& timed_automaton() { return timed_automaton_config_; }
    const TimedAutomatonConfig& timed_automaton() const { return timed_automaton_config_; }
    
    DTPTAConfig& dtpta() { return dtpta_config_; }
    const DTPTAConfig& dtpta() const { return dtpta_config_; }
    
    // =================================================================
    // Convenience methods for common configurations
    // =================================================================
    

    
    // =================================================================
    // Utility methods for checking τ-transitions
    // =================================================================
    
    // =================================================================
    // Configuration printing for debugging
    // =================================================================
    
    /**
     * Print current configuration to stdout
     */
    void print_configuration() const {
        std::cout << "=== DTPTA Configuration ===" << std::endl;
        std::cout << "Timed Automaton:" << std::endl;
        //std::cout << "  Default Action: '" << timed_automaton_config_.default_action_name << "'" << std::endl;
        std::cout << "  Tau Action: '" << timed_automaton_config_.tau_action_name << "'" << std::endl;
        std::cout << "  Max States: " << timed_automaton_config_.max_states_default << std::endl;
        std::cout << "  Debug Output: " << (timed_automaton_config_.enable_debug_output ? "ON" : "OFF") << std::endl;

    }
};

// =================================================================
// Global convenience functions
// =================================================================

/**
 * Get reference to the global configuration instance
 */
inline Config& config() {
    return Config::instance();
}

/**
 * Convenience macro for accessing configuration values
 */
#define DTPTA_CONFIG dtpta::config()

/**
 * Convenience macros for specific config sections
 */
#define TA_CONFIG DTPTA_CONFIG.timed_automaton()
#define DTPTA_ALGO_CONFIG DTPTA_CONFIG.dtpta()

} // namespace dtpta

#endif // CONFIGS_H
