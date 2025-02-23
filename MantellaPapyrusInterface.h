#pragma once

namespace MantellaPapyrusInterface {


 void AddMantellaEvent(std::string msg) {
    auto targetFunction = "AddMantellaEvent";
    auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
    auto mantellaQuest = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESQuest>(0x03D41A, "Mantella.esp");
    auto questHandle = vm->GetObjectHandlePolicy()->GetHandleForObject(mantellaQuest->GetFormType(), mantellaQuest);
    RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
    RE::BSTSmartPointer<RE::BSScript::Object> script = nullptr;
    if (vm->FindBoundObject(questHandle, "MantellaInterface", script)) {
        auto args = RE::MakeFunctionArguments(std::move(msg));
        vm->DispatchMethodCall1(script, targetFunction, args, callback);
    }
}

RE::VMHandle GetMantellaRepositoryHandle() {
    auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
    auto mantellaQuest = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESQuest>(0xD62, "Mantella.esp");
    auto questHandle = vm->GetObjectHandlePolicy()->GetHandleForObject(mantellaQuest->GetFormType(), mantellaQuest);
    return questHandle;
}

 bool GetMantellaMcmSetting(std::string propertyName, RE::BSScript::Variable& a_getVal) {
    auto questHandle = GetMantellaRepositoryHandle();
    auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
    RE::BSTSmartPointer<RE::BSScript::Object> script = nullptr;
    if (vm->FindBoundObject(questHandle, "MantellaRepository", script)) {
        auto propertyPointer = script->GetProperty(propertyName);
        if (!propertyPointer) {
            logger::error("Failed to get Mantella setting {}", propertyName);
            return false;
        }
        a_getVal = *propertyPointer;
        return true;
    }
    return false;
}

 bool GetMantellaEnableVanillaDialogueAwareness() {
    RE::BSScript::Variable property;
    auto result = GetMantellaMcmSetting("enableVanillaDialogueAwareness", property);
   
    if (result && property.IsBool()) 
        return property.GetBool();
    else
        logger::error("Failed to get Mantella setting enableVanillaDialogueAwareness");
    return true;
}

/// <returns>The port the mantella server is reachable under (From the Mcm)</returns>
 int GetMantellaServerPort() { 
    RE::BSScript::Variable property;
    auto result = GetMantellaMcmSetting("HttpPort", property);
    if (result && property.IsInt()) {
       int port = property.GetSInt();
        return port;
    }
    return -1;
}

}
