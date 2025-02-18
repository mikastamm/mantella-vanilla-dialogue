#include <cstdint>  // For fixed-width integer types
#include <map>      // For std::map
#include <set>      // For std::set
#include <string>   // For std::string
#include <vector>   // For std::vector

#include "MantellaPapyrusInterface.h"
#include "MantellaServerInterface.h"
#include "PCH.h"
#include "json.h"  // Include nlohmann/json library
#include "logger.h"

using json = nlohmann::json;

// Ensure you have included spdlog in your project and initialized it in logger.h/cpp
static void MDebugNotification(const char* a_notification) { RE::DebugNotification(a_notification); }

int port;
MantellaServerInterface serverInterface;

namespace Hooks {

    // -------------------------------------------------------------------------
    // A small POD struct to store a single exchange: player's line, NPC's line,
    // and the Skyrim in-game time at which it was recorded.
    // -------------------------------------------------------------------------
    struct DialogueLine {
        std::string playerLine;
        std::string playerName;
        std::string npcLine;
        std::string npcName;
        float gameTimeHours;
    };

    // Serialize DialogueLine to JSON
    inline void to_json(json& j, const DialogueLine& line) {
        j = json{
            {"playerQuery", line.playerLine}, {"npcResponse", line.npcLine}, {"gameTimeHours", line.gameTimeHours}};
    }

    // Deserialize DialogueLine from JSON
    inline void from_json(const json& j, DialogueLine& line) {
        j.at("playerQuery").get_to(line.playerLine);
        j.at("npcResponse").get_to(line.npcLine);
        j.at("gameTimeHours").get_to(line.gameTimeHours);
    }

    bool IsGreeting(std::string msg) {
        std::string greetings[] = {"Hello", "CYRGenericHello"};
        for (std::string greeting : greetings)
            if (msg == greeting) return true;

        return false;
    }

    // -----------------------------------------------------------------------------
    // Configuration
    // -----------------------------------------------------------------------------
    struct Configuration {
        bool ShouldAddDialogueToMantella;
        bool FilterShortReplies;
        bool FilterNonUniqueGreetings;
        // Filter query-response pairs where response is in this list
        std::vector<std::string> NPCLineBlacklist;
        // Filter query-response pairs where query is in this list
        std::vector<std::string> PlayerLineBlacklist;
    };
    Configuration config;

    void loadConfiguration() {
        // TODO: Load from ini file or settings ui
        config.ShouldAddDialogueToMantella = true;
        config.FilterShortReplies = true;
        config.FilterNonUniqueGreetings = true;
        config.NPCLineBlacklist = {"Can I help you?", "Farewell", "See you later"};
        config.PlayerLineBlacklist = {"Stage1Hello", "I want you to..", "Goodbye. (Remove from Mantella conversation)"};
    }

    // -------------------------------------------------------------------------
    // A simple helper to fetch current game time in hours.
    // -------------------------------------------------------------------------
    static float GetCurrentGameTimeHours() {
        auto calendar = RE::Calendar::GetSingleton();
        return calendar ? calendar->GetHoursPassed() : 0.0f;
    }

        /**
     * @brief Asynchronously sends the player's line, then the NPC's line
     *        to Mantella using the now-synchronous serverInterface methods.
     */
    static void AddDialogueExchangeAsync(const DialogueLine& exchange) {
        logger::info("AddEvent:", exchange.playerLine, exchange.npcLine);       
        MantellaPapyrusInterface::AddMantellaEvent(exchange.playerName + ": "+ exchange.playerLine);    
        MantellaPapyrusInterface::AddMantellaEvent(exchange.npcName + ": " + exchange.npcLine);
        /*    // We still launch a separate thread to avoid blocking the game thread,
        // but now we call Mantella synchronously inside that thread.
        std::async(std::launch::async, [exchange]() {
            try {
                // Send the first message (playerLine) and get the response
                cpr::Response response1 =
                    serverInterface.AddMessageToMantella(exchange.playerLine, exchange.playerName);
                if (response1.status_code != 200) {
                    MDebugNotification("Failed to send player message to Mantella");
                    logger::error("Failed to send player message to Mantella: {}", response1.status_code); 
                    return;  // Exit if the first message failed
                }

                // Send the second message (npcLine) after the first has succeeded
                cpr::Response response2 = serverInterface.AddMessageToMantella(exchange.npcLine, exchange.playerName);
                if (response2.status_code != 200) {
                    MDebugNotification("Failed to send NPC message to Mantella");
                    logger::error("Failed to send NPC message to Mantella: {}", response2.status_code);
                }

            } catch (const std::exception& e) {
                // Handle any exceptions thrown during the asynchronous operations
                logger::error("Exception in AddDialogueExchangeAsync: {}", e.what());
            }
        });*/ 
    }

    // -------------------------------------------------------------------------
    // MantellaDialogueTracker:
    // - holds the participants list form
    // - tracks if there's an internal error
    // - stores unsent dialogue lines in a map keyed by Actor form ID
    // -------------------------------------------------------------------------
    struct MantellaDialogueTracker {
        static inline RE::BGSListForm* aParticipants;
        static inline bool DialogueTrackerHasError = false;

        // Key = actor form ID, value = list of dialogue lines not yet added to Mantella
        static inline std::map<RE::FormID, std::vector<DialogueLine>> s_dialogueHistory{};

        // We use std::set instead of std::unordered_set
        static inline std::set<RE::FormID> s_lastParticipants{};

        static void Setup() {
            auto dataHandler = RE::TESDataHandler::GetSingleton();
            if (!dataHandler) {
                logger::error("MantellaDialogueTracker::Setup: TESDataHandler is null!");
                DialogueTrackerHasError = true;
                return;
            }
            aParticipants = dataHandler->LookupForm<RE::BGSListForm>(0xE4537, "Mantella.esp");
            if (!aParticipants) {
                logger::error("MantellaDialogueTracker::Setup: aParticipants is null!");
                DialogueTrackerHasError = true;
                return;
            }
        }

        // ---------------------------------------------------------------------
        // Helper: check if the conversation is considered "running" by looking
        // at the participants list. Adjust this logic to your needs.
        // ---------------------------------------------------------------------
        static bool IsConversationRunning() {
            if (!aParticipants || DialogueTrackerHasError) return false;

            auto scriptAddedForms = aParticipants->scriptAddedTempForms;
            if (!scriptAddedForms) return false;

            return !scriptAddedForms->empty();
        }

        // ---------------------------------------------------------------------
        // Helper: checks if a given actor is listed in the current participants
        // form list.
        // ---------------------------------------------------------------------
        static bool IsActorInConversation(RE::Actor* a_actor) {
            if (!a_actor || !aParticipants || DialogueTrackerHasError) return false;

            auto formID = a_actor->GetFormID();
            auto scriptAddedForms = aParticipants->scriptAddedTempForms;
            if (!scriptAddedForms) return false;

            for (auto& actorInConvFormId : *scriptAddedForms) {
                if (actorInConvFormId == formID) return true;
            }
            return false;
        }

        // ---------------------------------------------------------------------
        // Called when a conversation starts.
        // ---------------------------------------------------------------------
        static void OnConversationStarted() {
            if (DialogueTrackerHasError || !aParticipants) return;

            // Clear previous participants
            s_lastParticipants.clear();

            // For each form in the participants, replay old lines if any
            auto scriptAddedForms = aParticipants->scriptAddedTempForms;
            if (!scriptAddedForms) return;

            for (auto& formID : *scriptAddedForms) {
                s_lastParticipants.insert(formID);

                // See if we have stored lines for this participant
                auto it = s_dialogueHistory.find(formID);
                if (it == s_dialogueHistory.end()) continue;

                SendAndDiscardCapturedDialogue(it);
            }
        }

        // Sends the dialogue that was captured when not in a conversation to Mantella and removes it from the backlog
        static void SendAndDiscardCapturedDialogue(
            std::map<RE::FormID, std::vector<DialogueLine>>::iterator dialogueLineIterator) {
            if (dialogueLineIterator == s_dialogueHistory.end()) {
                logger::warn("SendAndDiscardCapturedDialogue: Invalid iterator provided.");
                return;
            }

            // Concatenate all lines into a single string
            std::string concatenatedLines;
            for (auto& line : dialogueLineIterator->second) {
                // Concatenate the player query and NPC response separated by a semicolon
                concatenatedLines += line.playerLine + "; " + line.npcLine + " ";
            }

            // Remove the trailing space, if any
            if (!concatenatedLines.empty() && concatenatedLines.back() == ' ') concatenatedLines.pop_back();

            // Send a single Mantella event with the concatenated lines
            if (!concatenatedLines.empty()) {
                AddDialogueExchangeAsync(dialogueLineIterator->second[0]);
                logger::info("Sent MantellaAddEvent with concatenated dialogue.");
            }

            // Erase the processed entry from dialogue history
            s_dialogueHistory.erase(dialogueLineIterator);
            MDebugNotification("Actor had captured dialogue. Sent it to mantella");
            logger::info("SendAndDiscardCapturedDialogue: Removed processed dialogue from history.");
        }

        // ---------------------------------------------------------------------
        // Called if a new participant joins mid-conversation.
        // ---------------------------------------------------------------------
        static void OnNewParticipant(RE::Actor* a_newActor) {
            if (!a_newActor || DialogueTrackerHasError) return;

            auto newFormID = a_newActor->GetFormID();
            auto it = s_dialogueHistory.find(newFormID);
            if (it == s_dialogueHistory.end()) return;

            SendAndDiscardCapturedDialogue(it);
        }
    };

    // -------------------------------------------------------------------------
    // ShowSubtitle hook:
    // - We capture the player's topic text + NPC response
    // - We store them in s_dialogueHistory if conversation not running
    // - Or we add them to Mantella event queue if conversation is running
    // -------------------------------------------------------------------------
    struct ShowSubtitle {
        inline static std::string s_lastPlayerTopicText{};

        static bool HasAlreadyProcessed(std::string_view a_topicText) { return a_topicText == s_lastPlayerTopicText; }

        static void UpdateLastPlayerTopicText(std::string_view a_topicText) {
            s_lastPlayerTopicText = std::string(a_topicText);
        }

    

        static bool ShouldFilterDialoge(std::string playerLine, std::string npcLine, RE::TESTopicInfo* topicInfo) {
            // Skip if we've processed this text before
            if (HasAlreadyProcessed(playerLine)) return true;

            // Filter if player line is in the config blacklist
            if (std::find(config.PlayerLineBlacklist.begin(), config.PlayerLineBlacklist.end(), playerLine) !=
                config.PlayerLineBlacklist.end()) {
                return true;
            }

            // Filter if NPC line is in the config blacklist
            if (std::find(config.NPCLineBlacklist.begin(), config.NPCLineBlacklist.end(), npcLine) !=
                config.NPCLineBlacklist.end()) {
                return true;
            }

            if (topicInfo != nullptr && config.FilterNonUniqueGreetings && IsGreeting(playerLine) &&
                !(topicInfo->data.flags & RE::TOPIC_INFO_DATA::TOPIC_INFO_FLAGS::kSayOnce)) {
                // Dont send greetings to mantella to prevent spamming the model with generic lines
                MDebugNotification("Filtered Greeting");
                return true;
            }
            if (topicInfo == nullptr && config.FilterNonUniqueGreetings && IsGreeting(playerLine)) {
                logger::error("TopicInfo is null, cannot filter greeting");
            }

            return false;
        }

        static RE::MenuTopicManager::Dialogue* GetDialogue() {
            auto topicManager = RE::MenuTopicManager::GetSingleton();
            if (!topicManager) {
                logger::error("ShowSubtitle::thunk: MenuTopicManager is null!");
                return nullptr;
            }
            return topicManager->lastSelectedDialogue;
        }

        // The hook function
        static void thunk(RE::SubtitleManager* a_this, RE::TESObjectREFR* a_speaker, const char* a_subtitle,
                          bool a_alwaysDisplay) {
            // Call original
            func(a_this, a_speaker, a_subtitle, a_alwaysDisplay);

            if (!config.ShouldAddDialogueToMantella) return;
            if (!a_speaker) return;

            RE::MenuTopicManager::Dialogue* dialogue = GetDialogue();
            if (dialogue == nullptr) return;

            const std::string currentPlayerTopicText = dialogue->topicText.c_str();
            if (currentPlayerTopicText.empty()) {
                logger::warn("ShowSubtitle::thunk: currentPlayerTopicText is empty!");
                return;
            }

            // Build the NPC's response
            std::string npcLine;
            for (auto* response : dialogue->responses) {
                if (response && !response->text.empty()) {
                    if (!npcLine.empty()) npcLine += " ";
                    npcLine += response->text.c_str();
                }
            }

            if (ShouldFilterDialoge(currentPlayerTopicText, npcLine, dialogue->parentTopicInfo)) return;

            auto actor = skyrim_cast<RE::Actor*>(a_speaker);
            if (!actor) {
                logger::error("ShowSubtitle::thunk: a_speaker is empty or not an actor!");
                return;
            }

            // Get the player's name
            auto player = RE::PlayerCharacter::GetSingleton();
            const char* playerName = "Player";
            if (player && player->GetActorBase()) {
                if (auto name = player->GetActorBase()->GetName(); name && name[0] != '\0') playerName = name;
            }

            auto exchange = DialogueLine();
            exchange.playerLine = currentPlayerTopicText;
            exchange.playerName = playerName;
            exchange.npcLine = npcLine;
            exchange.npcName = actor->GetDisplayFullName();
            exchange.gameTimeHours = GetCurrentGameTimeHours();

            // Are we in a conversation?
            bool conversationRunning = MantellaDialogueTracker::IsConversationRunning();
            bool actorInConversation = MantellaDialogueTracker::IsActorInConversation(actor);

            // If conversation not running, store for later
            if (!conversationRunning) {
                if (!MantellaDialogueTracker::DialogueTrackerHasError) {
                    auto formID = actor->GetFormID();
                    MantellaDialogueTracker::s_dialogueHistory[formID].push_back(exchange);
                    logger::info("Stored dialogue for FormID {} in s_dialogueHistory.", formID);
                    MDebugNotification("No Conv, Stored line");
                } else {
                    MDebugNotification("Mantella Dialoge Error");
                }
            } else {
                // If conversation is running
                if (actorInConversation) {
                    // If the speaker is a participant, send lines to Mantella
                    AddDialogueExchangeAsync(exchange);
                } else {
                    // Special case: speaker is not in participants,
                    // but conversation is running with others.
                    // Broadcast to all participants + store for possible future use
                    AddDialogueExchangeAsync(exchange);

                    if (!MantellaDialogueTracker::DialogueTrackerHasError) {
                        auto formID = actor->GetFormID();
                        MantellaDialogueTracker::s_dialogueHistory[formID].push_back(exchange);
                        logger::info("Stored dialogue for FormID {} in s_dialogueHistory.", formID);
                    }
                }
            }

            // Mark this topic as processed
            UpdateLastPlayerTopicText(currentPlayerTopicText);
        }

        // Original function pointer
        static inline REL::Relocation<decltype(thunk)> func;

        // Install
        static void Install() {
            std::array targets{
                std::make_pair(RELOCATION_ID(19119, 19521), 0x2B2),
                std::make_pair(RELOCATION_ID(36543, 37544), OFFSET(0x8EC, 0x8C2)),
            };

            for (auto& [id, offset] : targets) {
                REL::Relocation<std::uintptr_t> target(id, offset);
                stl::write_thunk_call<ShowSubtitle>(target.address());
            }
        }
    };

}  // namespace Hooks

#pragma region Serialization
// -----------------------------------------------------------------------------
// JSON Serialization and Deserialization Functions
// -----------------------------------------------------------------------------

// Serialize dialogue history to JSON string
std::string SerializeDialogueHistoryToJSON() {
    json j;

    for (const auto& [formID, dialogueLines] : Hooks::MantellaDialogueTracker::s_dialogueHistory) {
        j[std::to_string(formID)] = dialogueLines;
    }

    return j.dump();  // Convert JSON object to string
}

// Deserialize dialogue history from JSON string
bool DeserializeDialogueHistoryFromJSON(const std::string& jsonString) {
    try {
        json j = json::parse(jsonString);
        Hooks::MantellaDialogueTracker::s_dialogueHistory.clear();

        for (auto it = j.begin(); it != j.end(); ++it) {
            RE::FormID formID = static_cast<RE::FormID>(std::stoul(it.key()));
            std::vector<Hooks::DialogueLine> dialogueLines = it.value().get<std::vector<Hooks::DialogueLine>>();
            Hooks::MantellaDialogueTracker::s_dialogueHistory.emplace(formID, std::move(dialogueLines));
        }
        MDebugNotification(("Loaded " + std::to_string(Hooks::MantellaDialogueTracker::s_dialogueHistory.size()) +
                            " actors with pending lines")
                               .c_str());

        logger::info("Deserialized dialogue history with {} entries.",
                     Hooks::MantellaDialogueTracker::s_dialogueHistory.size());
        return true;
    } catch (const json::parse_error& e) {
        logger::error("Failed to parse dialogue history JSON: {}", e.what());
        return false;
    } catch (const std::exception& e) {
        logger::error("Exception during deserialization: {}", e.what());
        return false;
    }
}

// -----------------------------------------------------------------------------
// SKSE Serialization Callbacks
// -----------------------------------------------------------------------------

constexpr std::uint32_t kSerializationID = 'MTDL';  // 4-byte unique plugin signature

// Save Callback
void MySaveCallback(SKSE::SerializationInterface* a_intfc) {
    // Serialize dialogue history to JSON string
    std::string jsonString = SerializeDialogueHistoryToJSON();

    // Open a record with type 'HIST' and version 1
    constexpr std::uint32_t recordType = 'HIST';  // Unique 4-byte identifier for dialogue history
    constexpr std::uint32_t version = 1;

    if (!a_intfc->OpenRecord(recordType, version)) {
        logger::error("MySaveCallback: Failed to open 'HIST' record for serialization.");
        return;
    }

    // Write the length of the JSON string as a 32-bit unsigned integer
    std::uint32_t jsonLength = static_cast<std::uint32_t>(jsonString.size());
    if (!a_intfc->WriteRecordData(&jsonLength, sizeof(jsonLength))) {
        logger::error("MySaveCallback: Failed to write JSON string length.");
        return;
    }

    // Write the JSON string data
    if (!a_intfc->WriteRecordData(jsonString.c_str(), jsonLength)) {
        logger::error("MySaveCallback: Failed to write JSON string data.");
        return;
    }

    logger::info("MySaveCallback: Serialized dialogue history to SKSE co-save.");
}

// Load Callback
void MyLoadCallback(SKSE::SerializationInterface* a_intfc) {
    std::uint32_t type, version, length;
    while (a_intfc->GetNextRecordInfo(type, version, length)) {
        if (type != 'HIST') {
            continue;
        }

        // Read the length of the JSON string
        std::uint32_t jsonLength = 0;
        if (a_intfc->ReadRecordData(&jsonLength, sizeof(jsonLength)) != sizeof(jsonLength)) {
            logger::error("MyLoadCallback: Failed to read JSON string length.");
            continue;
        }

        // Read the JSON string data
        std::string jsonString(jsonLength, '\0');
        if (a_intfc->ReadRecordData(&jsonString[0], jsonLength) != jsonLength) {
            logger::error("MyLoadCallback: Failed to read JSON string data.");
            continue;
        }

        // Deserialize JSON string into dialogue history
        if (DeserializeDialogueHistoryFromJSON(jsonString)) {
            logger::info("MyLoadCallback: Successfully loaded dialogue history from SKSE co-save.");
        } else {
            logger::error("MyLoadCallback: Failed to deserialize dialogue history from JSON.");
        }
    }
}

// Revert Callback
void MyRevertCallback(SKSE::SerializationInterface*) {
    Hooks::MantellaDialogueTracker::s_dialogueHistory.clear();
    logger::info("MyRevertCallback: Cleared dialogue history.");
}
#pragma endregion

// -----------------------------------------------------------------------------
// SKSE Messaging Interface Listener
// -----------------------------------------------------------------------------

void OnSKSEMessage(SKSE::MessagingInterface::Message* a_msg) {
    if (a_msg->type == SKSE::MessagingInterface::kDataLoaded) {
        // Setup the Mantella tracker
        Hooks::MantellaDialogueTracker::Setup();
    }
    if (a_msg->type == SKSE::MessagingInterface::kPostLoad) {
        // Register your plugin's serialization callbacks
        auto serialization = SKSE::GetSerializationInterface();
        if (!serialization) {
            logger::error("OnSKSEMessage: Failed to get SKSE Serialization Interface.");
            return;
        }

        serialization->SetUniqueID(kSerializationID);  // 'MTDL'
        serialization->SetSaveCallback(MySaveCallback);
        serialization->SetLoadCallback(MyLoadCallback);
        serialization->SetRevertCallback(MyRevertCallback);

        logger::info("OnSKSEMessage: Registered SKSE Serialization callbacks for dialogue history.");
    }
}

void notifyConversationStart(RE::StaticFunctionTag*) { Hooks::MantellaDialogueTracker::OnConversationStarted(); }

void notifyActorAdded(RE::StaticFunctionTag*, std::vector<RE::TESForm*> actors) {
    for (auto actorForm : actors) {
        auto actor = skyrim_cast<RE::Actor*>(actorForm);
        if (actor) Hooks::MantellaDialogueTracker::OnNewParticipant(actor);
    }
}

void notifyActorRemoved(RE::StaticFunctionTag*, std::vector<RE::TESForm*> actors) {
    // Currently a no-op
}

void notifyConversationEnd(RE::StaticFunctionTag*) {
    // Placeholder: Clear the last participants list
    Hooks::MantellaDialogueTracker::s_lastParticipants.clear();
}

bool Bind(RE::BSScript::IVirtualMachine* vm) {
    std::string classname = "MantellaVanillaDialogue";
    vm->RegisterFunction("notifyConversationStart", classname, notifyConversationStart);
    vm->RegisterFunction("notifyNpcAdded", classname, notifyActorAdded);
    vm->RegisterFunction("notifyNpcRemoved", classname, notifyActorRemoved);
    vm->RegisterFunction("notifyConversationEnd", classname, notifyConversationEnd);
    return true;
}

void SetupServerInterface() {
    port = MantellaPapyrusInterface::GetMantellaServerPort();
    if (port == -1) {
        logger::error("Failed to get Mantella server port from MCM.");
        port = 4999;
        return;
    }

    // Use the updated MantellaServerInterface fields
    serverInterface.port = port;
    serverInterface.timeoutMs = 3000;  // or your desired default
}

// Typical SKSE entry point
SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);
    SKSE::GetPapyrusInterface()->Register(Bind);
    SetupLog();
    SetupServerInterface();
    Hooks::loadConfiguration();

    if (auto messaging = SKSE::GetMessagingInterface()) {
        messaging->RegisterListener("SKSE", OnSKSEMessage);
        logger::info("SKSEPluginLoad: Registered SKSE messaging listener.");
    } else {
        logger::error("SKSEPluginLoad: Failed to get SKSE Messaging Interface!");
    }
    // Hook subtitles
    Hooks::ShowSubtitle::Install();
    logger::info("SKSEPluginLoad: Installed ShowSubtitle hook.");

    return true;
}
