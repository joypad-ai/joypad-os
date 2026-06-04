# NES Multitap (4-Controller) Implementation Plan

**Date:** 2026-06-04  
**Status:** Analysis + Planning Phase (no implementation)  
**Scope:** Extend nes2usb to support 4 simultaneous NES controllers via physical multitap wiring.

---

## 1. Current nes2usb Implementation Summary

### 1.1 PIO Layer (`src/native/host/nes/nes_host.pio`)
- Single PIO program (`nes_host`) using side-set for CLK + LATCH.
- `in pins, 1` samples **one** DATA line (DATA0) per clock pulse.
- 8-bit shift register per 60 Hz poll.
- IRQ 0 triggered by CPU to start latch sequence.
- Side-set pins: bit 0 = CLK (GPIO 5), bit 1 = LATCH (GPIO 6).

### 1.2 C Driver Layer (`src/native/host/nes/nes_host.c`)
- Hardcoded single data pin: `NES_PIN_DATA0` (default GPIO 8).
- Single `tick_ctx_t` struct holding:
  - One `prev_buttons`, one `connected` flag, one `data_line_idle_high`.
  - Connection detection via 500 ms debounce on DATA line idle state (pull-up = HIGH → disconnected).
- `nes_timer_cb()` samples only DATA0 before forcing IRQ.
- `pio0_irq0_handler()` reads exactly one 32-bit word from RX FIFO, maps to one button set.
- `nes_host_task()` emits exactly **one** `input_event_t` per call (`dev_addr = 0xF0 + 0`).
- `get_device_count()` returns 0 or 1.
- Input interface registered as `INPUT_SOURCE_NATIVE_NES`.

### 1.3 App Layer (`src/apps/nes2usb/app.c`)
- Registers exactly one `InputInterface`.
- Router configured for single-source routing (`INPUT_SOURCE_NATIVE_NES` → `OUTPUT_TARGET_USB_DEVICE`).
- Player management and profile system already support multiple slots (see `MAX_PLAYER_SLOTS`, `USB_OUTPUT_PORTS`).

### 1.4 Pin Definitions (`nes_host.h`)
```c
#define NES_PIN_CLOCK  5
#define NES_PIN_LATCH  6
#define NES_PIN_DATA0  8
#define NES_MAX_PORTS  1   // currently hard-coded
```

---

## 2. NES Protocol Properties Relevant to Multitap

- **Shared control lines**: One LATCH + one CLK line can drive all controllers simultaneously.
- **Independent data lines**: Each controller has its own open-collector DATA line (active-low, pulled high when idle).
- **Physical multitap wiring**: Standard NES multitap (or custom) simply fans out CLK/LATCH to all four ports and brings back four separate DATA lines.
- **No protocol changes** required on the controller side.

---

## 3. Proposed Architecture for 4-Controller Support

### 3.1 Pin Allocation (Suggested)
```c
#define NES_PIN_CLOCK   5
#define NES_PIN_LATCH   6
#define NES_PIN_DATA0   8
#define NES_PIN_DATA1   9
#define NES_PIN_DATA2  10
#define NES_PIN_DATA3  11
#define NES_MAX_PORTS   4
```

### 3.2 PIO Changes

**Option A – Single SM, 4-bit parallel input (preferred for efficiency)**
- Change `in pins, 1` → `in pins, 4`.
- Map four consecutive GPIOs as input pins (DATA0–DATA3).
- RX FIFO will contain 8 × 4-bit samples packed; post-process in IRQ handler to extract four independent 8-bit button values.
- Side-set remains identical (CLK + LATCH shared).

**Option B – Four separate state machines**
- More complex, uses more SM resources, not necessary.

**Recommended:** Option A. One SM, one program, minimal resource impact.

### 3.3 C Driver Changes (`nes_host.c`)

**Data structures**
- Replace single `tick_ctx_t` with array of 4 contexts or embed per-port state inside one context.
- Track per-port: `prev_buttons[4]`, `connected[4]`, `data_line_idle_high[4]`, debounce timers.

**Connection detection**
- Sample all four DATA lines in `nes_timer_cb()` (or keep single sample + extend logic).
- Maintain independent 500 ms debounce per line.

**PIO IRQ handler**
- Read one 32-bit word (or multiple words if >8 clocks).
- Unpack four 8-bit values from the 4-bit-wide shift stream.
- Update four `prev_buttons[]` entries.

**Task / event emission**
- `nes_host_task()` loops over 4 ports, emits up to 4 `input_event_t` messages.
- `dev_addr = 0xF0 + port` (0xF0 … 0xF3).
- `get_device_count()` returns 0–4.

**Interface registration**
- Remains one `InputInterface`; the interface now reports up to 4 devices.

### 3.4 App / Router Layer

- No changes required to `nes2usb/app.c` routing config.
- Router and player manager already handle multiple devices from the same source.
- `nes_input_interface.get_device_count` will now return the live count (0–4).

### 3.5 Configuration / Build

- Make `NES_MAX_PORTS` and pin macros configurable via `nes_host.h` or board config.
- Update docs and pinout diagrams.

---

## 4. Implementation Phases (Future)

1. **Phase 0** – Analysis & plan (this document).
2. **Phase 1** – PIO program update + 4-bit input, basic 4-port FIFO read.
3. **Phase 2** – Per-port connection state machine + debounce.
4. **Phase 3** – Event emission for 4 ports, `get_device_count` dynamic.
5. **Phase 4** – Testing with physical multitap / 4 controllers.
6. **Phase 5** – Documentation, pin mapping, example wiring.

---

## 5. Open Questions / Risks

- PIO instruction count / timing when sampling 4 bits vs 1 bit (verify cycle budget).
- Whether four consecutive GPIOs are free on target boards (Pico W, ESP32-S3, nRF).
- Pull-up configuration for all four DATA lines.
- Potential need for stronger external pull-ups on long multitap cables.

---

## 6. Files to Modify (High-Level)

- `src/native/host/nes/nes_host.pio`
- `src/native/host/nes/nes_host.c`
- `src/native/host/nes/nes_host.h`
- Possibly board pin definitions and documentation.

---

**End of Plan Document**  
Ready for implementation once approved.