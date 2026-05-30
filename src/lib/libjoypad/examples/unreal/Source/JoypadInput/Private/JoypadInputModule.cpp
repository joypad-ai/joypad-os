// JoypadInputModule.cpp
#include "JoypadInputModule.h"
#include "FJoypadInputDevice.h"

#define LOCTEXT_NAMESPACE "FJoypadInputModule"

IMPLEMENT_MODULE(FJoypadInputModule, JoypadInput)

void FJoypadInputModule::StartupModule()
{
    // The module is registered as an IInputDeviceModule so Unreal will call
    // CreateInputDevice during input device enumeration.
    IInputDeviceModule::StartupModule();
}

void FJoypadInputModule::ShutdownModule()
{
    IInputDeviceModule::ShutdownModule();
}

TSharedPtr<IInputDevice> FJoypadInputModule::CreateInputDevice(
    const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler)
{
    return MakeShared<FJoypadInputDevice>(InMessageHandler);
}

#undef LOCTEXT_NAMESPACE
