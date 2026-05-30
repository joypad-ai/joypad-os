# minimal-c

Smallest possible libjoypad consumer. Plain ANSI C + HIDAPI command-line tool.
Enumerates connected controllers, opens the first match, reads HID reports in a
loop, and prints the decoded `input_event_t` to stdout.

**Purpose:** validate that libjoypad compiles and runs standalone outside firmware,
with no platform dependencies beyond HIDAPI and libc.

Status: not yet implemented (Task #8).
