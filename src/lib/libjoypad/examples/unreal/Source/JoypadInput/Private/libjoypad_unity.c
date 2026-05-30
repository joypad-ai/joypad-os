// libjoypad_unity.c
//
// Unity-build translation unit for the libjoypad sources we want compiled
// into the JoypadInput plugin. Keeping all #includes in one place avoids
// reaching across PrivateIncludePaths from individual driver TUs and keeps
// UBT's compilation graph simple.
//
// As more drivers move into libjoypad, add their #include here.

#include "../../../../../src/devices/usb/hid/sony/ds5.c"
