#include <cstdint>  // For fixed-width integer types
#include <map>      // For std::map
#include <set>      // For std::set
#include <string>   // For std::string
#include <vector>   // For std::vector

#include "PCH.h"
#include "json.h"  // Include nlohmann/json library
#include "logger.h"
using json = nlohmann::json;

// Ensure you have included spdlog in your project and initialized it in logger.h/cpp

namespace Hooks {
    // -------------------------------------------------------------------------
    // A small POD struct to store a single exchange: player's line, NPC's line,
    // and the Skyrim in-game time at which it was recorded.
    // -------------------------------------------------------------------------
    struct DialogueLine {
        std::string playerQuery;
        std::string npcResponse;
        float gameTimeHours;  // e.g., from RE::Calendar::GetSingleton()->GetHoursPassed()
    };

    // Serialize DialogueLine to JSON
    inline void to_json(json& j, const DialogueLine& line) {
        j = json{{"playerQuery", line.playerQuery},
                 {"npcResponse", line.npcResponse},
                 {"gameTimeHours", line.gameTimeHours}};
    }

    // Deserialize DialogueLine from JSON
    inline void from_json(const json& j, DialogueLine& line) {
        j.at("playerQuery").get_to(line.playerQuery);
        j.at("npcResponse").get_to(line.npcResponse);
        j.at("gameTimeHours").get_to(line.gameTimeHours);
    }

    // -------------------------------------------------------------------------
    // A simple helper to fetch current game time in hours.
    // -------------------------------------------------------------------------
    static float GetCurrentGameTimeHours() {
        auto calendar = RE::Calendar::GetSingleton();
        return calendar ? calendar->GetHoursPassed() : 0.0f;
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
        // form list. If your mod updates `aParticipants` over time, we compare
        // each form’s GetFormID().
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
                concatenatedLines += line.playerQuery + "; " + line.npcResponse + " ";
            }

            // Remove the trailing space, if any
            if (!concatenatedLines.empty() && concatenatedLines.back() == ' ') {
                concatenatedLines.pop_back();
            }

            // Send a single Mantella event with the concatenated lines
            if (!concatenatedLines.empty()) {
                SKSE::ModCallbackEvent modEvent{"MantellaAddEvent", concatenatedLines.c_str()};
                if (auto source = SKSE::GetModCallbackEventSource(); source) {
                    source->SendEvent(&modEvent);
                    logger::info("Sent MantellaAddEvent with concatenated dialogue.");
                } else {
                    logger::error("SendAndDiscardCapturedDialogue: ModCallbackEventSource is null!");
                }
            }

            // Erase the processed entry from dialogue history
            s_dialogueHistory.erase(dialogueLineIterator);
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
    // - This example also calls OnConversationStarted() each time (placeholder!)
    // -------------------------------------------------------------------------
    struct ShowSubtitle {
        inline static std::string s_lastPlayerTopicText{};

        static bool HasAlreadyProcessed(std::string_view a_topicText) { return a_topicText == s_lastPlayerTopicText; }

        static void UpdateLastPlayerTopicText(std::string_view a_topicText) {
            s_lastPlayerTopicText = std::string(a_topicText);
        }

        // Fire an event to Mantella
        static void AddMantellaEvent(const char* a_event) {
            SKSE::ModCallbackEvent modEvent{"MantellaAddEvent", a_event};
            if (auto modCallbackSource = SKSE::GetModCallbackEventSource(); modCallbackSource)
                modCallbackSource->SendEvent(&modEvent);
            else
                RE::DebugNotification("AddMantellaEvent: No ModCallbackEventSource found!");
        }

        // The hook function
        static void thunk(RE::SubtitleManager* a_this, RE::TESObjectREFR* a_speaker, const char* a_subtitle,
                          bool a_alwaysDisplay) {
            // Call original
            func(a_this, a_speaker, a_subtitle, a_alwaysDisplay);

            if (!a_speaker) return;

            auto topicManager = RE::MenuTopicManager::GetSingleton();
            if (!topicManager) {
                logger::error("ShowSubtitle::thunk: MenuTopicManager is null!");
                return;
            }

            auto dialogue = topicManager->lastSelectedDialogue;
            if (!dialogue) return;

            const std::string currentPlayerTopicText = dialogue->topicText.c_str();
            if (currentPlayerTopicText.empty()) {
                RE::DebugNotification("ShowSubtitle::thunk: No player topic text found!");
                return;
            }

            // Skip if we've processed this text before
            if (HasAlreadyProcessed(currentPlayerTopicText)) return;

            // Build the NPC's response
            std::string npcResponse;
            for (auto* response : dialogue->responses) {
                if (response && !response->text.empty()) {
                    if (!npcResponse.empty()) npcResponse += " ";
                    npcResponse += response->text.c_str();
                }
            }

            auto actor = skyrim_cast<RE::Actor*>(a_speaker);
            if (!actor) {
                RE::DebugNotification("ShowSubtitle::thunk: a_speaker is not an Actor!");
                return;
            }

            // Get the player's name
            auto player = RE::PlayerCharacter::GetSingleton();
            const char* playerName = "Player";
            if (player && player->GetActorBase()) {
                if (auto name = player->GetActorBase()->GetName(); name && name[0] != '\0') playerName = name;
            }

            std::string playerEvent = std::string(playerName) + ": " + currentPlayerTopicText;
            std::string npcEvent = "";
            if (!npcResponse.empty()) npcEvent = std::string(actor->GetName()) + ": " + npcResponse;

            // Placeholder: Force "OnConversationStarted" each time a new line is processed.
            MantellaDialogueTracker::OnConversationStarted();

            // Are we in a conversation?
            bool conversationRunning = MantellaDialogueTracker::IsConversationRunning();
            bool actorInConversation = MantellaDialogueTracker::IsActorInConversation(actor);

            // Prepare a DialogueLine entry
            DialogueLine newLine;
            newLine.playerQuery = playerEvent;
            newLine.npcResponse = npcEvent;
            newLine.gameTimeHours = GetCurrentGameTimeHours();

            // If conversation not running, store for later
            if (!conversationRunning) {
                if (!MantellaDialogueTracker::DialogueTrackerHasError) {
                    auto formID = actor->GetFormID();
                    MantellaDialogueTracker::s_dialogueHistory[formID].push_back(newLine);
                    logger::info("Stored dialogue for FormID %u in s_dialogueHistory.", formID);
                }
                // Fire events even if conversation not running
                AddMantellaEvent(playerEvent.c_str());
                if (!npcEvent.empty()) AddMantellaEvent(npcEvent.c_str());

            } else {
                // If conversation is running
                if (actorInConversation) {
                    // If the speaker is a participant, send lines to Mantella
                    AddMantellaEvent(playerEvent.c_str());
                    if (!npcEvent.empty()) AddMantellaEvent(npcEvent.c_str());

                } else {
                    // Special case: speaker is not in participants,
                    // but conversation is running with others.
                    // Broadcast to all participants + store for possible future use
                    AddMantellaEvent(playerEvent.c_str());
                    if (!npcEvent.empty()) AddMantellaEvent(npcEvent.c_str());

                    if (!MantellaDialogueTracker::DialogueTrackerHasError) {
                        auto formID = actor->GetFormID();
                        MantellaDialogueTracker::s_dialogueHistory[formID].push_back(newLine);
                        logger::info("Stored dialogue for FormID %u in s_dialogueHistory.", formID);
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
                logger::info("Installed ShowSubtitle hook at address: {:x}", target.address());
            }
        }
    };

}  // namespace Hooks

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

        logger::info("Deserialized dialogue history with %zu entries.",
                     Hooks::MantellaDialogueTracker::s_dialogueHistory.size());
        return true;
    } catch (const json::parse_error& e) {
        logger::error("Failed to parse dialogue history JSON: %s", e.what());
        return false;
    } catch (const std::exception& e) {
        logger::error("Exception during deserialization: %s", e.what());
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


// -----------------------------------------------------------------------------
// Process and Send Dialogue Function
// -----------------------------------------------------------------------------

void SendAndDiscardCapturedDialogue(std::map<RE::FormID, std::vector<Hooks::DialogueLine>>::iterator it) {
    if (it == Hooks::MantellaDialogueTracker::s_dialogueHistory.end()) {
        logger::warn("SendAndDiscardCapturedDialogue: Invalid iterator provided.");
        return;
    }

    // Concatenate all lines into a single string
    std::string concatenatedLines;

    for (auto& line : it->second) {
        // Concatenate the player query and NPC response separated by a semicolon
        concatenatedLines += line.playerQuery + "; " + line.npcResponse + " ";
    }

    // Remove the trailing space, if any
    if (!concatenatedLines.empty() && concatenatedLines.back() == ' ') {
        concatenatedLines.pop_back();
    }

    // Send a single Mantella event with the concatenated lines
    if (!concatenatedLines.empty()) {
        SKSE::ModCallbackEvent modEvent{"MantellaAddEvent", concatenatedLines.c_str()};
        if (auto source = SKSE::GetModCallbackEventSource(); source) {
            source->SendEvent(&modEvent);
            logger::info("Sent MantellaAddEvent with concatenated dialogue.");
        } else {
            logger::error("SendAndDiscardCapturedDialogue: ModCallbackEventSource is null!");
        }
    }

    // Erase the processed entry from dialogue history
    Hooks::MantellaDialogueTracker::s_dialogueHistory.erase(it);
    logger::info("SendAndDiscardCapturedDialogue: Removed processed dialogue from history.");
}

// Typical SKSE entry point
SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);
    SetupLog();

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
