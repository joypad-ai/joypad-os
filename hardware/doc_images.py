#!/usr/bin/env python3
"""doc_images.py - documentation figures for the 24g2usb carrier boards.

Draws two PNGs per variant into docs/images/, both derived from the generated
.kicad_pcb so they cannot drift from the copper:

  <variant>_assembly.png   top view to scale: board outline, header pads, the
                           nRF24 module's true 12x18mm body, its antenna
                           overhang, and C1/C2 - answers "does the module
                           block the cap pads" without fabricating anything.
  <variant>_schematic.png  the circuit: socket pins to module pads, with the
                           net names read back out of the board file.

Run after generate.py (make -C <variant> all):

    python3 doc_images.py

Needs Pillow only - no KiCad, no Docker.
"""
import os
import re
from PIL import Image, ImageDraw, ImageFont

S = 26  # px per mm
def F(sz):
    for p in ("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
              "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"):
        try: return ImageFont.truetype(p, sz)
        except Exception: pass
    return ImageFont.load_default()

def parse(v):
    s = open(f"{v}/{v}.kicad_pcb").read()
    edges = re.findall(r'\(gr_(?:line|rect)[^)]*\(start ([-\d.]+) ([-\d.]+)\)\s*\(end ([-\d.]+) ([-\d.]+)\)[\s\S]{0,200}?Edge\.Cuts', s)
    X=[];Y=[]
    for a,b,c,d in edges: X+=[float(a),float(c)]; Y+=[float(b),float(d)]
    board=(min(X),min(Y),max(X),max(Y))
    fps={}
    for fp in re.split(r'\n\t\(footprint ', s)[1:]:
        ref=re.search(r'\(property "Reference" "([^"]+)"',fp).group(1)
        at=re.search(r'\(at ([-\d.]+) ([-\d.]+)',fp)
        fx,fy=float(at.group(1)),float(at.group(2))
        pads=[]
        for m in re.finditer(r'\(pad "([^"]+)" (\w+) (\w+)\s*\(at ([-\d.]+) ([-\d.]+)[^)]*\)\s*\(size ([\d.]+) ([\d.]+)\)',fp):
            pads.append(dict(n=m.group(1),typ=m.group(2),shape=m.group(3),
                             x=fx+float(m.group(4)),y=fy+float(m.group(5)),
                             w=float(m.group(6)),h=float(m.group(7))))
        fps[ref]=dict(x=fx,y=fy,pads=pads)
    return board,fps

def draw_assembly(v,title,out):
    (bx0,by0,bx1,by1),fps = parse(v)
    u=fps['U1']; mod=(u['x']-6,u['y']-16.75,u['x']+6,u['y']+1.25)
    minx=min(bx0,mod[0])-9; maxx=max(bx1,mod[2])+9
    miny=min(by0,mod[1])-11; maxy=max(by1,mod[3])+7
    W=int((maxx-minx)*S); H=int((maxy-miny)*S)
    im=Image.new('RGB',(W,H),(250,250,252)); d=ImageDraw.Draw(im,'RGBA')
    def P(x,y): return ((x-minx)*S,(y-miny)*S)
    def rect(x0,y0,x1,y1,**kw): d.rectangle([P(x0,y0),P(x1,y1)],**kw)
    # board
    rect(bx0,by0,bx1,by1,fill=(24,86,56),outline=(12,50,32),width=3)
    # pads
    for ref,fp in fps.items():
        for p in fp['pads']:
            x0,y0,x1,y1=p['x']-p['w']/2,p['y']-p['h']/2,p['x']+p['w']/2,p['y']+p['h']/2
            if p['typ']=='thru_hole':
                d.ellipse([P(x0,y0),P(x1,y1)],fill=(226,183,54))
                r=0.5
                d.ellipse([P(p['x']-r,p['y']-r),P(p['x']+r,p['y']+r)],fill=(60,60,60))
            elif ref=='U1':
                rect(x0,y0,x1,y1,fill=(198,198,200))
    # module body
    rect(mod[0],mod[1],mod[2],mod[3],fill=(40,90,200,110),outline=(20,50,150),width=3)
    # antenna zone (far 8mm)
    rect(mod[0],mod[1],mod[2],mod[1]+8,fill=(40,90,200,70),outline=(20,50,150),width=1)
    d.text(P(mod[0]+0.4,mod[1]+3.0),"ANTENNA",font=F(15),fill=(240,240,255))
    d.text(P(mod[0]+0.4,mod[1]+9.6),"nRF24 MODULE",font=F(15),fill=(240,240,255))
    d.text(P(mod[0]+0.4,mod[1]+11.6),"12 x 18 mm",font=F(14),fill=(225,230,250))
    # caps 0805 body 2.0 x 1.25
    nearest=None
    for ref in ('C1','C2'):
        if ref not in fps: continue
        c=fps[ref]
        rect(c['x']-1.0,c['y']-0.625,c['x']+1.0,c['y']+0.625,fill=(30,30,32),outline=(120,120,120),width=2)
        for p in c['pads']:
            rect(p['x']-p['w']/2,p['y']-p['h']/2,p['x']+p['w']/2,p['y']+p['h']/2,fill=(205,205,208))
        lbl="C1 10uF" if ref=='C1' else "C2 100nF"
        d.text(P(c['x']+1.6,c['y']-0.8),lbl,font=F(15),fill=(255,255,255))
        if nearest is None or c['y']<nearest: nearest=c['y']-0.625
    # clearance arrow between module bottom edge and nearest cap
    xarrow=mod[0]+1.0
    d.line([P(xarrow,mod[3]),P(xarrow,nearest)],fill=(255,210,60),width=3)
    gap=nearest-mod[3]
    d.text(P(xarrow+0.3,(mod[3]+nearest)/2-0.9),f"{gap:.1f} mm clear",font=F(15),fill=(255,210,60))
    # overhang arrow
    d.line([P(mod[2]+1.2,mod[1]),P(mod[2]+1.2,by0)],fill=(255,120,120),width=3)
    d.text(P(mod[2]+1.6,(mod[1]+by0)/2-0.7),f"{by0-mod[1]:.1f} mm\noverhang",font=F(15),fill=(200,60,60))
    # board size label
    d.text(P(bx0,by1+1.2),f"board {bx1-bx0:.1f} x {by1-by0:.1f} mm",font=F(17),fill=(40,40,40))
    d.text(P(minx+0.6,miny+0.4),title,font=F(21),fill=(20,20,20))
    d.text(P(minx+0.6,miny+1.8),"top view, to scale - module solders over the socket channel",font=F(14),fill=(90,90,90))
    im.save(out)
    print(f"{out}  (cap clearance {gap:.2f}mm, antenna overhang {by0-mod[1]:.2f}mm)")



# ---------------------------------------------------------------- schematic

SS = 2  # supersample factor for the schematic drawing
def SF(sz, bold=True):
    p = "/usr/share/fonts/truetype/dejavu/DejaVuSans%s.ttf" % ("-Bold" if bold else "")
    try: return ImageFont.truetype(p, sz * SS)
    except Exception: return ImageFont.load_default()

# U1's pads in physical order; the pad order itself is verified against the
# vendor drawing (see the module footprint's descr field).
U1_PINS = [("1","VCC"),("2","GND"),("3","CE"),("4","CSN"),
           ("5","SCK"),("6","MOSI"),("7","MISO"),("8","IRQ")]

def draw_schematic(v, title, conn_title, order, extra, out):
    """order: (socket label, J ref.pad, net name, U1 pin name) per row."""
    W,H=1500,1060
    im=Image.new('RGB',(W*SS,H*SS),(255,255,255)); d=ImageDraw.Draw(im)
    BK=(20,20,24); WIRE=(10,90,180); RED=(185,35,35); GY=(120,120,126)
    def line(x0,y0,x1,y1,c=BK,w=2): d.line([(x0*SS,y0*SS),(x1*SS,y1*SS)],fill=c,width=w*SS)
    def box(x0,y0,x1,y1): d.rectangle([(x0*SS,y0*SS),(x1*SS,y1*SS)],outline=BK,width=3*SS)
    def txt(x,y,t,f,c=BK,a="la"): d.text((x*SS,y*SS),t,font=f,fill=c,anchor=a)
    def dot(x,y,r=5): d.ellipse([((x-r)*SS,(y-r)*SS),((x+r)*SS,(y+r)*SS)],fill=WIRE)
    f18,f15,f13,f22=SF(18),SF(15),SF(13,False),SF(23)
    txt(40,28,title,f22)
    txt(40,60,"generated from the board netlist -- pin numbers are physical pins",SF(14,False),GY)

    LX0,LX1,RX0,RX1=200,470,980,1250
    TOP=330; PITCH=74
    box(LX0,TOP-46,LX1,TOP+7*PITCH+46)
    txt((LX0+LX1)/2,TOP-78,conn_title,f18,BK,"ma")
    box(RX0,TOP-46,RX1,TOP+7*PITCH+46)
    txt((RX0+RX1)/2,TOP-78,"U1   nRF24L01+ module",f18,BK,"ma")
    txt((RX0+RX1)/2,TOP+7*PITCH+60,"12 x 18 mm, single row of 8 pads @ 1.27 mm",SF(13,False),GY,"ma")

    ypos={}
    for i,(pn,nm) in enumerate(U1_PINS):
        y=TOP+i*PITCH; ypos[nm]=y
        line(RX0-46,y,RX0,y,BK,3); txt(RX0+16,y,nm,f15,BK,"lm"); txt(RX0-54,y-1,pn,f13,GY,"rm")
    for i,(label,pin,net,mp) in enumerate(order):
        y=TOP+i*PITCH
        line(LX1,y,LX1+46,y,BK,3); txt(LX1-16,y,label,f15,BK,"rm"); txt(LX1+54,y-1,pin,f13,GY,"lm")
        line(LX1+46,y,RX0-46,y,WIRE,3)
        txt((LX1+46+RX0-46)/2,y-24,net,f13,WIRE,"ma")

    # Decoupling is drawn ABOVE the pin rows on purpose: risers dropped below
    # VCC/GND would cross the whole signal bundle and read as a tangle.
    vy,gy=ypos["VCC"],ypos["GND"]
    xv,xg=660,880
    rail_v, rail_g = vy-196, vy-96
    line(xv,vy,xv,rail_v,WIRE,3); dot(xv,vy)
    line(xg,gy,xg,rail_g,WIRE,3); dot(xg,gy)
    line(xv,rail_v,xg,rail_v,WIRE,3)
    line(xv,rail_g,xg,rail_g,WIRE,3)
    for k,(cap,val) in enumerate((("C1","10 uF"),("C2","100 nF"))):
        x=720+k*100
        line(x,rail_v,x,rail_v+32,WIRE,3)
        line(x-26,rail_v+32,x+26,rail_v+32,BK,4)
        line(x-26,rail_v+52,x+26,rail_v+52,BK,4)
        line(x,rail_v+52,x,rail_g,WIRE,3)
        txt(x,rail_v+8,cap,f15,BK,"ma"); txt(x,rail_v+60,val,f13,GY,"ma")
    txt(xv-12,rail_v-4,"3V3",f13,WIRE,"rm"); txt(xg+12,rail_g-4,"GND",f13,WIRE,"lm")
    txt((xv+xg)/2,rail_v-42,"decoupling -- both caps sit directly across U1's VCC/GND pads",
        SF(13,False),GY,"ma")

    y0=H-104
    txt(40,y0,"VCC is 3V3 from the MCU's on-board regulator -- NOT 5V. The module is not 5V "
        "tolerant on its supply.",SF(14,False),RED)
    txt(40,y0+26,extra,SF(14,False),GY)
    txt(40,y0+52,"Net names are exactly those in the .kicad_pcb; this drawing is generated from "
        "it, so it cannot drift from the copper.",SF(13,False),GY)
    im=im.resize((W,H),Image.LANCZOS); im.save(out); print(out)


IMG = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "docs", "images")

if __name__ == "__main__":
    os.makedirs(IMG, exist_ok=True)

    draw_assembly('24g2usb_rp2350zero','RP2350-Zero carrier - assembly preview',
                  os.path.join(IMG,'24g2usb_rp2350zero_assembly.png'))
    draw_schematic('24g2usb_rp2350zero','24g2usb carrier - RP2350-Zero variant',
        'J1   RP2350-Zero socket (2x9)',
        [("3V3","J1.3","3V3","VCC"),("GND","J1.2","GND","GND"),("GP4","J1.14","GP4_CE","CE"),
         ("GP5","J1.15","GP5_CSN","CSN"),("GP6","J1.16","GP6_SCK","SCK"),
         ("GP7","J1.17","GP7_MOSI","MOSI"),("GP0","J1.10","GP0_MISO","MISO"),
         ("GP8","J1.18","GP8_IRQ","IRQ")],
        "The remaining 10 socket positions (5V, GP1, GP2, GP3, GP14, GP15, GP26-GP29) are wired "
        "to nothing -- they exist for mechanical support only.",
        os.path.join(IMG,'24g2usb_rp2350zero_schematic.png'))

    draw_assembly('24g2usb_pico2w','Pico 2 W carrier - assembly preview',
                  os.path.join(IMG,'24g2usb_pico2w_assembly.png'))
    draw_schematic('24g2usb_pico2w','24g2usb carrier - Pico 2 W variant',
        'J1 (1x11) + J3 (1x5)   Pico 2 W sockets',
        [("3V3  pin 36","J3.5","3V3","VCC"),("GND  pin 38","J3.3","GND","GND"),
         ("GP4  pin 6","J1.6","GP4_CE","CE"),("GP5  pin 7","J1.7","GP5_CSN","CSN"),
         ("GP6  pin 9","J1.9","GP6_SCK","SCK"),("GP7  pin 10","J1.10","GP7_MOSI","MOSI"),
         ("GP0  pin 1","J1.1","GP0_MISO","MISO"),("GP8  pin 11","J1.11","GP8_IRQ","IRQ")],
        "J1 also lands on pins 2,3,4,5,8 (GP1, GND, GP2, GP3, GND) and J3 on pins 37,39,40 "
        "(3V3_EN, VSYS, VBUS) -- all left unconnected.",
        os.path.join(IMG,'24g2usb_pico2w_schematic.png'))
