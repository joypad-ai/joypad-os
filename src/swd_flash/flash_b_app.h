// flash_b_app.h - flash the host-side (B) RP2040 over SWD from A's running app.
#pragma once

// Flash B from A's app using the image staged in A's flash by combine_uf2.py.
// logf (may be NULL) receives progress lines for CDC logging. Blocks for the
// duration (~10-20s). Returns 0 on success, negative stage code on failure.
int flash_b_app(void (*logf)(const char*));
