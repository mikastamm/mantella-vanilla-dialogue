#include "PCH.h"
#include "logger.h" 

namespace Hooks {
    struct MantellaDialogueTracker {
        inline static RE::BGSListForm* aParticipants;
        inline static bool DialogueTrackerHasError = false;
        const char* ConversationParticipantsFormId = "";

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

        };
    };

    struct ShowSubtitle {
        // Holds the last processed player's topic text so we don't process duplicates.
        inline static std::string s_lastPlayerTopicText{};
        const char* ConversatioStartedEvent = "ConversationStarted";
        const char* ConversationEndedEvent = "ConversationEnded";
        // ----------------------------------------------------------------------------
        // 1) Check function: determines if the topic has already been processed
        // ----------------------------------------------------------------------------
        static bool HasAlreadyProcessed(std::string_view a_topicText) { return a_topicText == s_lastPlayerTopicText; }

        // ----------------------------------------------------------------------------
        // 2) Update function: remember the last player topic after we process it
        // ----------------------------------------------------------------------------
        static void UpdateLastPlayerTopicText(std::string_view a_topicText) {
            s_lastPlayerTopicText = a_topicText;  // Copies from view into s_lastPlayerTopicText
        }

        // ----------------------------------------------------------------------------
        // 3) Fires a Mantella event (SKSE's custom event system)
        // ----------------------------------------------------------------------------
        static void AddMantellaEvent(const char* a_event) {
            SKSE::ModCallbackEvent modEvent{"MantellaAddEvent", a_event};
            if (auto modCallbackSource = SKSE::GetModCallbackEventSource(); modCallbackSource) {
                modCallbackSource->SendEvent(&modEvent);
            } else {
                RE::DebugNotification("AddMantellaEvent: No ModCallbackEventSource found!");
            }
        }

        // ----------------------------------------------------------------------------
        // 4) The main hook (thunk) that intercepts the call to show subtitles
        // ----------------------------------------------------------------------------
        static void thunk(RE::SubtitleManager* a_this, RE::TESObjectREFR* a_speaker, const char* a_subtitle,
                          bool a_alwaysDisplay) {
            // Preserve original functionality.
            func(a_this, a_speaker, a_subtitle, a_alwaysDisplay);

            // If there's no speaker, nothing else to do here.
            if (!a_speaker) {
                return;
            }

            auto topicManager = RE::MenuTopicManager::GetSingleton();
            if (!topicManager) {
                logger::error("ShowSubtitle::thunk: MenuTopicManager is null!");
                return;
            }

            auto dialogue = topicManager->lastSelectedDialogue;
            if (!dialogue) {
                // Sometimes there's no valid dialogue object.
                return;
            }

            // Convert the dialogue topic text to a safe string for our checks.
            const std::string currentPlayerTopicText = dialogue->topicText.c_str();
            if (currentPlayerTopicText.empty()) {
                // If for some reason there's no topic text, we don't process it.
                RE::DebugNotification("ShowSubtitle::thunk: No player topic text found!");
                return;
            }

            // Skip processing if we've already handled this exact player topic text.
            if (HasAlreadyProcessed(currentPlayerTopicText)) {
                return;
            }

            // Build the NPC response string
            std::string npcResponse;
            for (auto* response : dialogue->responses) {
                if (!response) {
                    RE::DebugNotification("ShowSubtitle::thunk: Null response encountered!");
                    continue;
                }
                if (!response->text.empty()) {
                    if (!npcResponse.empty()) {
                        npcResponse += " ";
                    }
                    npcResponse += response->text.c_str();
                } 
            }

            // Cast speaker to Actor* so we can get an NPC name.
            auto actor = skyrim_cast<RE::Actor*>(a_speaker);
            if (!actor) {
                // If the speaker is not an actor, bail.
                RE::DebugNotification("ShowSubtitle::thunk: a_speaker is not an Actor!");
                return;
            }

            // Get the player's name; fall back to "Player" if something is off.
            auto player = RE::PlayerCharacter::GetSingleton();
            const char* playerName = "Player";
            if (player && player->GetActorBase()) {
                if (auto name = player->GetActorBase()->GetName(); name && name[0] != '\0') {
                    playerName = name;
                }
            }

            // Construct the event strings.
            std::string playerEvent = std::string(playerName) + ": " + currentPlayerTopicText;
            std::string npcEvent = std::string(actor->GetName()) + ": " + npcResponse;

            // Fire SKSE events
            AddMantellaEvent(playerEvent.c_str());
            if (!npcEvent.empty())
            AddMantellaEvent(npcEvent.c_str());

            // Remember this player topic so we don't process it again.
            UpdateLastPlayerTopicText(currentPlayerTopicText);
        }

        // Original function pointer, set by write_thunk_call.
        static inline REL::Relocation<decltype(thunk)> func;

        // ----------------------------------------------------------------------------
        // Installation function: writes our thunk calls to the target addresses
        // ----------------------------------------------------------------------------
        static void Install() {
            // List of targets where we need to patch in our thunk
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
}


// Typical SKSE entry point
SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);
    SetupLog();
    Hooks::ShowSubtitle::Install();
    return true;
}
