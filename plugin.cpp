#include "PCH.h"

namespace Hooks {
    struct ShowSubtitle {
        // A static variable to store the last processed player's topic text
        static inline std::string s_lastPlayerTopicText{};

        // ----------------------------------------------------------------------------
        // 1) Check function: determines if the topic has already been processed
        // ----------------------------------------------------------------------------
        static bool HasAlreadyProcessed(const std::string& currentPlayerTopicText) {
            // If the current topic is the same as the one we last processed, return true
            if (currentPlayerTopicText == s_lastPlayerTopicText) return true;
            return false;
        }

        // ----------------------------------------------------------------------------
        // 2) Update function: remember the last player topic after we process it
        // ----------------------------------------------------------------------------
        static void UpdateLastPlayerTopicText(const std::string& currentPlayerTopicText) {
            s_lastPlayerTopicText = currentPlayerTopicText;
        }

        // ----------------------------------------------------------------------------
        // The main thunk that hooks the game
        // ----------------------------------------------------------------------------
        static void thunk(RE::SubtitleManager* a_this, RE::TESObjectREFR* a_speaker, const char* a_subtitle,
                          bool a_alwaysDisplay) {
            // Keep the original behavior
            func(a_this, a_speaker, a_subtitle, a_alwaysDisplay);


            if (a_speaker) {
                if (auto dialogue = RE::MenuTopicManager::GetSingleton()->lastSelectedDialogue) {
                    // First, check whether we have new player topic text.
                    std::string currentPlayerTopicText = dialogue->topicText.c_str();
                    if (HasAlreadyProcessed(currentPlayerTopicText)) {
                        return;
                    }

                    // Now build and display the NPC response string
                    std::string npcResponse;
                    for (auto* response : dialogue->responses) {
                        if (!response) {
                            RE::DebugNotification("UpdateSelectedResponse: Found a null 'response'!");
                            continue;
                        }
                        // Add the text if non-empty
                        if (!response->text.empty()) {
                            if (!npcResponse.empty()) npcResponse += " ";
                            npcResponse += response->text.c_str();
                        } else {
                            RE::DebugNotification("UpdateSelectedResponse: 'response->text' is empty!");
                        }
                    }

                    //Get actor name from a_speaker
                    RE::Actor* actor = static_cast<RE::Actor*>(a_speaker);

                    auto player = RE::PlayerCharacter::GetSingleton();
                    const char* playerName = "Player:";
                    if (player) {
                        // You now have a reference to the player
                        // Example: Display player's name
                        playerName = player->GetActorBase()->GetName();
                    }
                    auto playerEvent = std::string(playerName) + ": " + currentPlayerTopicText;
                    std::string npcEvent = std::string(actor->GetName()) + ": " + npcResponse;

                    // If it's new, call AddMantellaEvent
                    AddMantellaEvent(playerEvent.c_str());
                    AddMantellaEvent(npcEvent.c_str());
                    // And store the topic text, so we don’t process it again
                    UpdateLastPlayerTopicText(currentPlayerTopicText);

                }
            }
        }

 

        // ----------------------------------------------------------------------------
        // Fires a Mantella event
        // ----------------------------------------------------------------------------
        static void AddMantellaEvent(const char* event) {
            SKSE::ModCallbackEvent modEvent{"MantellaAddEvent", event};
            auto modCallbackSource = SKSE::GetModCallbackEventSource();
            modCallbackSource->SendEvent(&modEvent);
        }

        // Original function pointer
        static inline REL::Relocation<decltype(thunk)> func;

        // ----------------------------------------------------------------------------
        // Install hook
        // ----------------------------------------------------------------------------
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
}

// Typical SKSE entry point
SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);

    Hooks::ShowSubtitle::Install();
    return true;
}
