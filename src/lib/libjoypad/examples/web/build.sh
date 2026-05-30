#!/bin/sh
# Build the libjoypad WASM bundle for the web example.
#
# Requires Emscripten on PATH:
#   git clone https://github.com/emscripten-core/emsdk
#   cd emsdk && ./emsdk install latest && ./emsdk activate latest
#   source ./emsdk_env.sh
#
# Outputs:
#   dist/libjoypad.js     ES module that loads libjoypad.wasm
#   dist/libjoypad.wasm   compiled bytecode
#
# Serve dist/ + index.html with any static file server. WebHID requires HTTPS
# in production; localhost is treated as secure context for development.

set -eu

cd "$(dirname "$0")"

if ! command -v emcc >/dev/null 2>&1; then
    echo "error: emcc not on PATH. Install Emscripten:" >&2
    echo "  git clone https://github.com/emscripten-core/emsdk && cd emsdk && ./emsdk install latest && ./emsdk activate latest && source ./emsdk_env.sh" >&2
    exit 1
fi

mkdir -p dist

# Exported functions: every joypad_wasm_* entry point, plus the malloc/free
# the JS side uses to allocate the HID report buffer.
EXPORTED_FUNCTIONS=$(cat <<'EOF'
_joypad_wasm_is_sony_ds5,
_joypad_wasm_ds5_feedback_report_id,
_joypad_wasm_ds5_feedback_payload_len,
_joypad_wasm_parse_ds5,
_joypad_wasm_buttons,
_joypad_wasm_analog,
_joypad_wasm_gyro,
_joypad_wasm_accel,
_joypad_wasm_touch_x,
_joypad_wasm_touch_y,
_joypad_wasm_touch_active,
_joypad_wasm_battery_level,
_joypad_wasm_battery_charging,
_joypad_wasm_has_motion,
_joypad_wasm_has_touch,
_joypad_wasm_feedback_reset,
_joypad_wasm_feedback_set_rumble,
_joypad_wasm_feedback_set_lightbar,
_joypad_wasm_feedback_set_player_index,
_joypad_wasm_feedback_set_adaptive_right,
_joypad_wasm_feedback_set_adaptive_left,
_joypad_wasm_build_ds5_feedback,
_malloc,_free
EOF
)
# Strip newlines and trim to a single comma-separated list.
EXPORTED_FUNCTIONS=$(echo "$EXPORTED_FUNCTIONS" | tr -d '\n ')

# Sources: wasm_bridge plus the libjoypad parsers.
LIBJOYPAD_ROOT=../..
SRCS="wasm_bridge.c \
      $LIBJOYPAD_ROOT/src/devices/usb/hid/sony/ds5.c"
INCLUDES="-I$LIBJOYPAD_ROOT/include"

emcc \
    -O2 \
    -std=c11 \
    -Wall -Wextra -Wpedantic -Wno-unused-parameter \
    $INCLUDES \
    $SRCS \
    -o dist/libjoypad.js \
    -s WASM=1 \
    -s MODULARIZE=1 \
    -s EXPORT_ES6=1 \
    -s EXPORT_NAME=createLibjoypadModule \
    -s ENVIRONMENT=web \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s "EXPORTED_FUNCTIONS=[$EXPORTED_FUNCTIONS]" \
    -s 'EXPORTED_RUNTIME_METHODS=["HEAPU8","HEAPU16","HEAPU32","HEAP16","HEAP32"]'

echo
echo "Built dist/libjoypad.js and dist/libjoypad.wasm"
echo "Serve this directory and open index.html. Chrome/Edge required for WebHID."
