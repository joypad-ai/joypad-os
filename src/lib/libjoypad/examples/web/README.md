# web example

Single-page HTML demo. Emscripten builds libjoypad to WebAssembly; a JS shim
uses WebHID to receive raw HID reports, passes them through WASM to libjoypad,
reads back the decoded `input_event_t`, and draws gamepad state on the page.

Validates the WASM/JS marshaling boundary and proves the same parser code that
runs in firmware also runs in a browser tab.

Status: not yet implemented (Task #9).
