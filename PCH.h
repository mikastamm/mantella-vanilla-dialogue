#pragma once

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"
#include "REL/Relocation.h"
#include "SKSE/Trampoline.h"
#include "SKSE/Logger.h"

// CommonLibSSE
#include "RE/T/TESObjectREFR.h"
#include "RE/T/TESForm.h"
#include "RE/B/BGSBaseAlias.h"
#include "RE/B/BSFixedString.h"
#include "RE/B/BSTEvent.h"
#include "RE/S/SubtitleManager.h"

// Standard
#include <string>
#include <array>

#ifdef SKYRIM_AE
    #define OFFSET(se, ae) ae
#else
    #define OFFSET(se, ae) se
#endif

// Address Library

namespace stl
{
    using namespace SKSE::stl;

    template <class T>
    void write_thunk_call(std::uintptr_t a_src)
    {
        auto& trampoline = SKSE::GetTrampoline();
        SKSE::AllocTrampoline(64);
        T::func = trampoline.write_call<5>(a_src, T::thunk);
    }
}



using namespace std::literals;
using namespace SKSE::log;
