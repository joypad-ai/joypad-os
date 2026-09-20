#!/usr/bin/env python3
"""generate.py -- builds the 24g2usb carrier boards from one explicit
component/net table per variant, so each board is reviewable as a text diff
and regenerable from scratch. Run inside the kicad/kicad:9.0-full container
(see Makefile in each variant directory):

    python3 generate.py --variant rp2350zero
    python3 generate.py --variant pico2w

Both variants carry the SAME nRF24L01+ mini SMD module (single row, 8 pads
@ 1.27mm pitch, confirmed 12x18mm body) wired to the same 8 signals; only
the host-board socket and layout differ. The module footprint and this
generator are shared (hardware/lib/joypad.pretty, hardware/generate.py);
each variant gets its own output directory.

Routing method (see README "Routing" section for the full rationale): every
point-to-point connection is drawn as an L-shape across two layers with a
via at the corner -- a B.Cu leg at the SOURCE pad's own (already-unique) Y,
then a via, then an F.Cu leg at the TARGET pad's own (already-unique) X.
Because every same-layer leg sits at a coordinate that is unique among the
nets it's grouped with, same-layer legs are parallel lines that provably
never cross, and different-layer legs can never short regardless of X/Y
overlap. This replaces an earlier same-layer "staircase" scheme that DRC
caught crossing itself in >50 places -- worth remembering if this script is
extended.

Units: millimetres everywhere; converted to KiCad internal units
(nanometres) only at the point of use via MM().
"""
import argparse
import os
import sys
import faulthandler
faulthandler.enable()

import pcbnew
from pcbnew import VECTOR2I

MM = pcbnew.FromMM

HERE = os.path.dirname(os.path.abspath(__file__))
LIB_DIR = os.path.join(HERE, "lib", "joypad.pretty")

# ===========================================================================
# WIRING TABLE -- authoritative, copied from docs/apps/24g2usb.md and
# src/native/host/rf24g/rf24g_host.h. Both variants are checked against it.
# ===========================================================================
WIRING = {
    "MISO": "GP0", "CE": "GP4", "CSN": "GP5", "SCK": "GP6",
    "MOSI": "GP7", "IRQ": "GP8", "VCC": "3V3", "GND": "GND",
}
U1_ORDER = ["VCC", "GND", "CE", "CSN", "SCK", "MOSI", "MISO", "IRQ"]


def net_of(pin_name):
    for fn, gp in WIRING.items():
        if gp == pin_name:
            return f"{pin_name}_{fn}" if pin_name.startswith("GP") else pin_name
    return pin_name


CORE_NETS = {sig: net_of(WIRING[sig]) for sig in U1_ORDER}
U1_NET = {i + 1: CORE_NETS[sig] for i, sig in enumerate(U1_ORDER)}

TRACK_WIDTH = 0.3
CLEARANCE = 0.2
MIN_DRILL = 0.3
VIA_DIAM = 0.6
VIA_DRILL = 0.3

U1_PITCH = 1.27
U1_PAD_SIZE = (0.95, 2.5)
U1_BODY = (12.0, 18.0)          # confirmed body size (W, len)
U1_BODY_MARGIN = 0.5


# ===========================================================================
# FOOTPRINT HELPERS (shared)
# ===========================================================================
def new_footprint(board, ref, fpid_name):
    fp = pcbnew.FOOTPRINT(board)
    fp.SetReference(ref)
    fp.SetFPID(pcbnew.LIB_ID("joypad", fpid_name))
    fp.Reference().SetLayer(pcbnew.F_SilkS)
    fp.Reference().SetVisible(True)
    fp.Value().SetVisible(False)
    return fp


def add_pad(fp, number, at_xy, size_xy, shape, attr, layerset, drill=None):
    pad = pcbnew.PAD(fp)
    pad.SetNumber(str(number))
    pad.SetPosition(VECTOR2I(MM(at_xy[0]), MM(at_xy[1])))
    pad.SetSize(VECTOR2I(MM(size_xy[0]), MM(size_xy[1])))
    pad.SetShape(shape)
    pad.SetAttribute(attr)
    pad.SetLayerSet(layerset)
    if drill is not None:
        pad.SetDrillSize(VECTOR2I(MM(drill), MM(drill)))
    fp.Add(pad)
    return pad


MIN_SILK = 0.8  # KiCad's default min silk text height DRC rule


def add_silk_text(fp, text, at_xy, size=MIN_SILK, thickness=0.15, layer=pcbnew.F_SilkS,
                   angle=0, h_align=pcbnew.GR_TEXT_H_ALIGN_CENTER,
                   v_align=pcbnew.GR_TEXT_V_ALIGN_CENTER):
    t = pcbnew.PCB_TEXT(fp)
    t.SetText(text)
    t.SetPosition(VECTOR2I(MM(at_xy[0]), MM(at_xy[1])))
    t.SetLayer(layer)
    t.SetTextSize(VECTOR2I(MM(size), MM(size)))
    t.SetTextThickness(MM(thickness))
    if angle:
        t.SetTextAngle(pcbnew.EDA_ANGLE(angle, pcbnew.DEGREES_T))
    t.SetHorizJustify(h_align)
    t.SetVertJustify(v_align)
    if layer in (pcbnew.B_SilkS, pcbnew.B_Cu, pcbnew.B_Fab):
        t.SetMirrored(True)
    fp.Add(t)
    return t


def add_bottom_orientation_marker(fp, line_xy, text_xy, text, line_width=0.3,
                                   text_size=0.8, text_thickness=0.15):
    """Mirrors a front-side USB-end / pin-1 style marker onto B.SilkS at the
    SAME local coordinates, so a male header soldered from underneath can
    still be oriented correctly -- required because the board must never
    be flipped over to mate (that would swap the two header rows and put
    3V3 on the SPI pins)."""
    (x0, y0), (x1, y1) = line_xy
    add_silk_line(fp, (x0, y0), (x1, y1), width=line_width, layer=pcbnew.B_SilkS)
    add_silk_text(fp, text, text_xy, size=text_size, thickness=text_thickness,
                  layer=pcbnew.B_SilkS)


def add_silk_line(fp, start_xy, end_xy, width=0.15, layer=pcbnew.F_SilkS):
    s = pcbnew.PCB_SHAPE(fp, pcbnew.SHAPE_T_SEGMENT)
    s.SetStart(VECTOR2I(MM(start_xy[0]), MM(start_xy[1])))
    s.SetEnd(VECTOR2I(MM(end_xy[0]), MM(end_xy[1])))
    s.SetWidth(MM(width))
    s.SetLayer(layer)
    fp.Add(s)


def add_silk_rect(fp, x0, y0, x1, y1, width=0.15, layer=pcbnew.F_SilkS):
    add_silk_line(fp, (x0, y0), (x1, y0), width, layer)
    add_silk_line(fp, (x1, y0), (x1, y1), width, layer)
    add_silk_line(fp, (x1, y1), (x0, y1), width, layer)
    add_silk_line(fp, (x0, y1), (x0, y0), width, layer)


def add_fp_zone(fp, x0, y0, x1, y1, allow_tracks, allow_vias, name=""):
    zone = pcbnew.ZONE(fp.GetBoard())
    lset = pcbnew.LSET()
    lset.AddLayer(pcbnew.F_Cu)
    lset.AddLayer(pcbnew.B_Cu)
    zone.SetLayerSet(lset)
    outline = zone.Outline()
    outline.NewOutline()
    for (x, y) in [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]:
        outline.Append(VECTOR2I(MM(x), MM(y)))
    zone.SetIsRuleArea(True)
    zone.SetDoNotAllowCopperPour(True)
    zone.SetDoNotAllowTracks(not allow_tracks)
    zone.SetDoNotAllowVias(not allow_vias)
    zone.SetDoNotAllowPads(False)
    zone.SetDoNotAllowFootprints(False)
    if name:
        zone.SetZoneName(name)
    fp.Add(zone)
    return zone


def build_nrf24_variant_a(board, show_title=True):
    """nRF24L01+ mini SMD module: single row of 8 castellated pads at
    1.27mm pitch, confirmed 12x18mm body (also fits the 12x19mm Ebyte
    E01-ML01S). Local frame: origin at the centre of the pad row; pads at
    y=0; antenna end is -Y."""
    fp = new_footprint(board, "U1", "nRF24L01_Mini_SMD_8P127")
    fp.SetLibDescription(
        "nRF24L01+ 'Mini SMD' module, single row of 8 castellated pads at "
        "1.27mm pitch, confirmed 12x18mm body (also fits the 12x19mm "
        "Ebyte E01-ML01S). Pad order VCC, GND, CE, CSN, SCK, MOSI, MISO, "
        "IRQ. Antenna end is -Y from the pad row -- keep that end, plus a "
        "small margin, completely free of copper (pour, tracks, vias).")

    half_w, body_len = U1_BODY[0] / 2.0, U1_BODY[1]
    pad_half = U1_PAD_SIZE[1] / 2.0
    body_north = pad_half - body_len

    # Pad labels and the part title sit just SOUTH of the pad row (toward
    # the header, in the open board area) rather than up at the antenna
    # end -- with the outline now drawn at its true 18mm length (below),
    # "north of the pads" is out past the antenna tip, off the board
    # entirely on any layout with a real overhang.
    for i, sig in enumerate(U1_ORDER):
        x = (i - 3.5) * U1_PITCH
        add_pad(fp, i + 1, (x, 0), U1_PAD_SIZE, pcbnew.PAD_SHAPE_RECT,
                pcbnew.PAD_ATTRIB_SMD, pcbnew.PAD.SMDMask())
        add_silk_text(fp, sig, (x, pad_half + 1.7), size=MIN_SILK, thickness=0.15,
                      angle=90)

    # TRUE 12x18mm body goes on F.Fab, not silkscreen. An earlier revision
    # drew the full body on F.SilkS and accepted the resulting
    # silk_edge_clearance DRC note on the overhanging (antenna) portion,
    # since a full-length silk outline necessarily crosses Edge.Cuts on
    # both variants (the module genuinely overhangs the board). F.Fab is
    # not a printed-ink layer and isn't DRC-constrained against the board
    # edge, so it can carry the true outline -- including the overhanging
    # antenna end -- without a violation, and it's exactly what the 1:1
    # fit-check PDF is for (see each board's Makefile, which now exports
    # F.Fab alongside F.Cu/F.Silkscreen/Edge.Cuts for that print). No
    # separate silk outline is drawn at all -- a south alignment stroke
    # was tried but it collides with the pad-label text directly above it
    # (silk_overlap) on both variants, and the pad copper itself already
    # marks the row on the physical board -- so the pads plus the per-pad
    # labels are the on-board (silk) reference, and F.Fab carries the
    # true body for the print fit-check.
    body_south = pad_half
    add_silk_rect(fp, -half_w, body_north, half_w, body_south, width=0.1, layer=pcbnew.F_Fab)
    if show_title:
        # Dropped on boards too crowded south of U1 to fit it (the
        # RP2350-Zero carrier, where C1/C2 and a board-level warning both
        # live in that same narrow strip) -- the U1 reference designator
        # alone still identifies the part.
        add_silk_text(fp, "U1 nRF24L01+", (0, pad_half + 5.0),
                      size=MIN_SILK, thickness=0.15)
    fp.Reference().SetPosition(VECTOR2I(MM(0), MM(pad_half + 3.7)))

    # Two-tier keepout. The PCB antenna trace itself is only ever in the
    # far end of the module, not the whole 18mm body (the chip, crystal
    # and passives take up the near end) -- so:
    #  - ANTENNA_LEN (the far ~10mm, including the overhang) gets the full
    #    "no copper at all" rule: no pour, no tracks, no vias.
    #  - the rest of the body (near end, above the pads) only forbids
    #    ground pour and vias; short signal routes are allowed to cross
    #    under it. This is what makes a route reachable at all on the
    #    Pico 2 W board, where U1 sits between two header columns and some
    #    source rows are north of U1's own pad row -- a same-Y crossing
    #    has nowhere to go except under the module's near end.
    antenna_len = 10.0
    antenna_boundary = body_north + (body_len - antenna_len)
    add_fp_zone(fp, -half_w - U1_BODY_MARGIN, body_north - U1_BODY_MARGIN,
                half_w + U1_BODY_MARGIN, antenna_boundary,
                allow_tracks=False, allow_vias=False, name="U1_ANTENNA_KEEPOUT")
    add_fp_zone(fp, -half_w - U1_BODY_MARGIN, antenna_boundary,
                half_w + U1_BODY_MARGIN, -pad_half,
                allow_tracks=True, allow_vias=True, name="U1_BODY_NOPOUR")
    return fp


def build_0805_cap(board, ref, pitch=1.9, pad_size=(1.2, 1.3)):
    fp = new_footprint(board, ref, "C_0805_Hand")
    half = pitch / 2.0
    add_pad(fp, 1, (-half, 0), pad_size, pcbnew.PAD_SHAPE_RECT,
            pcbnew.PAD_ATTRIB_SMD, pcbnew.PAD.SMDMask())
    add_pad(fp, 2, (half, 0), pad_size, pcbnew.PAD_SHAPE_RECT,
            pcbnew.PAD_ATTRIB_SMD, pcbnew.PAD.SMDMask())
    add_silk_line(fp, (-half - 0.3, -0.9), (half + 0.3, -0.9))
    fp.Reference().SetPosition(VECTOR2I(MM(0), MM(1.6)))
    fp.Reference().SetTextSize(VECTOR2I(MM(0.8), MM(0.8)))
    fp.Reference().SetTextThickness(MM(0.13))
    return fp


def build_mount_hole(board, ref, drill=2.2):
    fp = new_footprint(board, ref, "MountingHole_M2")
    pad = pcbnew.PAD(fp)
    pad.SetNumber("1")
    pad.SetPosition(VECTOR2I(0, 0))
    pad.SetSize(VECTOR2I(MM(drill), MM(drill)))
    pad.SetShape(pcbnew.PAD_SHAPE_CIRCLE)
    pad.SetAttribute(pcbnew.PAD_ATTRIB_NPTH)
    pad.SetDrillSize(VECTOR2I(MM(drill), MM(drill)))
    pad.SetLayerSet(pcbnew.PAD.UnplatedHoleMask())
    fp.Add(pad)
    fp.Reference().SetVisible(False)
    return fp


def build_tht_socket(board, ref, fpid, pin_layout, pitch=2.54, pad_size=1.8,
                      drill=1.0, label_offset=2.4, label_side=None,
                      label_angle=0, label_inward=False, show_pin_labels=True):
    """Generic single- or multi-column THT socket. pin_layout: dict
    {number: (x, y, label)} in the footprint's local frame. label_side
    forces every label to one side (+1 east / -1 west) -- needed for a
    single-column socket (all pins at local x=0) placed near a board edge,
    where the default "west if x<=0" rule would push every label off the
    board. label_angle=90 rotates labels vertical, trading their length
    for their (much smaller) height in X -- needed on a board too narrow
    for a horizontal label between the pin column and the edge.
    label_inward flips the default per-column side (west for x<=0, east
    for x>0) so both columns' labels point toward the board's centre
    instead of out toward its edges -- needed when a 2-column socket's own
    outer edge is only ~1.6mm from the board edge, too narrow for an
    outward label to fit without running off the board."""
    fp = new_footprint(board, ref, fpid)
    for num, (x, y, label) in pin_layout.items():
        shape = pcbnew.PAD_SHAPE_RECT if num == 1 else pcbnew.PAD_SHAPE_CIRCLE
        add_pad(fp, num, (x, y), (pad_size, pad_size), shape,
                pcbnew.PAD_ATTRIB_PTH, pcbnew.PAD.PTHMask(), drill=drill)
        if show_pin_labels:
            if label_side is not None:
                side = label_side
            else:
                side = -1 if x <= 0 else 1
                if label_inward:
                    side = -side
            add_silk_text(fp, label, (x + side * label_offset, y), size=MIN_SILK,
                          thickness=0.15, angle=label_angle)
    xs = [p[0] for p in pin_layout.values()]
    ys = [p[1] for p in pin_layout.values()]
    ref_side = label_side if label_side is not None else 1
    ref_x = (max(xs) if ref_side > 0 else min(xs)) + ref_side * (label_offset + 0.8)
    fp.Reference().SetPosition(VECTOR2I(MM(ref_x), MM(min(ys) - 2.0)))
    fp.Reference().SetTextSize(VECTOR2I(MM(0.8), MM(0.8)))
    fp.Reference().SetTextThickness(MM(0.13))
    return fp


# ===========================================================================
# BOARD-LEVEL ROUTING HELPERS
# ===========================================================================
def make_board_context():
    board = pcbnew.BOARD()
    ds = board.GetDesignSettings()
    ds.m_TrackMinWidth = MM(TRACK_WIDTH)
    ds.m_MinClearance = MM(CLEARANCE)
    ds.m_MinThroughDrill = MM(MIN_DRILL)
    ds.SetCopperLayerCount(2)
    try:
        ds.SetBoardThickness(MM(1.6))
    except AttributeError:
        pass
    return board


def netlist_assertion(pad_nets_by_ref):
    """pad_nets_by_ref: {ref: {pad_number: net_name}}. Checks that, for
    every signal in WIRING, every referenced component's pad on that net is
    labelled consistently. Fails loudly on any mismatch."""
    errors = []
    for ref, pad_nets in pad_nets_by_ref.items():
        for sig, expected_net in CORE_NETS.items():
            pads_on_net = [p for p, n in pad_nets.items() if n == expected_net]
            if not pads_on_net:
                errors.append(f"{ref} has no pad on net {expected_net!r} (signal {sig})")
    if errors:
        sys.stderr.write("NETLIST ASSERTION FAILED against wiring table:\n")
        for e in errors:
            sys.stderr.write(f"  - {e}\n")
        sys.exit(1)
    print(f"Netlist assertion OK: {len(CORE_NETS)} signals verified against "
          f"the wiring table for {', '.join(pad_nets_by_ref)}.")


class Router:
    def __init__(self, board, nets):
        self.board = board
        self.nets = nets

    def net(self, name):
        return self.nets[name] if name else None

    def track(self, net_name, points, layer=pcbnew.F_Cu, width=TRACK_WIDTH):
        ni = self.net(net_name)
        for (x0, y0), (x1, y1) in zip(points, points[1:]):
            if (x0, y0) == (x1, y1):
                continue
            t = pcbnew.PCB_TRACK(self.board)
            t.SetStart(VECTOR2I(MM(x0), MM(y0)))
            t.SetEnd(VECTOR2I(MM(x1), MM(y1)))
            t.SetWidth(MM(width))
            t.SetLayer(layer)
            t.SetNet(ni)
            self.board.Add(t)

    def via(self, net_name, xy, diam=VIA_DIAM, drill=VIA_DRILL):
        v = pcbnew.PCB_VIA(self.board)
        v.SetPosition(VECTOR2I(MM(xy[0]), MM(xy[1])))
        v.SetWidth(MM(diam))
        v.SetDrill(MM(drill))
        v.SetNet(self.net(net_name))
        v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
        self.board.Add(v)

    def route_L(self, net_name, src_xy, dst_xy, bus_layer=pcbnew.B_Cu,
                leg_layer=pcbnew.F_Cu, width=TRACK_WIDTH):
        """Two-layer L-route: B.Cu leg at src_xy's Y, via at the corner,
        F.Cu leg at dst_xy's X. Safe against any other route in the same
        group as long as EITHER each route's src Y is unique OR each
        route's dst X is unique (true for every use in this script)."""
        corner = (dst_xy[0], src_xy[1])
        self.track(net_name, [src_xy, corner], layer=bus_layer, width=width)
        self.via(net_name, corner)
        self.track(net_name, [corner, dst_xy], layer=leg_layer, width=width)

    def route_jog(self, net_name, src_xy, dst_xy, jog_x, bus_layer=pcbnew.B_Cu,
                  leg_layer=pcbnew.F_Cu):
        """Like route_L, but the F.Cu leg runs at a caller-chosen unique
        jog_x instead of dst_xy's X (for groups where dst X is NOT unique,
        e.g. a header column shared by several nets), with a short final
        F.Cu jog into the real pad. Safe as long as jog_x is unique among
        the group AND jog_x values are assigned in the same order as the
        nets' target row/position (monotonic -- see README)."""
        corner = (jog_x, src_xy[1])
        self.track(net_name, [src_xy, corner], layer=bus_layer)
        self.via(net_name, corner)
        self.track(net_name, [corner, (jog_x, dst_xy[1]), dst_xy], layer=leg_layer)

    def route_jog_dodge(self, net_name, src_xy, dst_xy, jog_x, dodge_dy,
                        bus_layer=pcbnew.B_Cu, leg_layer=pcbnew.F_Cu):
        """Like route_jog, but first steps dodge_dy away from src_xy's own
        Y (same layer, at src_xy's own X) before the long bus run. Use this
        when the bus would otherwise cross a THIRD component's pad column
        at exactly src_xy's Y (e.g. two header columns that share the same
        row spacing, so a bus at any one row's Y risks hitting the other
        column's pad at that identical Y)."""
        dodge = (src_xy[0], src_xy[1] + dodge_dy)
        corner = (jog_x, dodge[1])
        self.track(net_name, [src_xy, dodge, corner], layer=bus_layer)
        self.via(net_name, corner)
        self.track(net_name, [corner, (jog_x, dst_xy[1]), dst_xy], layer=leg_layer)


def pad_xy(fp, num):
    p = fp.FindPadByNumber(str(num)).GetPosition()
    return (pcbnew.ToMM(p.x), pcbnew.ToMM(p.y))


def save_library_footprints(board_ctor, entries):
    """entries: list of (builder_fn, libname). Writes each into LIB_DIR,
    skipping any .kicad_mod that already exists there (so running this for
    one variant doesn't require re-running it for the other -- both share
    LIB_DIR, and FootprintLibCreate() crashes the whole process if the
    directory already exists, so it's only called the very first time)."""
    io = pcbnew.PCB_IO_KICAD_SEXPR()
    if not os.path.isdir(LIB_DIR):
        io.FootprintLibCreate(LIB_DIR)
    lib_board = board_ctor()
    for builder, libname in entries:
        if os.path.exists(os.path.join(LIB_DIR, libname + ".kicad_mod")):
            continue
        fp = builder(lib_board)
        fp.SetFPID(pcbnew.LIB_ID("joypad", libname))
        io.FootprintSave(LIB_DIR, fp)
        print(f"wrote lib/joypad.pretty/{libname}.kicad_mod")


def board_text(board, text, xy, size=1.0, thickness=0.15, layer=pcbnew.F_SilkS,
               h_align=pcbnew.GR_TEXT_H_ALIGN_LEFT, angle=0):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(text)
    t.SetPosition(VECTOR2I(MM(xy[0]), MM(xy[1])))
    t.SetTextSize(VECTOR2I(MM(size), MM(size)))
    t.SetTextThickness(MM(thickness))
    t.SetLayer(layer)
    t.SetHorizJustify(h_align)
    if angle:
        t.SetTextAngle(pcbnew.EDA_ANGLE(angle, pcbnew.DEGREES_T))
    if layer in (pcbnew.B_SilkS, pcbnew.B_Cu, pcbnew.B_Fab):
        t.SetMirrored(True)
    board.Add(t)
    return t


def add_outline(board, w, h):
    outline = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_RECTANGLE)
    outline.SetStart(VECTOR2I(0, 0))
    outline.SetEnd(VECTOR2I(MM(w), MM(h)))
    outline.SetLayer(pcbnew.Edge_Cuts)
    outline.SetWidth(MM(0.15))
    board.Add(outline)


def add_gnd_pour(board, ni_gnd, w, h, margin=1.0):
    pour = pcbnew.ZONE(board)
    pour.SetLayer(pcbnew.B_Cu)
    pour.SetNet(ni_gnd)
    pour.SetZoneName("GND_POUR_BOTTOM")
    o = pour.Outline()
    o.NewOutline()
    for (x, y) in [(margin, margin), (w - margin, margin),
                   (w - margin, h - margin), (margin, h - margin)]:
        o.Append(VECTOR2I(MM(x), MM(y)))
    pour.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
    board.Add(pour)


def make_nets(board, names):
    nets = {}
    for name in sorted(set(names) - {""}):
        ni = pcbnew.NETINFO_ITEM(board, name)
        board.Add(ni)
        nets[name] = ni
    return nets


def write_bom(outdir, rows):
    """rows: list of (ref, qty, value, footprint, description, jlc_note)."""
    import csv
    with open(os.path.join(outdir, "fab", "bom.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Reference", "Qty", "Value", "Footprint", "Description", "Notes"])
        for row in rows:
            w.writerow(row)
    print(f"wrote {outdir}/fab/bom.csv")


def write_fp_lib_table(outdir):
    """Project-level fp-lib-table registering the shared 'joypad' library,
    so kicad-cli (DRC's footprint-library check, footprint loading for
    renders, etc.) can resolve LIB_ID("joypad", ...) references."""
    content = (
        '(fp_lib_table\n'
        '  (version 7)\n'
        '  (lib (name "joypad")(type "KiCad")'
        '(uri "${KIPRJMOD}/../lib/joypad.pretty")(options "")(descr "Shared 24g2usb footprints"))\n'
        ')\n'
    )
    with open(os.path.join(outdir, "fp-lib-table"), "w") as f:
        f.write(content)


PAGE_MARGIN_Y = 10.0  # see translate_board() below


def translate_board(board, dx, dy):
    """Moves EVERY item on the board (footprints -- which carries their
    pads, silk, and any footprint-owned zones like U1's antenna keepouts
    along with them -- plus top-level tracks/vias/drawings/zones) by a
    constant offset. Needed because kicad-cli's SVG/PDF page export shares
    ONE coordinate system between the page and the board: the page's own
    origin (0,0) is the top-left of the sheet, and board content is drawn
    at its own literal coordinates on that same sheet -- there is no
    auto-centering. This board's local origin is (0,0) at the Edge.Cuts
    corner (by construction, in add_outline), but U1's antenna keepout and
    F.Fab body legitimately extend to NEGATIVE Y (the antenna overhangs
    the board on both variants -- that's the point). Left unshifted, that
    entire overhanging portion -- the one thing the 1:1 fit-check PDF
    exists to show -- silently falls off the top of the page and prints
    as nothing (confirmed by rendering the PDF before this fix: the page
    started exactly at the board's row-1 header pins with a blank margin
    above, no antenna outline at all). Shifting every item down by
    PAGE_MARGIN_Y (comfortably more than the ~7-8mm overhang on either
    variant) moves the whole board, overhang included, onto the page.
    This is purely a page-coordinate cosmetic: gerbers/drill are referenced
    to the board's own geometry (fabs auto-detect origin from Edge.Cuts),
    so a uniform translate of every item has zero effect on the
    manufactured board."""
    v = VECTOR2I(MM(dx), MM(dy))
    for fp in board.Footprints():
        fp.Move(v)
    for tr in board.Tracks():
        tr.Move(v)
    for dw in board.Drawings():
        dw.Move(v)
    for z in board.Zones():
        z.Move(v)


def finish_and_save(board, out_path):
    """NOTE: ZONE_FILLER.Fill() reliably segfaults when pcbnew is driven
    from plain `python3` in the kicad/kicad:9.0-full container -- reproduced
    even on a single trivial zone with no footprints involved (see the repo
    history for generate.py / this comment). Zones are saved UNFILLED;
    `kicad-cli pcb export gerbers` and `kicad-cli pcb drc` both fill zones
    themselves before doing anything with them, so this does not affect any
    deliverable -- only the raw .kicad_pcb shows unfilled zone outlines if
    opened in the GUI until the user re-fills (B) once."""
    pcbnew.SaveBoard(out_path, board)
    print(f"Saved {out_path}")


# ===========================================================================
# VARIANT: rp2350zero
# ===========================================================================
def build_rp2350zero(outdir):
    """Rev C: board matches the Zero's own 18x23.5mm footprint (grown to
    18.5x26mm for margin), with U1 dropped into the channel between J1's
    two pad rows -- the same arrangement the Pico 2 W board uses between
    J1 and J3. (Two earlier revisions: rev A was 50x65mm with U1 pushed
    off-center of J1 entirely, needed to keep vias off J1's own pads under
    the general L-route scheme's via-near-source-pad constraint; rev B
    shrank to 30x49mm with U1 stacked above J1 instead of alongside it.
    Both worked, but this is the one that actually matches the MCU
    board's own size, per explicit instruction.)"""
    ZERO_LEFT = ["5V", "GND", "3V3", "GP29", "GP28", "GP27", "GP26", "GP15", "GP14"]
    ZERO_RIGHT = ["GP0", "GP1", "GP2", "GP3", "GP4", "GP5", "GP6", "GP7", "GP8"]
    J1_PITCH = 2.54
    J1_ROW_SPACING = 15.24

    def build_j1(board):
        half_row = J1_ROW_SPACING / 2.0
        layout = {}
        for idx, name in enumerate(ZERO_LEFT):
            layout[idx + 1] = (-half_row, (4 - idx) * J1_PITCH, name)
        for idx, name in enumerate(ZERO_RIGHT):
            layout[10 + idx] = (half_row, (4 - idx) * J1_PITCH, name)
        # Per-pin labels are dropped on this board (show_pin_labels=False):
        # at 18.5mm wide there's no room for an outward label without
        # running off the board edge, and no room for an inward one
        # without overlapping U1's own silk in the channel. Full pinout is
        # in the BOM/README/build guide instead; PIN1 and USB-C END below
        # cover the orientation markers that are actually load-bearing.
        fp = build_tht_socket(board, "J1", "RP2350-Zero_Socket", layout,
                              pitch=J1_PITCH, show_pin_labels=False)
        fp.SetLibDescription(
            "2x9 THT socket, 2.54mm pitch, 15.24mm row spacing, for a "
            "Waveshare RP2350-Zero (or RP2040-Zero, same footprint) "
            "plugged in from above with male headers soldered to it. "
            "Pin 1 (5V) is square.")
        add_silk_rect(fp, -9.0, -half_row - 2.0, 9.0, half_row + 2.0,
                      width=0.15, layer=pcbnew.F_Fab)
        # USB-C END sits as close under the pad row as the board's tight
        # southern margin (2.84mm past pin index 0) allows. PIN1 has no
        # separate text label on this board -- there's no room for it
        # without colliding with either USB-C END or the board edge -- but
        # pin 1 is still unambiguous from the square pad shape, which
        # (unlike text) needs no mirroring to read the same from either
        # face: it's on *.Cu/*.Mask on both sides regardless.
        usbc_y = 4 * J1_PITCH + 1.15
        add_silk_line(fp, (-4, usbc_y), (4, usbc_y), width=0.3)
        add_silk_text(fp, "USB-C END", (0, usbc_y + 0.95), size=MIN_SILK,
                      thickness=0.15)
        # Bottom-side copy (mirrored text) -- this board must never be
        # mated flipped over (that swaps the two header rows and would
        # drive 3V3 into the SPI pins), so whichever face a header gets
        # soldered from, the USB end is unambiguous without guessing or
        # holding the board up to a light.
        add_bottom_orientation_marker(fp, ((-4, usbc_y), (4, usbc_y)),
                                      (0, usbc_y + 0.95), "USB-C END")
        return fp

    save_library_footprints(make_board_context, [
        (build_j1, "RP2350-Zero_Socket"),
        (build_nrf24_variant_a, "nRF24L01_Mini_SMD_8P127"),
        (lambda b: build_0805_cap(b, "C"), "C_0805_Hand"),
    ])

    # Board matches the Zero's OWN footprint, the same way the Pico 2 W
    # carrier matches the Pico's 21mm width: outer copper extent of the
    # socket is 15.24 (row spacing) + 1.8 (pad) = 17.04mm; at 0.5mm margin
    # each side that's 18.04mm, essentially the Zero's own 18.00mm width.
    # Length likewise targets the Zero's own 23.5mm, grown to 26mm for a
    # comfortable (2.8mm) margin above/below J1's pad grid -- a few mm, not
    # the "up to 30mm" ceiling.
    BOARD_W, BOARD_H = 18.5, 26.0
    J1_ROW_SPACING = 15.24
    J1_POS = (BOARD_W / 2.0, BOARD_H / 2.0)  # centred; pad grid (20.32mm
    # tall) is then symmetric top/bottom with 2.84mm margin each end
    # U1 drops into the channel BETWEEN J1's two rows, the same pattern as
    # the Pico 2 W board's module-between-J1-and-J3 arrangement -- the
    # channel is 15.24 - 1.8 = 13.4mm clear, the module is 12mm wide, so it
    # fits with about 0.7mm to spare each side (plus the keepout margin).
    # Y=9 (south of J1's own northmost pad at Y=2.84) puts the antenna tip
    # at 9 + (1.25 - 18) = -7.75, i.e. 7.75mm of overhang past the top
    # edge, comfortably past the "at least 6mm" mark.
    U1_POS = (BOARD_W / 2.0, 9.0)
    # C1/C2 sit in the channel at the end opposite the antenna -- i.e.
    # just south of U1's own pad row, the closest open space to the pads
    # once the module and its keepout are placed.
    # X~5.44 puts both caps' pads almost directly under U1's VCC/GND pads
    # (4.805/6.075) so the connecting tracks are short near-vertical hops
    # that stay well clear of the CE/CSN/SCK legs further east (X>=7.345);
    # Y is south of U1's own labels/reference/title text.
    C1_POS, C2_POS = (5.44, 16.0), (5.44, 19.5)
    # M2 mounting holes were tried in each corner and dropped: at this
    # width J1's own corner pads (X=1.63/16.87, 2.2mm from either edge)
    # leave under 2mm of clearance to a 2.2mm-drill hole there -- not
    # enough (needs ~2.2mm just for hole-to-pad clearance on top of the
    # hole's own radius). No mounting holes on this board -- see README.
    GND_TRACK_WIDTH = 0.9  # generous vs. the 0.3mm signal default -- this
    # carries the single GND return for the whole board now that there's
    # no copper pour (see "no pour" note below)

    J1_NET = {}
    for i, name in enumerate(ZERO_LEFT):
        J1_NET[i + 1] = net_of(name)
    for i, name in enumerate(ZERO_RIGHT):
        J1_NET[10 + i] = net_of(name)

    netlist_assertion({"J1": J1_NET, "U1": U1_NET})

    board = make_board_context()
    nets = make_nets(board, list(J1_NET.values()) + list(U1_NET.values()) +
                      ["3V3", "GND"])
    r = Router(board, nets)

    j1 = build_j1(board)
    j1.SetPosition(VECTOR2I(MM(J1_POS[0]), MM(J1_POS[1])))
    board.Add(j1)
    for num, n in J1_NET.items():
        j1.FindPadByNumber(str(num)).SetNet(r.net(n))

    u1 = build_nrf24_variant_a(board, show_title=False)
    u1.SetPosition(VECTOR2I(MM(U1_POS[0]), MM(U1_POS[1])))
    board.Add(u1)
    for num, n in U1_NET.items():
        u1.FindPadByNumber(str(num)).SetNet(r.net(n))

    c1 = build_0805_cap(board, "C1")
    c1.SetPosition(VECTOR2I(MM(C1_POS[0]), MM(C1_POS[1])))
    board.Add(c1)
    c1.FindPadByNumber("1").SetNet(r.net("3V3"))
    c1.FindPadByNumber("2").SetNet(r.net("GND"))
    c1.Value().SetText("10uF"); c1.Value().SetVisible(True)
    c1.Value().SetLayer(pcbnew.F_Fab)

    c2 = build_0805_cap(board, "C2")
    c2.SetPosition(VECTOR2I(MM(C2_POS[0]), MM(C2_POS[1])))
    board.Add(c2)
    c2.FindPadByNumber("1").SetNet(r.net("3V3"))
    c2.FindPadByNumber("2").SetNet(r.net("GND"))
    c2.Value().SetText("100nF"); c2.Value().SetVisible(True)
    c2.Value().SetLayer(pcbnew.F_Fab)

    # ---- J1 -> U1: route_L for all 8 nets ----
    # Every J1 pin used here has a unique source Y (9 rows per column, each
    # net on its own row) and every U1 pad has a unique target X (8 pads on
    # a single row) -- exactly the condition route_L's docstring requires.
    # A B.Cu bus at each net's own source Y can only ever touch the
    # column's X at that one Y (never sweeping through a neighbouring
    # pad's row on the way to its via), and an F.Cu leg at each net's own
    # target X never shares an X with any other net's leg -- so none of
    # the 8 same-layer legs or buses can cross each other, and a leg can
    # never short a foreign bus regardless of XY overlap since they're on
    # different layers. (An earlier pass here used single-segment direct
    # diagonals with no via, which DRC immediately flagged: a diagonal
    # leaving a J1 pad lingers close to the column's X long enough to clip
    # several neighbouring pads on the same column before it diverges.)
    u1_sig_to_pin = {sig: i + 1 for i, sig in enumerate(U1_ORDER)}
    j1_pin_for_sig = {"VCC": 3, "GND": 2, "CE": 14, "CSN": 15, "SCK": 16,
                      "MOSI": 17, "IRQ": 18, "MISO": 10}
    for sig, j1pin in j1_pin_for_sig.items():
        width = GND_TRACK_WIDTH if sig == "GND" else TRACK_WIDTH
        r.route_L(CORE_NETS[sig], pad_xy(j1, j1pin), pad_xy(u1, u1_sig_to_pin[sig]),
                 width=width)

    # ---- C1 / C2 -> U1's VCC/GND pads: short direct tracks, no via ----
    # C1/C2 sit almost directly below VCC/GND (see C1_POS comment), so the
    # direct track is a short near-vertical hop that never reaches as far
    # east as CE's leg (X=7.345) -- unlike route_L's via-cornered path,
    # which (tried first) put each cap's two buses at the same Y, since
    # both pads of one capacitor share a row: their bus legs overlapped
    # each other on B.Cu and shorted 3V3 to GND.
    u1vxy = pad_xy(u1, u1_sig_to_pin["VCC"])
    u1gxy = pad_xy(u1, u1_sig_to_pin["GND"])
    for cap in (c1, c2):
        r.track("3V3", [pad_xy(cap, 1), u1vxy])
        r.track("GND", [pad_xy(cap, 2), u1gxy], width=GND_TRACK_WIDTH)

    add_outline(board, BOARD_W, BOARD_H)
    # No copper pour: GND is fully carried by the explicit tracks above
    # (widened to GND_TRACK_WIDTH), and kicad-cli in this Docker toolchain
    # can't fill a zone headless (pcbnew.ZONE_FILLER segfaults even with a
    # real Xvfb display -- confirmed, not assumed) -- so a pour zone would
    # ship unfilled in the gerbers, i.e. silently absent from the actual
    # order. Explicit tracks are what's in the file is what gets fabbed.
    # The module's antenna keepouts stay regardless, via U1's own
    # footprint zones (see build_nrf24_variant_a).

    # No separate board-name/rev title line on this board: at 18.5x26mm
    # the only open strip (south of J1's pad grid) is already used by
    # J1's own "USB-C END" marker, and there's no room for both. Board
    # identity is on J1/U1/C1/C2's own reference designators plus the BOM
    # and README instead.
    # "3V3 NOT 5V" is centred in the channel, south of C1/C2 and far in X
    # from either of J1's columns (1.63 / 16.87) regardless of Y proximity
    # to a pad row.
    board_text(board, "3V3 NOT 5V", (BOARD_W / 2.0, 19.0), size=MIN_SILK,
              thickness=0.15, angle=90, h_align=pcbnew.GR_TEXT_H_ALIGN_CENTER)

    translate_board(board, 0, PAGE_MARGIN_Y)

    write_fp_lib_table(outdir)
    write_bom(outdir, [
        ("J1", 1, "RP2350-Zero_Socket", "hand-wound 2x9 2.54mm socket",
         "Female header socket, 2x9, 2.54mm pitch (e.g. two 1x9 strips)", "Not on LCSC as one part -- use generic 2.54mm female header strip"),
        ("U1", 1, "nRF24L01_Mini_SMD_8P127", "nRF24L01+ mini SMD module, 1.27mm pitch",
         "2.4GHz transceiver module, single-row castellated SMD", "Hand-solder; JLCPCB does not stock this part"),
        ("C1", 1, "C_0805_Hand", "10uF 0805 X5R/X7R", "Bulk decoupling, at U1's VCC/GND pads", ""),
        ("C2", 1, "C_0805_Hand", "100nF 0805 X7R", "HF decoupling, at U1's VCC/GND pads", ""),
    ])
    finish_and_save(board, os.path.join(outdir, "24g2usb_rp2350zero.kicad_pcb"))


# ===========================================================================
# VARIANT: pico2w
# ===========================================================================
def build_pico2w(outdir):
    # Pico / Pico W / Pico 2 / Pico 2 W mechanicals (ncarandini/KiCad-RP-Pico
    # RPi_Pico_SMD_TH.kicad_mod): 51.0 x 21.0mm body, 2x20 pads, 2.54mm
    # pitch, row-to-row 17.78mm, pad centres 1.61mm in from each long edge,
    # 1.02mm drill / 1.7mm pad. Pin 1 (GP0) is the first left-column pad,
    # ~1.37mm from the USB end; numbering runs 1..20 down the left column
    # (GP0..GP15, with GND at 3/8/13/18) and 21..40 back up the right
    # column (GP16 at 21, VBUS at 40) -- cross-checked against the
    # well-known Pico pinout (pin6=GP4, pin7=GP5, pin9=GP6, pin10=GP7,
    # pin11=GP8, pin36=3V3(OUT), pin38=GND all confirmed).
    PITCH = 2.54
    LEFT_LABELS = ["GP0", "GP1", "GND", "GP2", "GP3", "GP4", "GP5", "GND",
                   "GP6", "GP7", "GP8"]  # pins 1..11 (only the ones we need)
    RIGHT_LABELS = ["VBUS", "VSYS", "GND", "3V3_EN", "3V3"]  # pins 40..36 --
    # net names (net_of() below reads these); RIGHT_SILK is the on-board
    # abbreviated form actually drawn, since a full label's rotated length
    # bleeds into the next pin's row at this 2.54mm pitch.
    RIGHT_SILK = ["VB", "VS", "GN", "EN", "3V"]

    def build_left(board):
        layout = {i + 1: (0, i * PITCH, LEFT_LABELS[i])
                  for i in range(len(LEFT_LABELS))}
        fp = build_tht_socket(board, "J1", "Pico_Socket_1x11", layout,
                              pitch=PITCH, drill=1.02, pad_size=1.7,
                              label_offset=1.7, label_side=1, label_angle=90)
        fp.SetLibDescription(
            "1x11 THT socket, 2.54mm pitch, pins 1-11 of a Raspberry Pi "
            "Pico-family board (GP0..GP8 plus two GND), plugged in from "
            "above with male headers soldered to it. Pin 1 (GP0) is square.")
        add_silk_text(fp, "1", (0, -1.6), size=MIN_SILK, thickness=0.15)
        add_silk_text(fp, "1", (0, -1.6), size=MIN_SILK, thickness=0.15,
                      layer=pcbnew.B_SilkS)
        return fp

    def build_right(board):
        layout = {i + 1: (0, i * PITCH, RIGHT_SILK[i])
                  for i in range(len(RIGHT_LABELS))}
        fp = build_tht_socket(board, "J3", "Pico_Socket_1x5", layout,
                              pitch=PITCH, drill=1.02, pad_size=1.7,
                              label_offset=1.7, label_side=-1, label_angle=90)
        fp.SetLibDescription(
            "1x5 THT socket, 2.54mm pitch, pins 40-36 of a Raspberry Pi "
            "Pico-family board (VBUS, VSYS, GND, 3V3_EN, 3V3(OUT)) -- "
            "reaches 3V3 and a second GND, plus mechanical support at the "
            "USB end. Pin 40 (VBUS, unused) is square.")
        add_silk_text(fp, "1", (0, -1.6), size=MIN_SILK, thickness=0.15)
        add_silk_text(fp, "1", (0, -1.6), size=MIN_SILK, thickness=0.15,
                      layer=pcbnew.B_SilkS)
        return fp

    save_library_footprints(make_board_context, [
        (build_left, "Pico_Socket_1x11"),
        (build_nrf24_variant_a, "nRF24L01_Mini_SMD_8P127"),
        (build_right, "Pico_Socket_1x5"),
        (lambda b: build_0805_cap(b, "C"), "C_0805_Hand"),
    ])

    # Board: Pico's own 21mm width, as short as the pin span allows.
    BOARD_W, BOARD_H = 21.0, 33.0
    HALF_ROW = 17.78 / 2.0          # 8.89
    BOARD_CX = BOARD_W / 2.0        # 10.5
    LEFT_X = BOARD_CX - HALF_ROW    # 1.61
    RIGHT_X = BOARD_CX + HALF_ROW   # 19.39
    Y0 = 4.0                        # pin-1 row Y (leaves room for the
                                     # antenna overhang north of it)
    J1_POS = (LEFT_X, Y0)
    J3_POS = (RIGHT_X, Y0)
    # Y=10 (was 16 in an earlier revision, which gave only 0.75mm of
    # antenna overhang -- effectively none) puts the tip at
    # 10 + (1.25 - 18) = -6.75, 6.75mm past the top edge, and opens up
    # 1.25..11.75 south of the pads as the "relaxed" (pour/via-only)
    # keepout tier, with open board beyond that -- room for C1/C2.
    U1_POS = (BOARD_CX, 10.0)
    GND_TRACK_WIDTH = 0.9
    # C1/C2 sit south of U1's own pad-label text band (empirically ~y=11
    # to ~15 for the longest labels, rotated vertical at 0.8mm size --
    # confirmed via DRC silk_overlap when caps were first tried at y=13/
    # 15, right in that band) and south of the CE trace's F.Cu leg
    # clearance (X centred at 6.3, between VCC=6.055/GND=7.325, biased
    # slightly toward VCC to keep 0.2mm+ clear of the CE leg at X=8.595 --
    # 6.7 measured 0.005mm short on DRC). Short diagonal tracks reach
    # VCC/GND from here; see the C1/C2 wiring below.
    C1_POS, C2_POS = (6.3, 15.6), (6.3, 19.0)

    def left_xy(idx):
        return (LEFT_X, Y0 + idx * PITCH)

    def right_xy(idx):
        return (RIGHT_X, Y0 + idx * PITCH)

    # signal -> (column, index-from-pin1-row)
    SRC = {
        "MISO": ("L", 0), "CE": ("L", 5), "CSN": ("L", 6), "SCK": ("L", 8),
        "MOSI": ("L", 9), "IRQ": ("L", 10), "GND": ("L", 7),
        "VCC": ("R", 4),        # pin36, 3V3(OUT)
        "GND2": ("R", 2),       # pin38, second GND tap
    }

    j1_net = {i + 1: net_of(LEFT_LABELS[i]) for i in range(len(LEFT_LABELS))}
    j3_net = {i + 1: net_of(RIGHT_LABELS[i]) for i in range(len(RIGHT_LABELS))}
    j3_net[1] = ""  # VBUS unused -- do not wire 5V-domain VBUS to anything
    # J1 pin3 is also labelled GND (Pico silkscreen has two on this column)
    # but isn't the pin actually used as the GND source below -- giving it
    # the same "GND" net as a pad with no copper link to it would leave an
    # unconnected-ratsnest pad, so it gets its own single-pad net instead.
    j1_net[3] = "GND_NC_L3"

    # J1 (left socket) carries 6 of the 8 signals; J3 (right socket)
    # carries VCC/GND -- merge them into one pad map (distinct keys) for
    # the assertion, since neither alone touches all 8 core nets.
    j1j3_merged = {f"L{k}": v for k, v in j1_net.items()}
    j1j3_merged.update({f"R{k}": v for k, v in j3_net.items() if v})
    netlist_assertion({"J1+J3": j1j3_merged, "U1": U1_NET})

    board = make_board_context()
    nets = make_nets(board, list(j1_net.values()) + list(j3_net.values()) +
                      list(U1_NET.values()) + ["3V3", "GND"])
    r = Router(board, nets)

    j1 = build_left(board)
    j1.SetPosition(VECTOR2I(MM(J1_POS[0]), MM(J1_POS[1])))
    board.Add(j1)
    for num, n in j1_net.items():
        j1.FindPadByNumber(str(num)).SetNet(r.net(n))

    j3 = build_right(board)
    j3.SetPosition(VECTOR2I(MM(J3_POS[0]), MM(J3_POS[1])))
    board.Add(j3)
    for num, n in j3_net.items():
        if n:
            j3.FindPadByNumber(str(num)).SetNet(r.net(n))

    # show_title=False: this board is too tight vertically for U1's "U1
    # nRF24L01+" title text once C1/C2 occupy the space just south of the
    # pad-label band (same reasoning the RP2350-Zero carrier already
    # uses) -- the U1 reference designator alone still identifies the part.
    u1 = build_nrf24_variant_a(board, show_title=False)
    u1.SetPosition(VECTOR2I(MM(U1_POS[0]), MM(U1_POS[1])))
    board.Add(u1)
    for num, n in U1_NET.items():
        u1.FindPadByNumber(str(num)).SetNet(r.net(n))

    # ---- C1 (10uF) / C2 (100nF) decoupling, right at U1's VCC/GND pads --
    # same arrangement as the RP2350-Zero carrier: straight direct tracks
    # (no via, no L-route) since both cap pads share one row and an
    # L-route's same-Y bus legs would overlap and short 3V3 to GND.
    u1_sig_to_pin_caps = {sig: i + 1 for i, sig in enumerate(U1_ORDER)}
    c1 = build_0805_cap(board, "C1")
    c1.SetPosition(VECTOR2I(MM(C1_POS[0]), MM(C1_POS[1])))
    board.Add(c1)
    c1.FindPadByNumber("1").SetNet(r.net("3V3"))
    c1.FindPadByNumber("2").SetNet(r.net("GND"))
    c1.Value().SetText("10uF"); c1.Value().SetVisible(True)
    c1.Value().SetLayer(pcbnew.F_Fab)

    c2 = build_0805_cap(board, "C2")
    c2.SetPosition(VECTOR2I(MM(C2_POS[0]), MM(C2_POS[1])))
    board.Add(c2)
    c2.FindPadByNumber("1").SetNet(r.net("3V3"))
    c2.FindPadByNumber("2").SetNet(r.net("GND"))
    c2.Value().SetText("100nF"); c2.Value().SetVisible(True)
    c2.Value().SetLayer(pcbnew.F_Fab)

    u1vxy = pad_xy(u1, u1_sig_to_pin_caps["VCC"])
    u1gxy = pad_xy(u1, u1_sig_to_pin_caps["GND"])
    for cap in (c1, c2):
        r.track("3V3", [pad_xy(cap, 1), u1vxy])
        r.track("GND", [pad_xy(cap, 2), u1gxy], width=GND_TRACK_WIDTH)

    # ---- L-routes: source Y unique per net (distinct header rows), target
    # X unique per net (distinct U1 pads) -- no jog needed, EXCEPT for the
    # 3 signals whose source row sits north of U1's own pad row (MISO at
    # the very first pin, and both taps off J3 which is entirely north of
    # U1): a plain route_L's bus would cross through U1's antenna keepout
    # to get there. Those detour south of ALL of U1's pads first (clear
    # strip at Y=18, then a short jog west/east off the dense pin columns
    # before turning, so the detour doesn't clip a neighbouring pin on its
    # own column either).
    u1_sig_to_pin = {sig: i + 1 for i, sig in enumerate(U1_ORDER)}
    # Only MISO (pin 1, the very first row) sits north of U1's relaxed
    # "body, no pour" zone (Y<7.25 local-to-board here) -- GND2 and VCC
    # both land inside it and route with a plain L now that the zone
    # allows tracks through the module's near (non-antenna) end.
    SOUTH_DETOUR = {"MISO": (3.2, 7.5)}
    for sig, (col, idx) in SRC.items():
        target_sig = "GND" if sig == "GND2" else sig
        width = GND_TRACK_WIDTH if target_sig == "GND" else TRACK_WIDTH
        src = left_xy(idx) if col == "L" else right_xy(idx)
        dst = pad_xy(u1, u1_sig_to_pin[target_sig])
        if sig in SOUTH_DETOUR:
            jog_x, cross_y = SOUTH_DETOUR[sig]
            r.track(CORE_NETS[target_sig],
                    [src, (jog_x, src[1]), (jog_x, cross_y), (dst[0], cross_y), dst],
                    width=width)
        else:
            r.route_L(CORE_NETS[target_sig], src, dst, width=width)

    add_outline(board, BOARD_W, BOARD_H)
    # No copper pour: same reasoning as the RP2350-Zero carrier -- kicad-cli
    # in this Docker toolchain can't fill a zone headless (pcbnew.ZONE_FILLER
    # segfaults even with a real Xvfb display -- confirmed, not assumed), so
    # a pour zone would ship UNFILLED in the gerbers, i.e. silently absent
    # from the actual order. GND is instead fully carried by the explicit
    # tracks above (widened to GND_TRACK_WIDTH) -- what's in the file is
    # what gets fabbed. The module's antenna keepouts stay regardless, via
    # U1's own footprint zones (see build_nrf24_variant_a).

    # Bottom-centre gap between J1's and J3's (now vertical, narrow) labels
    # -- clear of both columns and of U1 (which stops at Y=17.25).
    board_text(board, "24G2USB-PICO2W", (5.0, BOARD_H - 4.0), size=MIN_SILK, thickness=0.15)
    board_text(board, "REV A 2026-09", (5.0, BOARD_H - 2.8), size=MIN_SILK, thickness=0.15)
    board_text(board, "3V3 NOT 5V", (5.0, BOARD_H - 1.6), size=MIN_SILK, thickness=0.15)

    # Board-level USB-end marker, front and back (mirrored) -- this board
    # must never be mated flipped over (male headers soldered from below
    # instead of sockets from above swap the two columns and drive 3V3
    # into the SPI pins), so both faces must be unambiguous about which
    # edge is the USB end regardless of which side a header gets soldered.
    board_text(board, "USB END ^", (6.0, 1.0), size=MIN_SILK, thickness=0.15,
              h_align=pcbnew.GR_TEXT_H_ALIGN_LEFT)
    board_text(board, "USB END ^", (6.0, 1.0), size=MIN_SILK, thickness=0.15,
              h_align=pcbnew.GR_TEXT_H_ALIGN_LEFT, layer=pcbnew.B_SilkS)

    translate_board(board, 0, PAGE_MARGIN_Y)

    write_fp_lib_table(outdir)
    write_bom(outdir, [
        ("J1", 1, "Pico_Socket_1x11", "hand-wound 1x11 2.54mm socket",
         "Female header socket, 1x11, 2.54mm pitch", "Cut from a standard break-away strip"),
        ("J3", 1, "Pico_Socket_1x5", "hand-wound 1x5 2.54mm socket",
         "Female header socket, 1x5, 2.54mm pitch", "Cut from a standard break-away strip"),
        ("U1", 1, "nRF24L01_Mini_SMD_8P127", "nRF24L01+ mini SMD module, 1.27mm pitch",
         "2.4GHz transceiver module, single-row castellated SMD", "Hand-solder; JLCPCB does not stock this part"),
        ("C1", 1, "C_0805_Hand", "10uF 0805 X5R/X7R", "Bulk decoupling, at U1's VCC/GND pads", ""),
        ("C2", 1, "C_0805_Hand", "100nF 0805 X7R", "HF decoupling, at U1's VCC/GND pads", ""),
    ])
    finish_and_save(board, os.path.join(outdir, "24g2usb_pico2w.kicad_pcb"))


# ===========================================================================
if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--variant", required=True, choices=["rp2350zero", "pico2w"])
    args = ap.parse_args()
    outdir = os.path.join(HERE, f"24g2usb_{args.variant}")
    os.makedirs(outdir, exist_ok=True)
    if args.variant == "rp2350zero":
        build_rp2350zero(outdir)
    else:
        build_pico2w(outdir)
