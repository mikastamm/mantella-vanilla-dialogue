#include "PCH.h"

namespace Hooks 
{
    using SetSubtitleType = char* (*)(RE::DialogueResponse* a_response, char* text, int32_t unk);
    REL::Relocation<SetSubtitleType> _SetSubtitle;

    char* SetSubtitle(RE::DialogueResponse* a_response, char* text, int32_t a_3)
    {
        if (text) {
            // Show both a notification and popup for the subtitle
            RE::DebugNotification(text);
            RE::DebugMessageBox(text);
        }
        return _SetSubtitle(a_response, text, a_3);
    }

    void Install()
    {
        auto& trampoline = SKSE::GetTrampoline();
        SKSE::AllocTrampoline(64);

        // Hook the SetSubtitle function using trampoline
        REL::Relocation<std::uintptr_t> target{ REL::RelocationID(34429, 35249) };
        _SetSubtitle = trampoline.write_call<5>(target.address() + REL::Relocate(0x61, 0x61), SetSubtitle);

    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);
    
    // Install our hooks
    Hooks::Install();
    
    return true;
}
