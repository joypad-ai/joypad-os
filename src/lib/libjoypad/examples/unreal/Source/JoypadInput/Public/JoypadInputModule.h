// JoypadInputModule.h
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "IInputDeviceModule.h"

class FJoypadInputModule : public IInputDeviceModule
{
public:
    // IModuleInterface
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

    // IInputDeviceModule
    virtual TSharedPtr<class IInputDevice> CreateInputDevice(
        const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler) override;
};
