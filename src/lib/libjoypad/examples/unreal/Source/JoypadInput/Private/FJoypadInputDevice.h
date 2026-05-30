// FJoypadInputDevice.h
//
// Polls libjoypad-supported controllers via HIDAPI and forwards the resulting
// input_event_t into Unreal's input message handler. One device for the demo;
// production code would track multiple slots.

#pragma once

#include "CoreMinimal.h"
#include "IInputDevice.h"
#include "GenericPlatform/GenericApplicationMessageHandler.h"

extern "C" {
#include <joypad/input_event.h>
#include <joypad/buttons.h>
}

struct hid_device_;
typedef struct hid_device_ hid_device;

class FJoypadInputDevice : public IInputDevice
{
public:
    explicit FJoypadInputDevice(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler);
    virtual ~FJoypadInputDevice();

    // IInputDevice
    virtual void Tick(float DeltaTime) override;
    virtual void SendControllerEvents() override;
    virtual void SetMessageHandler(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler) override;
    virtual bool Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar) override { return false; }
    virtual void SetChannelValue(int32 ControllerId, FForceFeedbackChannelType ChannelType, float Value) override;
    virtual void SetChannelValues(int32 ControllerId, const FForceFeedbackValues& Values) override;

private:
    bool TryOpenDevice();
    void CloseDevice();
    void DispatchEvent(const input_event_t& Event);

    TSharedRef<FGenericApplicationMessageHandler> MessageHandler;
    hid_device* HidDev = nullptr;
    input_event_t PrevEvent {};
    bool bHaveDevice = false;
    float ReopenAccum = 0.f;
};
