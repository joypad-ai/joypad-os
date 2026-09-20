# 24g2usb_rp2350zero carrier

Carrier PCB for the [24g2usb](../../docs/apps/24g2usb.md) app: holds an nRF24L01+ mini SMD
module and receives a Waveshare RP2350-Zero on female header sockets. Board: 18.5 x 26.0mm
(the Zero's own footprint, plus a comfortable margin), 2-layer, 1.6mm FR4, 1oz copper.

> **Not yet fabricated or tested.** This board is DRC-clean and its netlist is asserted against
> the firmware's wiring table, but no physical board has been built from these files. Treat it as
> a first spin: do the fit-check print and the module pad-order check below before ordering.


Everything here is generated from [`../generate.py`](../generate.py) (`--variant rp2350zero`) --
edit the tables in that file and regenerate rather than hand-editing the `.kicad_pcb`. See its
module docstring for the routing method and why it looks the way it does.

**No KiCad GUI step is required before ordering.** `make gerbers zip` produces a gerber/drill zip
ready to upload as-is -- see "Ordering" below.

## Mounting orientation -- read before soldering anything

The RP2350-Zero plugs in from **above**: female sockets soldered to this carrier's **top** face,
Zero's male header pins pointing down into them. The carrier can also go **above** the Zero
instead -- solder male header pins into J1 pointing down out of the carrier's **bottom** face,
and plug those into a Zero that has female sockets fitted. Both ways put the module's antenna on
the carrier's top face; mounting the carrier above additionally gives the antenna clear air above
it with nothing overhead.

**Never flip the board over to mate it.** J1's two columns are not left-right symmetric (5V/GND/
3V3/GPxx on one side, GP0-GP8 on the other) -- mating flipped swaps them and puts 3V3 across the
SPI lines. Both faces are marked for this: "USB-C END" and the pin-1 marker (a square pad) are
present on **both** the top and bottom face, so whichever face you solder a header to, orientation
is never a guess. Through-hole pads are on `*.Cu` and `*.Mask` (both sides), so a socket or a
header solders cleanly from either face.

## BOM

See [`fab/bom.csv`](fab/bom.csv). Headline parts:

| Ref | Part | Notes |
|-----|------|-------|
| U1 | nRF24L01+ "Mini SMD" module, 1.27mm pitch, 12x18mm body | Hand-solder -- JLCPCB stocks none of these |
| J1 | 2x9 2.54mm female header, 15.24mm row spacing | Cut two 1x9 strips, or one 2x9 if you have it |
| C1 | 10uF 0805 | Bulk decoupling, right at the module's VCC/GND pads |
| C2 | 100nF 0805 | HF decoupling, right at the module's VCC/GND pads |

No RP2350-Zero on the BOM -- it's a separate board you already have, plugged into J1. This board
has no breakout header for the Zero's unused pins and no mounting holes -- see "Known
limitations."

## Fab order settings (JLCPCB or similar)

- 2 layers, 1.6mm thickness, 1oz copper, green soldermask, white silkscreen (defaults are fine)
- Min track/space 0.3mm/0.2mm (signal), 0.9mm GND -- inside every fab's standard capability
- Upload `fab/24g2usb_rp2350zero-gerbers.zip` (Gerber + Excellon drill, RS-274X)
- **This is a bare PCB order.** JLCPCB has no nRF24 module in its assembly catalogue -- do not
  select PCBA/assembly for U1

## Ordering

1. `make -C hardware/24g2usb_rp2350zero all` (or `generate drc gerbers zip render` individually)
   regenerates the board, checks it, and produces everything in `fab/` -- no GUI step needed.
2. Print `fab/24g2usb_rp2350zero-top-1to1.pdf` at 100% scale (**disable "fit to page" / "scale to
   fit" in your PDF viewer's print dialog**) and lay your actual RP2350-Zero and nRF24 module on
   it -- the one check the software genuinely can't do for you. The print includes the module's
   TRUE 12x18mm body (on the fabrication-reference layer, not silkscreen -- see "Why the module
   outline isn't on silkscreen" below), including the antenna overhang past the board edge.
   Confirm J1's pad spacing matches the Zero's header pins, and that the module you have is really
   a single row of 8 pads at 1.27mm pitch -- seller listings for "mini nRF24L01+ SMD" modules
   aren't fully standardized, and this is the one mistake DRC can't catch.
3. **Check the module's pad ORDER.** The board expects `+3.3V, GND, CE, CSN, SCK, MOSI, MISO,
   IRQ` along the row, VCC at the silkscreen-marked end. This is verified against the vendor
   dimension drawing for the common 12x18mm module (Sunrom model 3962,
   https://www.sunrom.com/p/rf-module-2-4ghz-nrf24l01-smd -- 12x18mm, 1.27mm pitch, first pad
   1.6mm from the edge), which is what this footprint was built from. These modules are NOT
   standardised between sellers, though, and carry no silkscreen on the pad row, so if yours came
   from elsewhere, probe for continuity between each pad and any bare ground on the module
   (crystal can, or the ground fill around the antenna): exactly one pad should beep, and it must
   be the second from the +3.3V end. If GND appears at the far end of the row instead, that module
   needs a different footprint. A swapped VCC/GND kills the module on first power-up.
4. Upload `fab/24g2usb_rp2350zero-gerbers.zip` to your fab of choice as a bare (non-assembled)
   2-layer board.

## Assembly order

1. **Module first** (U1) -- hardest part to align, easiest to rework before anything else is in
   the way. Tack one corner pad, check alignment against the printed fit-check, then solder the
   rest.
2. **C1, C2** -- right next to U1's VCC/GND pads.
3. **J1 socket** -- last, since it's the tallest part and the easiest to bump while soldering the
   others.
4. Solder male headers to your RP2350-Zero (if not already fitted) and plug it into J1.

## 3V3, not 5V

The module's VCC pad is fed from the Zero's 3V3 pin, never 5V -- nRF24 modules are not 5V
tolerant on their supply. This is called out on the silkscreen next to the module. If you're
hand-wiring anything extra, don't cross it.

## Why the module outline isn't on silkscreen

The nRF24 module's antenna genuinely overhangs the board edge on this carrier (by design -- see
"Verification performed" below for the exact figure) with no copper underneath it. A full-length
silkscreen outline of the module's true 12x18mm body would have to cross Edge.Cuts to show that
overhang, and KiCad's DRC flags silk that crosses the board edge (`silk_edge_clearance`). Rather
than ship that DRC note, the true body outline is drawn on `F.Fab` instead -- a fabrication
reference layer, not printed ink, so it isn't DRC-constrained against the edge -- and the
Makefile's 1:1 PDF/SVG exports include `F.Fab` specifically so the antenna overhang is still
visible and checkable when you print it. Silkscreen itself just carries the pad labels, reference
designators, and orientation markers; the pad copper marks the module's row on the physical board.

## Verification performed

- `kicad-cli pcb drc --severity-all --exit-code-violations`: **0 violations, 0 unconnected items,
  0 schematic-parity issues.**
- Netlist assertion in `generate.py` passes: J1 and U1's pad-to-net mapping is checked against the
  wiring table from `docs/apps/24g2usb.md` / `rf24g_host.h` before any file is written.
- Renders (`fab/*.png`, `fab/*.svg`) and the 1:1 PDF inspected: pads land on the expected nets,
  nothing routed under the module's antenna keepout, silkscreen legible, C1/C2 sit right next to
  U1's VCC/GND pads, board outline a plain rectangle, bottom face clean (no components, nothing
  obstructing the header holes). The module's antenna overhangs the board edge by about 7.75mm
  with no copper anywhere under it, confirmed both from the F.Fab outline in the 1:1 print and
  from the (automatically DRC-checked) antenna keepout zone.
- Gerber round-trip: unzipped `fab/24g2usb_rp2350zero-gerbers.zip` and confirmed F.Cu, B.Cu,
  F.Silkscreen, B.Silkscreen, F.Mask, B.Mask, Edge.Cuts, the Excellon drill file, and the job file
  are all present, and that F.Cu/B.Cu actually contain the designed copper (0.3mm signal traces,
  0.9mm GND traces, and the SMD/THT pad apertures) rather than an empty or pour-only layer.

## Known limitations

- **No GPIO breakout header and no M2 mounting holes.** An earlier revision of this board planned
  both, but at 18.5mm wide there wasn't room for either without conflicting with J1's own pads or
  routing -- see `generate.py`'s comments near `J1_POS`/mounting-hole for the specific numbers.
  The board relies on J1's own socket for mechanical support.
- **Ground is carried by explicit 0.9mm-wide tracks, not a copper pour.** `kicad-cli`'s headless
  zone filler segfaults in this Docker toolchain even for a single trivial zone (confirmed, not
  assumed), so a pour would ship silently unfilled in the gerbers -- what the fab builds wouldn't
  match the design. Routing GND as an explicit track sidesteps the problem entirely rather than
  requiring a manual GUI fill-and-reexport step before every order.
