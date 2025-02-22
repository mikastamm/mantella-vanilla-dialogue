#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "ini.h"
#include "logger.h"

#ifdef _WIN32
    #define STRCASECMP _stricmp
#else
    #define STRCASECMP strcasecmp
#endif

namespace MantellaDialogueIniConfig {

    // Configuration struct
    struct Configuration {
        bool FilterShortReplies;
        int FilterShortRepliesMinWordCount;
        bool FilterNonUniqueGreetings;
        bool DebugLogVanillaDialogue;
        std::vector<std::string> NPCLineBlacklist;
        std::vector<std::string> PlayerLineBlacklist;
        std::vector<std::string> NPCNamesToIgnore;
    };

    // Global configuration variable
    Configuration config;

    // Utility function to trim whitespace from a string
    static std::string trim(const std::string& str) {
        const char* whitespace = " \t\n\r";
        size_t start = str.find_first_not_of(whitespace);
        if (start == std::string::npos) return "";
        size_t end = str.find_last_not_of(whitespace);
        return str.substr(start, end - start + 1);
    }

    // Utility function to split a string by a delimiter **without** trimming
    static std::vector<std::string> split(const std::string& s, char delimiter) {
        std::vector<std::string> tokens;
        std::istringstream tokenStream(s);
        std::string token;
        while (std::getline(tokenStream, token, delimiter)) tokens.push_back(token);  // No trimming here
        return tokens;
    }

    // Utility function to split a string by a delimiter **and** trim each value
    static std::vector<std::string> splitAndTrim(const std::string& s, char delimiter) {
        std::vector<std::string> tokens = split(s, delimiter);
        for (auto& token : tokens) token = trim(token);
        return tokens;
    }

    // INI handler function
    struct IniConfigHelper {
        Configuration* configPtr;
    };

    static int handler(void* user, const char* section, const char* name, const char* value) {
        auto* helper = static_cast<IniConfigHelper*>(user);
        Configuration* config = helper->configPtr;


      
        if (strcmp(name, "FilterShortReplies") == 0)
            config->FilterShortReplies = (STRCASECMP(value, "true") == 0 || strcmp(value, "1") == 0);
        else if (strcmp(name, "FilterShortRepliesMinWordCount") == 0)
            config->FilterShortRepliesMinWordCount = std::max(1, atoi(value));  // Ensure min word count is at least 1
        else if (strcmp(name, "FilterNonUniqueGreetings") == 0)
            config->FilterNonUniqueGreetings = (STRCASECMP(value, "true") == 0 || strcmp(value, "1") == 0);
        else if (strcmp(name, "DebugLogVanillaDialogue") == 0)
            config->DebugLogVanillaDialogue = (STRCASECMP(value, "true") == 0 || strcmp(value, "1") == 0);
        else if (strcmp(name, "NPCLineBlacklist") == 0) {
            auto tokens = splitAndTrim(value, ';');
            if (!tokens.empty()) config->NPCLineBlacklist = tokens;
        } else if (strcmp(name, "PlayerLineBlacklist") == 0) {
            auto tokens = splitAndTrim(value, ';');
            if (!tokens.empty()) config->PlayerLineBlacklist = tokens;
        } else if (strcmp(name, "NPCNamesToIgnore") == 0) {
            auto tokens = splitAndTrim(value, ';');
            if (!tokens.empty()) config->NPCNamesToIgnore = tokens;
        }

        return 1;
    }

    // Function to load configuration from an INI file, falling back to defaults
    void loadConfiguration() {
        // Set default values
        config.FilterShortReplies = true;
        config.FilterShortRepliesMinWordCount = 4;
        config.FilterNonUniqueGreetings = true;
        config.DebugLogVanillaDialogue = false;
        config.NPCLineBlacklist = {"Can I help you?", "Farewell", "See you later"};
        config.PlayerLineBlacklist = {"Stage1Hello", "I want you to..", "Goodbye. (Remove from Mantella conversation)"};
        config.NPCNamesToIgnore = {};

        const std::string filename = "SKSE/Plugins/MantellaDialogue.ini";
        std::ifstream infile(filename);
        if (!infile.good()) {
            logger::error("Failed to open INI file: {}", filename);
            return;
        }
        infile.close();

        IniConfigHelper helper{&config};
        ini_parse(filename.c_str(), handler, &helper);
    }

}  // namespace MantellaDialogueIniConfig
