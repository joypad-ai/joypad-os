# 24g2usb_pico2w carrier

Minimal carrier PCB for the [24g2usb](../../docs/apps/24g2usb.md) app on a Raspberry Pi Pico 2 W
(or Pico / Pico W / Pico 2 -- mechanically identical): holds an nRF24L01+ mini SMD module and
receives the Pico on two female header sockets covering only the pins actually needed. Board:
21 x 33mm (the Pico's own width, as short as the used pin span allows), 2-layer, 1.6mm FR4, 1oz
copper.

> **Not yet fabricated or tested, and the less settled of the two carriers.** This board is
> DRC-clean and its netlist is asserted against the firmware's wiring table, but nothing has been
> built from these files. Specific to this variant: the module's antenna overhangs the same board
> edge the Pico's USB-C connector occupies, and that clearance has only been checked in the 3D
> render, not against a real cable -- verify it with your own cable on the fit-check print. The
> [RP2350-Zero carrier](../24g2usb_rp2350zero/README.md) overhangs away from its USB end and is
> the better-understood board of the two.


Generated from [`../generate.py`](../generate.py) (`--variant pico2w`) -- same generator, same
module footprint, and the same routing method as the RP2350-Zero carrier; see that file's
docstring and the sibling [`24g2usb_rp2350zero/README.md`](../24g2usb_rp2350zero/README.md) for
the shared background. This README covers only what's specific to this board.

**No KiCad GUI step is required before ordering.** `make gerbers zip` produces a gerber/drill zip
ready to upload as-is -- see "Ordering" below.

## Why it's this shape

Only 16 of the Pico's 40 pins are used: pins 1-11 (GP0-GP8 plus two GND, all six SPI/IRQ
signals live in here) on J1, and pins 36-40 (3V3(OUT), 3V3_EN, GND, VSYS, VBUS -- only 3V3 and
the second GND are actually wired) on J3, purely to reach 3V3 and add a second mounting/support
point at the same end of the board. Both spans sit at the USB end of the Pico, so the carrier
only needs to be as tall as that span -- the rest of the Pico (about 21mm) cantilevers out past
the carrier's far edge, supported only by the header pins at this end.

The module sits in the ~13mm channel between J1 and J3, its antenna overhanging the carrier's
**USB-end edge**, in the same free-air gap the Pico's own USB-C connector needs anyway -- check
this against your actual connector and cable in the fit-check step below; it's the one clearance
this design can't verify for you from a render. C1/C2 (decoupling) sit in the same channel, south
of the module's pad-label text, between J1's CE row and the antenna keepout.

## Mounting orientation -- read before soldering anything

Same convention as the RP2350-Zero carrier, and the same warning applies: the Pico plugs in from
**above** (female sockets on the carrier's top face) or the carrier plugs into the Pico from
**below** (male headers soldered pointing down out of the carrier's bottom face, into a Pico
fitted with sockets) -- **never flip the carrier over to mate it**, or J1/J3's asymmetric pinouts
swap and 3V3 ends up across the SPI lines. "USB END" and each socket's pin-1 marker are present on
both the top and bottom face, so either mounting direction is unambiguous without holding the
board up to a light. Through-hole pads are on `*.Cu` and `*.Mask` on both sides -- a socket or a
header solders cleanly from either face, and nothing sits on the bottom face to obstruct a header
soldered from underneath.

## BOM

See [`fab/bom.csv`](fab/bom.csv).

| Ref | Part | Notes |
|-----|------|-------|
| U1 | nRF24L01+ "Mini SMD" module, 1.27mm pitch, 12x18mm body | Hand-solder -- JLCPCB stocks none of these |
| J1 | 1x11 2.54mm female header | Cut from a standard break-away strip |
| J3 | 1x5 2.54mm female header | Cut from a standard break-away strip |
| C1 | 10uF 0805 | Bulk decoupling, at U1's VCC/GND pads |
| C2 | 100nF 0805 | HF decoupling, at U1's VCC/GND pads |

No Pico 2 W on the BOM -- it's a separate board, plugged into J1/J3.

## Fab order settings (JLCPCB or similar)

- 2 layers, 1.6mm thickness, 1oz copper -- same as the RP2350-Zero carrier
- Min track/space 0.3mm/0.2mm (signal), 0.9mm GND
- Upload `fab/24g2usb_pico2w-gerbers.zip`
- Bare PCB order -- U1 is hand-soldered, not JLCPCB-assembled

## Ordering

1. `make -C hardware/24g2usb_pico2w all` (or `generate drc gerbers zip render` individually)
   regenerates the board, checks it, and produces everything in `fab/` -- no GUI step needed.
2. Print `fab/24g2usb_pico2w-top-1to1.pdf` at 100% scale (turn off "fit to page" in your PDF
   viewer) and lay your actual Pico 2 W and nRF24 module on it. The print includes the module's
   TRUE 12x18mm body (on the fabrication-reference layer, not silkscreen -- see the RP2350-Zero
   README's "Why the module outline isn't on silkscreen" for why). Check J1/J3's pad spacing
   against the Pico's own header holes, confirm the module is really a single row of 8 pads at
   1.27mm pitch, and -- specific to this board -- confirm the antenna overhang doesn't foul the
   USB-C cable you actually plan to use.
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
4. Upload `fab/24g2usb_pico2w-gerbers.zip` to your fab of choice as a bare (non-assembled) 2-layer
   board.

## Assembly order

1. **Module first** (U1) -- tack one corner, check against the fit-check print, then finish
   soldering.
2. **C1, C2** -- right next to U1's VCC/GND pads.
3. **J1 and J3 sockets** (or headers, if mounting the carrier above the Pico) -- last, since
   they're the tallest parts.
4. Plug in the Pico 2 W (with headers fitted, if the carrier has sockets).

## 3V3, not 5V

VCC is fed from the Pico's 3V3(OUT) pin (J3 pin 36), never VBUS/VSYS -- called out on the
silkscreen next to the module.

## Verification performed

- `kicad-cli pcb drc --severity-all --exit-code-violations`: **0 violations, 0 unconnected items,
  0 schematic-parity issues.**
- Netlist assertion in `generate.py` passes: J1+J3 (merged) and U1's pad-to-net mapping checked
  against the wiring table before any file is written.
- Renders and the 1:1 PDF inspected: pads on the expected nets, nothing routed under the antenna's
  strict keepout, module correctly centred in the channel between J1 and J3, C1/C2 right next to
  U1's VCC/GND pads, bottom face clean. The module's antenna overhangs the board edge by about
  6.75mm with no copper anywhere under it, confirmed both from the F.Fab outline in the 1:1 print
  and from the (automatically DRC-checked) antenna keepout zone.
- Gerber round-trip: unzipped `fab/24g2usb_pico2w-gerbers.zip` and confirmed F.Cu, B.Cu,
  F.Silkscreen, B.Silkscreen, F.Mask, B.Mask, Edge.Cuts, the Excellon drill file, and the job file
  are all present, and that F.Cu/B.Cu actually contain the designed copper (0.3mm signal traces,
  0.9mm GND traces, and the SMD/THT pad apertures) rather than an empty or pour-only layer.

## Known limitations

- **Ground is carried by explicit 0.9mm-wide tracks, not a copper pour** -- same reasoning as the
  RP2350-Zero carrier: `kicad-cli`'s headless zone filler segfaults in this Docker toolchain, so a
  pour would ship silently unfilled in the gerbers. An explicit track sidesteps the problem
  entirely.
- **Silkscreen labels are tight.** J1's pin labels (GP0..GP8, GND) and J3's (abbreviated to
  VB/VS/GN/EN/3V to fit the 2.54mm pitch rotated vertical) sit close to their own pin. Full pin
  names are in the BOM and the table above -- read the silkscreen with this file open if a label
  is ambiguous.
- **Antenna/cable clearance** is the one physical check this design leans on you for -- see the
  fit-check step.
