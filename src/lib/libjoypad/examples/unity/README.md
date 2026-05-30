# unity example

Bare Unity native plugin. libjoypad compiles to platform-native dynamic libraries
(.dll/.dylib/.so); a C# wrapper uses P/Invoke to call into them and bridges to
Unity's Input System.

Reference integration showing how Unity devs would consume libjoypad. The polished
Asset Store version lives in a future `joypad-unity` repo.

Status: not yet implemented (post-MVP).
