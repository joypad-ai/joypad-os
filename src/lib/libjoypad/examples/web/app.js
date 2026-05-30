// app.js — libjoypad web example
//
// Loads the libjoypad WASM bundle, asks the user to pick a DualSense via
// WebHID, and bridges the two: HID inputreport events go into WASM as raw
// bytes; UI reads parsed fields back from WASM and updates the page.

import createLibjoypadModule from './dist/libjoypad.js';

const Joypad = await createLibjoypadModule();

// ----- Bind exported WASM functions as plain JS functions -----
const _isSonyDs5     = Joypad.cwrap('joypad_wasm_is_sony_ds5',     'number', ['number', 'number']);
const _parseDs5      = Joypad.cwrap('joypad_wasm_parse_ds5',       'number', ['number', 'number']);
const _setTsUs       = Joypad.cwrap('joypad_wasm_set_timestamp_us', null,    ['number', 'number']);
const _buttons       = Joypad.cwrap('joypad_wasm_buttons',         'number', []);
const _analog        = Joypad.cwrap('joypad_wasm_analog',          'number', ['number']);
const _gyro          = Joypad.cwrap('joypad_wasm_gyro',            'number', ['number']);
const _accel         = Joypad.cwrap('joypad_wasm_accel',           'number', ['number']);
const _touchX        = Joypad.cwrap('joypad_wasm_touch_x',         'number', ['number']);
const _touchY        = Joypad.cwrap('joypad_wasm_touch_y',         'number', ['number']);
const _touchActive   = Joypad.cwrap('joypad_wasm_touch_active',    'number', ['number']);
const _batteryLevel  = Joypad.cwrap('joypad_wasm_battery_level',   'number', []);
const _batteryCharge = Joypad.cwrap('joypad_wasm_battery_charging','number', []);
const _hasMotion     = Joypad.cwrap('joypad_wasm_has_motion',      'number', []);
const _hasTouch      = Joypad.cwrap('joypad_wasm_has_touch',       'number', []);

const _fbReset       = Joypad.cwrap('joypad_wasm_feedback_reset',           null, []);
const _fbRumble      = Joypad.cwrap('joypad_wasm_feedback_set_rumble',      null, ['number', 'number']);
const _fbLightbar    = Joypad.cwrap('joypad_wasm_feedback_set_lightbar',    null, ['number', 'number', 'number']);
const _fbPlayer      = Joypad.cwrap('joypad_wasm_feedback_set_player_index', null, ['number']);
const _fbBuild       = Joypad.cwrap('joypad_wasm_build_ds5_feedback',       'number', []);
const _fbReportId    = Joypad.cwrap('joypad_wasm_ds5_feedback_report_id',   'number', []);
const _fbPayloadLen  = Joypad.cwrap('joypad_wasm_ds5_feedback_payload_len', 'number', []);

// Allocate a reusable buffer in WASM heap for incoming HID reports.
const HID_BUF_SIZE = 256;
const hidBufPtr = Joypad._malloc(HID_BUF_SIZE);

// ----- Button bitmap labels (mirror joypad/buttons.h JP_BUTTON_* layout) -----
const BUTTON_BITS = [
  ['B1',  0],  ['B2', 1],  ['B3', 2],  ['B4', 3],
  ['L1',  4],  ['R1', 5],  ['L2', 6],  ['R2', 7],
  ['S1',  8],  ['S2', 9],  ['L3', 10], ['R3', 11],
  ['DU', 12],  ['DD', 13], ['DL', 14], ['DR', 15],
  ['A1', 16],  ['A2', 17], ['A3', 18],
  ['L4', 20],  ['R4', 21],
];

// ----- UI hookup -----
const statusEl = document.getElementById('status');
const setStatus = (msg, isError = false) => {
  statusEl.textContent = msg;
  statusEl.classList.toggle('error', isError);
};

const buttonsContainer = document.getElementById('buttons');
const buttonChips = {};
for (const [label, bit] of BUTTON_BITS) {
  const chip = document.createElement('span');
  chip.className = 'btn-chip';
  chip.textContent = label;
  buttonChips[bit] = chip;
  buttonsContainer.appendChild(chip);
}

const leftStick      = document.getElementById('left-stick');
const rightStick     = document.getElementById('right-stick');
const leftStickVals  = document.getElementById('left-stick-vals');
const rightStickVals = document.getElementById('right-stick-vals');
const l2Bar          = document.getElementById('l2-bar');
const r2Bar          = document.getElementById('r2-bar');
const l2Val          = document.getElementById('l2-val');
const r2Val          = document.getElementById('r2-val');
const gyroEl         = document.getElementById('gyro');
const accelEl        = document.getElementById('accel');
const touch1El       = document.getElementById('touch1');
const touch2El       = document.getElementById('touch2');
const batteryEl      = document.getElementById('battery');
const frameCountEl   = document.getElementById('frame-count');

let frameCount = 0;
let connectedDevice = null;

function updateUI() {
  const buttons = _buttons();
  for (const [, bit] of BUTTON_BITS) {
    const pressed = (buttons & (1 << bit)) !== 0;
    buttonChips[bit].classList.toggle('on', pressed);
  }

  const lx = _analog(0), ly = _analog(1), rx = _analog(2), ry = _analog(3);
  const l2 = _analog(4), r2 = _analog(5);

  leftStick.style.left  = `${(lx / 255) * 100}%`;
  leftStick.style.top   = `${(ly / 255) * 100}%`;
  rightStick.style.left = `${(rx / 255) * 100}%`;
  rightStick.style.top  = `${(ry / 255) * 100}%`;
  leftStickVals.textContent  = `${lx}, ${ly}`;
  rightStickVals.textContent = `${rx}, ${ry}`;

  l2Bar.style.width = `${(l2 / 255) * 100}%`;
  r2Bar.style.width = `${(r2 / 255) * 100}%`;
  l2Val.textContent = l2;
  r2Val.textContent = r2;

  if (_hasMotion()) {
    gyroEl.textContent  = `${_gyro(0)}, ${_gyro(1)}, ${_gyro(2)}`;
    accelEl.textContent = `${_accel(0)}, ${_accel(1)}, ${_accel(2)}`;
  }
  if (_hasTouch()) {
    const t1 = _touchActive(0) ? `${_touchX(0)}, ${_touchY(0)} ★` : '—';
    const t2 = _touchActive(1) ? `${_touchX(1)}, ${_touchY(1)} ★` : '—';
    touch1El.textContent = t1;
    touch2El.textContent = t2;
  }
  const lvl = _batteryLevel();
  batteryEl.textContent = lvl > 0 ? `${lvl}%${_batteryCharge() ? ' (charging)' : ''}` : '—';

  frameCountEl.textContent = ++frameCount;
}

// ----- WebHID hookup -----
async function sendFeedback(builderFn) {
  if (!connectedDevice) return;
  _fbReset();
  builderFn();
  const ptr = _fbBuild();
  if (!ptr) return;
  const len = _fbPayloadLen();
  const data = Joypad.HEAPU8.slice(ptr, ptr + len);
  try {
    await connectedDevice.sendReport(_fbReportId(), data);
  } catch (e) {
    setStatus(`Output report failed: ${e.message}`, true);
  }
}

document.getElementById('connect-btn').addEventListener('click', async () => {
  if (!('hid' in navigator)) {
    setStatus('WebHID is not supported in this browser. Use Chrome or Edge.', true);
    return;
  }
  try {
    // Empty filter: let the user pick anything; libjoypad VID/PID check below.
    const devices = await navigator.hid.requestDevice({
      filters: [
        { vendorId: 0x054c, productId: 0x0ce6 },  // Sony DualSense
        { vendorId: 0x0e6f, productId: 0x0209 },  // Victrix Pro FS PS5
      ],
    });
    if (devices.length === 0) {
      setStatus('No device selected.', true);
      return;
    }
    const device = devices[0];
    if (!_isSonyDs5(device.vendorId, device.productId)) {
      setStatus(`Picked device (VID=${device.vendorId.toString(16)} PID=${device.productId.toString(16)}) is not a DualSense.`, true);
      return;
    }
    if (!device.opened) await device.open();
    connectedDevice = device;
    setStatus(`Connected: ${device.productName || 'DualSense'}`);
    device.addEventListener('inputreport', (event) => {
      // event.data is a DataView over the report payload (without the report ID).
      // We reconstruct the original "full report" by prepending the report ID
      // byte so libjoypad sees the same bytes the firmware would.
      const reportId = event.reportId;
      const len = 1 + event.data.byteLength;
      if (len > HID_BUF_SIZE) return;
      Joypad.HEAPU8[hidBufPtr] = reportId;
      Joypad.HEAPU8.set(new Uint8Array(event.data.buffer, event.data.byteOffset, event.data.byteLength), hidBufPtr + 1);
      if (_parseDs5(hidBufPtr, len)) {
        // Stamp event with monotonic-ish browser time (microseconds).
        // event.timeStamp is a DOMHighResTimeStamp in milliseconds.
        // Split into two 32-bit halves for the WASM 64-bit setter.
        const ts_us = event.timeStamp * 1000;
        const hi = Math.floor(ts_us / 0x100000000);
        const lo = ts_us >>> 0;
        _setTsUs(hi, lo);
        updateUI();
      }
    });
  } catch (e) {
    setStatus(`Connection failed: ${e.message}`, true);
  }
});

document.getElementById('set-lightbar').addEventListener('click', () => {
  const hex = document.getElementById('lightbar-color').value;
  const r = parseInt(hex.slice(1, 3), 16);
  const g = parseInt(hex.slice(3, 5), 16);
  const b = parseInt(hex.slice(5, 7), 16);
  sendFeedback(() => _fbLightbar(r, g, b));
});

const rumbleLow  = document.getElementById('rumble-low');
const rumbleHigh = document.getElementById('rumble-high');
rumbleLow .addEventListener('input', () => document.getElementById('rumble-low-val').textContent  = rumbleLow.value);
rumbleHigh.addEventListener('input', () => document.getElementById('rumble-high-val').textContent = rumbleHigh.value);

document.getElementById('apply-rumble').addEventListener('click', () => {
  sendFeedback(() => _fbRumble(parseInt(rumbleLow.value, 10), parseInt(rumbleHigh.value, 10)));
});
document.getElementById('clear-rumble').addEventListener('click', () => {
  sendFeedback(() => _fbRumble(0, 0));
});
document.getElementById('set-player').addEventListener('click', () => {
  const idx = parseInt(document.getElementById('player-index').value, 10);
  sendFeedback(() => _fbPlayer(idx));
});
