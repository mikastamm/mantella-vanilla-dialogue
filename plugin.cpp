#include <cstdint>  // For fixed-width integer types
#include <map>      // For std::map
#include <set>      // For std::set
#include <string>   // For std::string
#include <vector>   // For std::vector

#include "MantellaDialogueIniConfig.h"
#include "MantellaPapyrusInterface.h"
#include "PCH.h"
#include "json.h"  // Include nlohmann/json library
#include "logger.h"

using json = nlohmann::json;

int port;

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
        // at the participants list.
        // ---------------------------------------------------------------------
        static bool IsConversationRunning() {
            if (!aParticipants || DialogueTrackerHasError) return false;
            auto scriptAddedForms = aParticipants->scriptAddedTempForms;
            if (!scriptAddedForms) return false;
            return !scriptAddedForms->empty();
        }

        // ---------------------------------------------------------------------
        // Helper: checks if a given actor is listed in the current participants form list.
        // ---------------------------------------------------------------------
        static bool IsActorInConversation(RE::Actor* a_actor) {
            if (!a_actor || !aParticipants || DialogueTrackerHasError) return false;
            auto formID = a_actor->GetFormID();
            auto scriptAddedForms = aParticipants->scriptAddedTempForms;
            if (!scriptAddedForms) return false;
            for (auto& actorInConvFormId : *scriptAddedForms)
                if (actorInConvFormId == formID) return true;
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
            // Log the NPC name if available
            if (!dialogueLineIterator->second.empty())
                logger::debug("SendAndDiscardCapturedDialogue: Sending dialogue for NPC '%s'",
                              dialogueLineIterator->second.front().npcName.c_str());
            // Concatenate all lines into a single string
            std::string concatenatedLines;
            for (auto& line : dialogueLineIterator->second)
                concatenatedLines +=
                    line.playerName + ": " + line.playerLine + ";\n " + line.npcName + ": " + line.npcLine + " ";
            // Remove the trailing space, if any
            if (!concatenatedLines.empty() && concatenatedLines.back() == ' ') concatenatedLines.pop_back();
            // Send a single Mantella event with the concatenated lines
            if (!concatenatedLines.empty()) MantellaPapyrusInterface::AddMantellaEvent(concatenatedLines.c_str());
            // Erase the processed entry from dialogue history
            s_dialogueHistory.erase(dialogueLineIterator);
            logger::debug("Actor had captured dialogue. Sent it to mantella");
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
    // -------------------------------------------------------------------------
    struct ShowSubtitle {
        inline static std::string s_lastPlayerTopicText{};

        static bool HasAlreadyProcessed(std::string_view a_topicText) { return a_topicText == s_lastPlayerTopicText; }

        static void UpdateLastPlayerTopicText(std::string_view a_topicText) {
            s_lastPlayerTopicText = std::string(a_topicText);
        }

        static void AddDialogueExchangeAsync(const DialogueLine& exchange) {
            MantellaPapyrusInterface::AddMantellaEvent(exchange.playerName + ": " + exchange.playerLine + "; " +
                                                       exchange.npcName + ": " + exchange.npcLine);
        }

        static bool ShouldFilterDialoge(std::string playerLine, std::string npcLine, RE::TESTopicInfo* topicInfo) {
            if (HasAlreadyProcessed(playerLine)) return true;
            if (std::find(MantellaDialogueIniConfig::config.PlayerLineBlacklist.begin(),
                MantellaDialogueIniConfig::config.PlayerLineBlacklist.end(),
                playerLine) != MantellaDialogueIniConfig::config.PlayerLineBlacklist.end())
            {
                logger::debug(" -> Filtered: Player Line Blacklist");
                return true;
            }
            if (std::find(MantellaDialogueIniConfig::config.NPCLineBlacklist.begin(),
                          MantellaDialogueIniConfig::config.NPCLineBlacklist.end(),
                          npcLine) != MantellaDialogueIniConfig::config.NPCLineBlacklist.end()) {
                logger::debug(" -> Filtered: NPC Line Blacklist");
                return true;
            }
            if (topicInfo != nullptr && MantellaDialogueIniConfig::config.FilterNonUniqueGreetings &&
                IsGreeting(playerLine) && !(topicInfo->data.flags & RE::TOPIC_INFO_DATA::TOPIC_INFO_FLAGS::kSayOnce)) {
                logger::debug(" -> Filtered: Greeting ");
                return true;
            }
            if (topicInfo == nullptr && MantellaDialogueIniConfig::config.FilterNonUniqueGreetings &&
                IsGreeting(playerLine))
                logger::error(" -> Error: Topic Info is null");
            if (MantellaDialogueIniConfig::config.FilterShortReplies &&
                MantellaDialogueIniConfig::split(npcLine, ' ').size() <
                    MantellaDialogueIniConfig::config.FilterShortRepliesMinWordCount) {
                logger::debug(" -> Filtered: Short reply");
                return true;
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

        static inline bool ShouldLogHookConfirmation = true;

        // The hook function
        static void thunk(RE::SubtitleManager* a_this, RE::TESObjectREFR* a_speaker, const char* a_subtitle,
                          bool a_alwaysDisplay) {
            // Call original
            if (ShouldLogHookConfirmation == true) logger::info("Hooking into dialogue system...");
            func(a_this, a_speaker, a_subtitle, a_alwaysDisplay);
            if (ShouldLogHookConfirmation == true) { logger::info(" -> Success"); ShouldLogHookConfirmation = false; }

            if (!MantellaDialogueIniConfig::config.EnableVanillaDialogueTracking) return;
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
            for (auto* response : dialogue->responses)
                if (response && !response->text.empty())
                    npcLine += (npcLine.empty() ? "" : " ") + std::string(response->text.c_str());
            auto actor = skyrim_cast<RE::Actor*>(a_speaker);
            if (!actor) {
                logger::error("ShowSubtitle::thunk: a_speaker is empty or not an actor!");
                return;
            }
            // Get the player's name
            auto player = RE::PlayerCharacter::GetSingleton();
            const char* playerName = "Player";
            if (player && player->GetActorBase())
                if (auto name = player->GetActorBase()->GetName(); name && name[0] != '\0') playerName = name;
            auto exchange = DialogueLine();
            exchange.playerLine = currentPlayerTopicText;
            exchange.playerName = playerName;
            exchange.npcLine = npcLine;
            exchange.npcName = actor->GetDisplayFullName();
            exchange.gameTimeHours = GetCurrentGameTimeHours();
            if (std::find(MantellaDialogueIniConfig::config.NPCNamesToIgnore.begin(),
                          MantellaDialogueIniConfig::config.NPCNamesToIgnore.end(),
                          exchange.npcName) != MantellaDialogueIniConfig::config.NPCNamesToIgnore.end())
                return;
            bool conversationRunning = MantellaDialogueTracker::IsConversationRunning();
            bool actorInConversation = MantellaDialogueTracker::IsActorInConversation(actor);

            logger::info("({}): {}", exchange.playerName, exchange.playerLine);
            logger::info("({}): {}", exchange.npcName, exchange.npcLine);


            if (ShouldFilterDialoge(currentPlayerTopicText, npcLine, dialogue->parentTopicInfo)) return;
            

            if (!conversationRunning) {
                if (!MantellaDialogueTracker::DialogueTrackerHasError) {
                    auto formID = actor->GetFormID();
                    MantellaDialogueTracker::s_dialogueHistory[formID].push_back(exchange);
                    logger::info("  -> Not in a conv: Stored dialogue line for later use");
                } else
                    logger::debug(" -> Dialogue tracker is in error state :( cannot save the exchagne");
            } else {
                if (actorInConversation) {
                    AddDialogueExchangeAsync(exchange);
                    logger::info("  -> Sent dialogue to Mantella");
                }
                else {
                    AddDialogueExchangeAsync(exchange);
                    logger::info("  -> Actor not in conversation, sent dialogue to Mantella anyways");
                    if (!MantellaDialogueTracker::DialogueTrackerHasError) {
                        auto formID = actor->GetFormID();
                        MantellaDialogueTracker::s_dialogueHistory[formID].push_back(exchange);
                        logger::info("  -> Actor not in a conv: Stored dialogue line for later use");
                    }
                }
            }
            UpdateLastPlayerTopicText(currentPlayerTopicText);
        }

        // Original function pointer
        static inline REL::Relocation<decltype(thunk)> func;

        // Install
        static void Install() {
            std::array targets{std::make_pair(RELOCATION_ID(19119, 19521), 0x2B2),
                               std::make_pair(RELOCATION_ID(36543, 37544), OFFSET(0x8EC, 0x8C2))};
            if (REL::Module::IsAE)
                targets = std::array{std::make_pair(RELOCATION_ID(19521, 19521), 0x2B2),
                                     std::make_pair(RELOCATION_ID(37544, 37544), OFFSET(0x8C2, 0x8C2))};
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

std::string SerializeDialogueHistoryToJSON() {
    json j;
    for (const auto& [formID, dialogueLines] : Hooks::MantellaDialogueTracker::s_dialogueHistory)
        j[std::to_string(formID)] = dialogueLines;
    return j.dump();
}

bool DeserializeDialogueHistoryFromJSON(const std::string& jsonString) {
    try {
        json j = json::parse(jsonString);
        Hooks::MantellaDialogueTracker::s_dialogueHistory.clear();
        for (auto it = j.begin(); it != j.end(); ++it) {
            RE::FormID formID = static_cast<RE::FormID>(std::stoul(it.key()));
            std::vector<Hooks::DialogueLine> dialogueLines = it.value().get<std::vector<Hooks::DialogueLine>>();
            Hooks::MantellaDialogueTracker::s_dialogueHistory.emplace(formID, std::move(dialogueLines));
        }
        logger::debug("Loaded {} actors with pending lines", Hooks::MantellaDialogueTracker::s_dialogueHistory.size());
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

constexpr std::uint32_t kSerializationID = 'MTDL';

void MySaveCallback(SKSE::SerializationInterface* a_intfc) {
    std::string jsonString = SerializeDialogueHistoryToJSON();
    constexpr std::uint32_t recordType = 'HIST';
    constexpr std::uint32_t version = 1;
    if (!a_intfc->OpenRecord(recordType, version)) {
        logger::error("MySaveCallback: Failed to open 'HIST' record for serialization.");
        return;
    }
    std::uint32_t jsonLength = static_cast<std::uint32_t>(jsonString.size());
    if (!a_intfc->WriteRecordData(&jsonLength, sizeof(jsonLength))) {
        logger::error("MySaveCallback: Failed to write JSON string length.");
        return;
    }
    if (!a_intfc->WriteRecordData(jsonString.c_str(), jsonLength)) {
        logger::error("MySaveCallback: Failed to write JSON string data.");
        return;
    }
    logger::info("MySaveCallback: Serialized dialogue history to SKSE co-save.");
}

void MyLoadCallback(SKSE::SerializationInterface* a_intfc) {
    std::uint32_t type, version, length;
    while (a_intfc->GetNextRecordInfo(type, version, length)) {
        if (type != 'HIST') continue;
        std::uint32_t jsonLength = 0;
        if (a_intfc->ReadRecordData(&jsonLength, sizeof(jsonLength)) != sizeof(jsonLength)) {
            logger::error("MyLoadCallback: Failed to read JSON string length.");
            continue;
        }
        std::string jsonString(jsonLength, '\0');
        if (a_intfc->ReadRecordData(&jsonString[0], jsonLength) != jsonLength) {
            logger::error("MyLoadCallback: Failed to read JSON string data.");
            continue;
        }
        if (DeserializeDialogueHistoryFromJSON(jsonString))
            logger::info("MyLoadCallback: Successfully loaded dialogue history from SKSE co-save.");
        else
            logger::error("MyLoadCallback: Failed to deserialize dialogue history from JSON.");
    }
}

void MyRevertCallback(SKSE::SerializationInterface*) {
    Hooks::MantellaDialogueTracker::s_dialogueHistory.clear();
    logger::info("MyRevertCallback: Cleared dialogue history.");
}
#pragma endregion

// -----------------------------------------------------------------------------
// SKSE Messaging Interface Listener
// -----------------------------------------------------------------------------

void OnSKSEMessage(SKSE::MessagingInterface::Message* a_msg) {
    if (a_msg->type == SKSE::MessagingInterface::kDataLoaded) Hooks::MantellaDialogueTracker::Setup();
    if (a_msg->type == SKSE::MessagingInterface::kPostLoad) {
        auto serialization = SKSE::GetSerializationInterface();
        if (!serialization) {
            logger::error("OnSKSEMessage: Failed to get SKSE Serialization Interface.");
            return;
        }
        serialization->SetUniqueID(kSerializationID);
        serialization->SetSaveCallback(MySaveCallback);
        serialization->SetLoadCallback(MyLoadCallback);
        serialization->SetRevertCallback(MyRevertCallback);
        logger::info("OnSKSEMessage: Registered SKSE Serialization callbacks for dialogue history.");
    }
}

void notifyConversationStart(RE::StaticFunctionTag*) {
    logger::info("notifyConversationStart: Called");
    Hooks::MantellaDialogueTracker::OnConversationStarted();
}

void notifyActorAdded(RE::StaticFunctionTag*, std::vector<RE::TESForm*> actors) {
    logger::info("notifyActorAdded: Called with {} actors", actors.size());
}

void notifyActorRemoved(RE::StaticFunctionTag*, std::vector<RE::TESForm*> actors) {
    logger::info("notifyActorRemoved: Called with {} actors", actors.size());
}

void notifyConversationEnd(RE::StaticFunctionTag*) {
    logger::info("notifyConversationEnd: Called");
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

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);
    SKSE::GetPapyrusInterface()->Register(Bind);
    SetupLog();
    MantellaDialogueIniConfig::loadConfiguration();
    if (auto messaging = SKSE::GetMessagingInterface()) {
        messaging->RegisterListener("SKSE", OnSKSEMessage);
        logger::info("SKSEPluginLoad: Registered SKSE messaging listener.");
    } else
        logger::error("SKSEPluginLoad: Failed to get SKSE Messaging Interface!");
    Hooks::ShowSubtitle::Install();
    logger::info("SKSEPluginLoad: Installed ShowSubtitle hook.");
    return true;
}
