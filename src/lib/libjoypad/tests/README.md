# libjoypad tests

Fixture-based replay tests. Real HID reports captured from physical controllers
(via Comrade, USBPcap, etc.) are stored in `fixtures/<vendor>/<device>/`. The
runner in `replay/` feeds each fixture through the appropriate parser and asserts
the resulting `input_event_t` matches a known-good snapshot.

Runs on host (no firmware needed). Same fixtures verify that firmware, userspace,
and WASM builds produce identical output — that's the integrity proof that the
boundary is real.

Status: not yet implemented (Task #7).
