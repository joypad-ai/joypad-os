// btstack_host_nus_send stub for ESP32 BLE-peripheral builds (controller_btusb).
//
// The real implementation is in bt/btstack/btstack_host.c, which ESP only
// compiles in central mode (bt2usb). Peripheral builds still compile the shared
// cmd_face_forward handler in cdc_commands.c (it relays FACE.* commands to a
// paired JoypadOS face over BLE NUS *as a central*), which references this
// symbol. A peripheral has no central/host NUS link to a face, so the relay is a
// no-op here and the command reports "no face connected".
//
// nRF peripheral builds don't need this — they compile btstack_host.c in all
// modes — so this stub lives only in the ESP peripheral source list.
#include <stdint.h>
#include <stdbool.h>

bool btstack_host_nus_send(const uint8_t *data, uint16_t len)
{
    (void)data;
    (void)len;
    return false;
}
