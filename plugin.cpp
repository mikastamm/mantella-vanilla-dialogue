#include <map>  // For std::map or std::unordered_map, whichever you prefer
#include <set>  // We use std::set instead of std::unordered_set

#include "PCH.h"
#include "logger.h"

namespace Hooks {
    // -------------------------------------------------------------------------
    // A small POD struct to store a single exchange: player's line, NPC's line,
    // and the Skyrim in-game time at which it was recorded.
    // -------------------------------------------------------------------------
    struct DialogueLine {
        std::string playerQuery;
        std::string npcResponse;
        float gameTimeHours;  // e.g. from RE::Calendar::GetSingleton()->GetHoursPassed()
    };

    // -------------------------------------------------------------------------
    // A simple helper to fetch current game time in hours. You can adjust to
    // whatever granularity you need (days, hours, etc.).
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
        // If your environment does not support std::unordered_map, you can use std::map.
        // We’ll assume at least std::map is available.
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


            // If the form list has at least one actor, treat it as "conversation is running."
            // (Adjust as you see fit.)
            return !aParticipants->scriptAddedTempForms->empty();
        }

        // ---------------------------------------------------------------------
        // Helper: checks if a given actor is listed in the current participants
        // form list. If your mod updates `aParticipants` over time, we compare
        // each form’s GetFormID().
        // ---------------------------------------------------------------------
        static bool IsActorInConversation(RE::Actor* a_actor) {
            if (!a_actor || !aParticipants || DialogueTrackerHasError) return false;

            auto formID = a_actor->GetFormID();

            for (auto& actorInConvFormId : *aParticipants->scriptAddedTempForms) {
                if (actorInConvFormId == formID) return true;
            }
            return false;
        }

        // ---------------------------------------------------------------------
        // **PLACEHOLDER**: Called when a conversation starts. In a future version,
        // you might have an actual SKSE or Mantella event for conversation start.
        // ---------------------------------------------------------------------
        static void OnConversationStarted() {
            if (DialogueTrackerHasError || !aParticipants) return;

            // In case you want to re-check participants, do so here
            s_lastParticipants.clear();

            // For each form in the participants, replay old lines if any
            for (auto& formID : *aParticipants->scriptAddedTempForms) {
                
                s_lastParticipants.insert(formID);

                // See if we have stored lines for this participant
                auto it = s_dialogueHistory.find(formID);
                if (it == s_dialogueHistory.end()) continue;

                // Replay those lines. For example:
                for (auto& line : it->second) {
                    // Fire Mantella events or do your logic to pass them in
                    SKSE::ModCallbackEvent modEventA{"MantellaAddEvent",
                                                     (std::string("Replay - ") + line.playerQuery).c_str()};
                    if (auto source = SKSE::GetModCallbackEventSource(); source) source->SendEvent(&modEventA);

                    SKSE::ModCallbackEvent modEventB{"MantellaAddEvent",
                                                     (std::string("Replay - ") + line.npcResponse).c_str()};
                    if (auto source = SKSE::GetModCallbackEventSource(); source) source->SendEvent(&modEventB);
                }

                // Once replayed, remove them
                s_dialogueHistory.erase(it);
            }
        }

        // ---------------------------------------------------------------------
        // **PLACEHOLDER**: Called if a new participant joins mid-conversation.
        // You can implement this once you have the actual event from Mantella.
        // ---------------------------------------------------------------------
        static void OnNewParticipant(RE::Actor* a_newActor) {
            if (!a_newActor || DialogueTrackerHasError) return;

            auto newFormID = a_newActor->GetFormID();
            auto it = s_dialogueHistory.find(newFormID);
            if (it == s_dialogueHistory.end()) return;

            // Replay the lines
            for (auto& line : it->second) {
                SKSE::ModCallbackEvent modEventA{"MantellaAddEvent",
                                                 (std::string("Replay - ") + line.playerQuery).c_str()};
                if (auto source = SKSE::GetModCallbackEventSource(); source) source->SendEvent(&modEventA);

                SKSE::ModCallbackEvent modEventB{"MantellaAddEvent",
                                                 (std::string("Replay - ") + line.npcResponse).c_str()};
                if (auto source = SKSE::GetModCallbackEventSource(); source) source->SendEvent(&modEventB);
            }
            // Remove them once replayed
            s_dialogueHistory.erase(it);
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
            s_lastPlayerTopicText = a_topicText.data();
        }

        // Fire an event to Mantella (unchanged from your example)
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
            
            // -------------------------------
            // Placeholder:
            // Force "OnConversationStarted" each time a new line is processed.
            // In future, replace this with your real event-based approach.
            // -------------------------------
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
                }
                // If you still want to fire events (knowing they'll be ignored by Mantella),
                // you can do so:
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

void OnSKSEMessage(SKSE::MessagingInterface::Message* a_msg) {
    if (a_msg->type == SKSE::MessagingInterface::kDataLoaded) {
        // Setup the Mantella tracker
        Hooks::MantellaDialogueTracker::Setup();
    }
}

// Typical SKSE entry point
SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);
    SetupLog();

      if (auto messaging = SKSE::GetMessagingInterface()) {
        messaging->RegisterListener("SKSE", OnSKSEMessage);
    } else {
        logger::error("Failed to get SKSE Messaging Interface!");
    }

    // Hook subtitles
    Hooks::ShowSubtitle::Install();
    return true;
}
