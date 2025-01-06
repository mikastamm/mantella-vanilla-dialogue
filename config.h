// DialogueFilterConfig.h
#pragma once

#include <algorithm>
#include <cctype>
#include <fstream>
#include <mutex>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

// Forward declaration of the logger (assuming it's in logger.h)
#include "logger.h"

class DialogueFilterConfig {
public:
    // Singleton Accessor
    static DialogueFilterConfig& GetInstance() {
        static DialogueFilterConfig instance;
        return instance;
    }

    // Delete copy constructor and assignment operator to enforce singleton
    DialogueFilterConfig(const DialogueFilterConfig&) = delete;
    DialogueFilterConfig& operator=(const DialogueFilterConfig&) = delete;

    // Load the INI configuration from the specified file path
    bool LoadConfig(const std::string& filepath) {
        std::lock_guard<std::mutex> lock(configMutex_);
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        std::ifstream iniFile(filepath);
        if (!iniFile.is_open()) {
            logger::error("DialogueFilterConfig: Failed to open INI file: {}", filepath);
            return false;
        }

        std::string line;
        std::string currentSection;
        while (std::getline(iniFile, line)) {
            // Trim whitespace
            line = Trim(line);

            // Skip empty lines and comments
            if (line.empty() || line[0] == ';' || line[0] == '#') continue;

            // Check for section headers
            if (line.front() == '[' && line.back() == ']') {
                currentSection = line.substr(1, line.size() - 2);
                continue;
            }

            // Process key=value lines within the desired section
            if (currentSection == "MantellaDialogueFilter") {
                auto delimiterPos = line.find('=');
                if (delimiterPos == std::string::npos) {
                    logger::warn("DialogueFilterConfig: Invalid line in INI file: {}", line);
                    continue;
                }

                std::string key = Trim(line.substr(0, delimiterPos));
                std::string value = Trim(line.substr(delimiterPos + 1));

                // Split the comma-separated regex patterns
                std::vector<std::string> patterns = SplitString(value, ',');

                // Depending on the key, add to the appropriate regex list
                if (key == "PlayerLineExcludeRegex") {
                    AddRegexPatterns(patterns, playerLineExcludeRegex_, key);
                } else if (key == "NPCLineBlacklistRegex") {
                    AddRegexPatterns(patterns, npcLineBlacklistRegex_, key);
                } else if (key == "PlayerLineBlacklistRegex") {
                    AddRegexPatterns(patterns, playerLineBlacklistRegex_, key);
                } else {
                    logger::warn("DialogueFilterConfig: Unknown key [{}] in section [{}]", key, currentSection);
                }
            }
        }

        iniFile.close();
        logger::info("DialogueFilterConfig: Successfully loaded configuration from {}", filepath);
        return true;
    }

    // Accessor methods to retrieve the regex lists
    const std::vector<std::regex>& GetPlayerLineExcludeRegex() const { return playerLineExcludeRegex_; }

    const std::vector<std::regex>& GetNPCLineBlacklistRegex() const { return npcLineBlacklistRegex_; }

    const std::vector<std::regex>& GetPlayerLineBlacklistRegex() const { return playerLineBlacklistRegex_; }

    // Evaluates if any regex in the list matches the input text
    bool MatchAny(const std::vector<std::regex>& regexList, const std::string& text) const {
        for (const auto& regexPattern : regexList) {
            if (std::regex_search(text, regexPattern)) {
                logger::info("DialogueFilterConfig::MatchAny: Text '{}' matched pattern '{}'", text,
                             regexPattern.pattern());
                return true;
            }
        }
        return false;
    }

private:
    // Private constructor for singleton pattern
    DialogueFilterConfig() = default;

    // Helper Methods

    // Trim whitespace from both ends of a string
    static std::string Trim(const std::string& s) {
        size_t start = s.find_first_not_of(" \t\r\n");
        size_t end = s.find_last_not_of(" \t\r\n");
        if (start == std::string::npos || end == std::string::npos) return "";
        return s.substr(start, end - start + 1);
    }

    // Split a string by a delimiter and return a vector of trimmed substrings
    static std::vector<std::string> SplitString(const std::string& str, char delimiter) {
        std::vector<std::string> tokens;
        std::stringstream ss(str);
        std::string token;
        while (std::getline(ss, token, delimiter)) {
            token = Trim(token);
            if (!token.empty()) tokens.push_back(token);
        }
        return tokens;
    }

    // Add regex patterns to a given vector, compiling them and handling errors
    void AddRegexPatterns(const std::vector<std::string>& patterns, std::vector<std::regex>& regexList,
                          const std::string& key) {
        for (const auto& pattern : patterns) {
            try {
                regexList.emplace_back(pattern, std::regex::ECMAScript | std::regex::icase);
                logger::info("DialogueFilterConfig: Loaded regex pattern for [{}]: {}", key, pattern);
            } catch (const std::regex_error& e) {
                logger::error("DialogueFilterConfig: Invalid regex pattern '{}' for [{}]: {}", pattern, key, e.what());
                // Continue loading other patterns
            }
        }
    }

    // Regex lists
    std::vector<std::regex> playerLineExcludeRegex_;
    std::vector<std::regex> npcLineBlacklistRegex_;
    std::vector<std::regex> playerLineBlacklistRegex_;

    // Mutex for thread-safe operations
    mutable std::mutex configMutex_;
};
