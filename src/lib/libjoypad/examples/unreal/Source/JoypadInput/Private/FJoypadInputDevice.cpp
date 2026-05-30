// FJoypadInputDevice.cpp
#include "FJoypadInputDevice.h"

#if __has_include(<hidapi.h>)
extern "C" {
#  include <hidapi.h>
}
#elif __has_include(<hidapi/hidapi.h>)
extern "C" {
#  include <hidapi/hidapi.h>
}
#else
#  error "hidapi.h not found — install libhidapi-dev or brew install hidapi"
#endif

extern "C" {
#include <joypad/devices/sony/ds5.h>
}

#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogJoypadInput, Log, All);

// ----------------------------------------------------------------------------
// Button bit → Unreal FKey mapping.
// Matches Enhanced Input's GamepadKey set; advanced buttons (A2/A3/L4/R4) map
// to FName-only keys so games that don't bind them still build cleanly.
// ----------------------------------------------------------------------------

static const TCHAR* KeyNameForJoypadBit(int Bit)
{
    switch (Bit)
    {
        case 0:  return TEXT("Gamepad_FaceButton_Bottom");   // B1 → Cross / A
        case 1:  return TEXT("Gamepad_FaceButton_Right");    // B2 → Circle / B
        case 2:  return TEXT("Gamepad_FaceButton_Left");     // B3 → Square / X
        case 3:  return TEXT("Gamepad_FaceButton_Top");      // B4 → Triangle / Y
        case 4:  return TEXT("Gamepad_LeftShoulder");        // L1
        case 5:  return TEXT("Gamepad_RightShoulder");       // R1
        case 6:  return TEXT("Gamepad_LeftTrigger");         // L2 (digital edge)
        case 7:  return TEXT("Gamepad_RightTrigger");        // R2 (digital edge)
        case 8:  return TEXT("Gamepad_Special_Left");        // S1 / Share
        case 9:  return TEXT("Gamepad_Special_Right");       // S2 / Option
        case 10: return TEXT("Gamepad_LeftThumbstick");      // L3
        case 11: return TEXT("Gamepad_RightThumbstick");     // R3
        case 12: return TEXT("Gamepad_DPad_Up");             // DU
        case 13: return TEXT("Gamepad_DPad_Down");           // DD
        case 14: return TEXT("Gamepad_DPad_Left");           // DL
        case 15: return TEXT("Gamepad_DPad_Right");          // DR
        case 16: return TEXT("Gamepad_Special_Left_X");      // A1 / PS / Guide
        case 17: return TEXT("Joypad_TouchpadClick");        // A2 — custom
        case 18: return TEXT("Joypad_MuteButton");           // A3 — custom
        case 20: return TEXT("Joypad_LeftPaddle");           // L4 — custom
        case 21: return TEXT("Joypad_RightPaddle");          // R4 — custom
        default: return nullptr;
    }
}

FJoypadInputDevice::FJoypadInputDevice(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler)
    : MessageHandler(InMessageHandler)
{
    hid_init();
    UE_LOG(LogJoypadInput, Log, TEXT("Joypad Input initialized (libjoypad)."));
}

FJoypadInputDevice::~FJoypadInputDevice()
{
    CloseDevice();
    hid_exit();
}

void FJoypadInputDevice::SetMessageHandler(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler)
{
    MessageHandler = InMessageHandler;
}

bool FJoypadInputDevice::TryOpenDevice()
{
    if (bHaveDevice) return true;

    hid_device_info* list = hid_enumerate(0, 0);
    char Path[512] = {0};
    for (hid_device_info* it = list; it != nullptr; it = it->next)
    {
        if (joypad_is_sony_ds5(it->vendor_id, it->product_id))
        {
            FCStringAnsi::Strncpy(Path, it->path, sizeof(Path));
            break;
        }
    }
    hid_free_enumeration(list);
    if (!Path[0]) return false;

    HidDev = hid_open_path(Path);
    if (!HidDev)
    {
        UE_LOG(LogJoypadInput, Warning, TEXT("hid_open_path failed"));
        return false;
    }
    hid_set_nonblocking(HidDev, 1);
    bHaveDevice = true;
    PrevEvent = {};
    UE_LOG(LogJoypadInput, Log, TEXT("DualSense opened via libjoypad."));
    return true;
}

void FJoypadInputDevice::CloseDevice()
{
    if (HidDev)
    {
        hid_close(HidDev);
        HidDev = nullptr;
    }
    bHaveDevice = false;
}

void FJoypadInputDevice::Tick(float DeltaTime)
{
    if (!bHaveDevice)
    {
        ReopenAccum += DeltaTime;
        if (ReopenAccum > 1.f)
        {
            ReopenAccum = 0.f;
            TryOpenDevice();
        }
        return;
    }

    uint8_t Buf[256];
    int n = hid_read_timeout(HidDev, Buf, sizeof(Buf), 0);
    if (n <= 0) return;

    input_event_t Event;
    if (joypad_parse_sony_ds5(Buf, (uint16_t)n, &Event))
    {
        DispatchEvent(Event);
        PrevEvent = Event;
    }
}

void FJoypadInputDevice::SendControllerEvents()
{
    // Tick() already pushes events into the message handler. SendController-
    // Events is left for engine compatibility but no extra work needed.
}

void FJoypadInputDevice::DispatchEvent(const input_event_t& Event)
{
    const int32 ControllerId = 0;
    constexpr FPlatformUserId UserId = PLATFORMUSERID_NONE;
    constexpr FInputDeviceId InputDeviceId = INPUTDEVICEID_NONE;

    // --- Buttons (edge-triggered against PrevEvent) -------------------------
    uint32 Changed = Event.buttons ^ PrevEvent.buttons;
    for (int Bit = 0; Bit < 22; ++Bit)
    {
        uint32 Mask = 1u << Bit;
        if ((Changed & Mask) == 0) continue;

        const TCHAR* KeyName = KeyNameForJoypadBit(Bit);
        if (!KeyName) continue;

        const FName KeyFName(KeyName);
        const bool bPressed = (Event.buttons & Mask) != 0;

        if (bPressed)
        {
            MessageHandler->OnControllerButtonPressed(KeyFName, UserId, InputDeviceId, /*IsRepeat*/ false);
        }
        else
        {
            MessageHandler->OnControllerButtonReleased(KeyFName, UserId, InputDeviceId, /*IsRepeat*/ false);
        }
    }

    // --- Analog axes (normalized to [-1, 1] for sticks, [0, 1] for triggers)
    auto SendAxis = [&](const TCHAR* Name, float Value)
    {
        MessageHandler->OnControllerAnalog(FName(Name), UserId, InputDeviceId, Value);
    };

    const auto StickToFloat = [](uint8_t v) -> float {
        // HID convention: 0=up/left, 128=center, 255=down/right
        return (static_cast<float>(v) - 128.f) / 127.f;
    };
    const auto TriggerToFloat = [](uint8_t v) -> float {
        return static_cast<float>(v) / 255.f;
    };

    SendAxis(TEXT("Gamepad_LeftX"),         StickToFloat(Event.analog[ANALOG_LX]));
    SendAxis(TEXT("Gamepad_LeftY"),        -StickToFloat(Event.analog[ANALOG_LY]));  // UE convention: +up
    SendAxis(TEXT("Gamepad_RightX"),        StickToFloat(Event.analog[ANALOG_RX]));
    SendAxis(TEXT("Gamepad_RightY"),       -StickToFloat(Event.analog[ANALOG_RY]));
    SendAxis(TEXT("Gamepad_LeftTriggerAxis"),  TriggerToFloat(Event.analog[ANALOG_L2]));
    SendAxis(TEXT("Gamepad_RightTriggerAxis"), TriggerToFloat(Event.analog[ANALOG_R2]));

    // --- Advanced features as custom analog values --------------------------
    if (Event.has_motion)
    {
        SendAxis(TEXT("Joypad_GyroX"),  static_cast<float>(Event.gyro[0]));
        SendAxis(TEXT("Joypad_GyroY"),  static_cast<float>(Event.gyro[1]));
        SendAxis(TEXT("Joypad_GyroZ"),  static_cast<float>(Event.gyro[2]));
        SendAxis(TEXT("Joypad_AccelX"), static_cast<float>(Event.accel[0]));
        SendAxis(TEXT("Joypad_AccelY"), static_cast<float>(Event.accel[1]));
        SendAxis(TEXT("Joypad_AccelZ"), static_cast<float>(Event.accel[2]));
    }
    if (Event.has_touch)
    {
        // Normalize to [0, 1]
        SendAxis(TEXT("Joypad_Touch0_X"), Event.touch[0].active ? Event.touch[0].x / 1919.f : 0.f);
        SendAxis(TEXT("Joypad_Touch0_Y"), Event.touch[0].active ? Event.touch[0].y / 1079.f : 0.f);
        SendAxis(TEXT("Joypad_Touch1_X"), Event.touch[1].active ? Event.touch[1].x / 1919.f : 0.f);
        SendAxis(TEXT("Joypad_Touch1_Y"), Event.touch[1].active ? Event.touch[1].y / 1079.f : 0.f);
    }
}

void FJoypadInputDevice::SetChannelValue(int32 /*ControllerId*/, FForceFeedbackChannelType /*ChannelType*/, float /*Value*/)
{
    // TODO: route to libjoypad feedback builder + hid_write.
}

void FJoypadInputDevice::SetChannelValues(int32 /*ControllerId*/, const FForceFeedbackValues& /*Values*/)
{
    // TODO: route to libjoypad feedback builder + hid_write.
}
