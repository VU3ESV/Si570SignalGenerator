#!/usr/bin/env python3
"""Draws docs/ext-ref-wiring.svg: the external 10 MHz reference and Si570 sample inputs.

Usage:  python3 tools/ext_ref_schematic.py docs/ext-ref-wiring.svg
PNG:    Chrome --headless --force-device-scale-factor=2 --window-size=1260,1090 \
               --screenshot=docs/ext-ref-wiring.png file://$PWD/docs/ext-ref-wiring.svg
"""
import sys

W, H = 1260, 1090
out = []
OHM, ARROW, DOT, MU, DASH = "Ω", "→", "·", "µ", "–"


def add(s):
    out.append(s)


def wire(*pts):
    add('<polyline class="w" points="%s"/>' % " ".join("%g,%g" % p for p in pts))


def dot(x, y):
    add('<circle class="dot" cx="%g" cy="%g" r="4"/>' % (x, y))


def text(x, y, s, cls="t", anchor="start"):
    add('<text class="%s" x="%g" y="%g" text-anchor="%s">%s</text>' % (cls, x, y, anchor, s))


def res_h(x0, x1, y, name, value):
    m = (x0 + x1) / 2
    wire((x0, y), (m - 20, y))
    add('<rect class="p" x="%g" y="%g" width="40" height="14"/>' % (m - 20, y - 7))
    wire((m + 20, y), (x1, y))
    text(m, y - 14, "%s  %s" % (name, value), "t", "middle")


def res_v(x, y0, y1, name, value, right=True):
    m = (y0 + y1) / 2
    wire((x, y0), (x, m - 20))
    add('<rect class="p" x="%g" y="%g" width="14" height="40"/>' % (x - 7, m - 20))
    wire((x, m + 20), (x, y1))
    if right:
        text(x + 14, m - 2, name)
        text(x + 14, m + 15, value)
    else:
        text(x - 14, m - 2, name, "t", "end")
        text(x - 14, m + 15, value, "t", "end")


def cap_h(x0, x1, y, label=None, extra=None):
    m = (x0 + x1) / 2
    wire((x0, y), (m - 4, y))
    wire((m - 4, y - 14), (m - 4, y + 14))
    wire((m + 4, y - 14), (m + 4, y + 14))
    wire((m + 4, y), (x1, y))
    if label:
        text(m, y - 22, label, "t", "middle")
    if extra:
        text(m, y - 39, extra, "s", "middle")


def gnd(x, y, stub=0):
    if stub:
        wire((x, y), (x, y + stub))
        y += stub
    wire((x - 12, y), (x + 12, y))
    wire((x - 8, y + 5), (x + 8, y + 5))
    wire((x - 4, y + 10), (x + 4, y + 10))


def vcc(x, y, label="+3V3"):
    wire((x - 12, y), (x + 12, y))
    text(x, y - 7, label, "net", "middle")


def nc_mark(x, y):
    add('<path class="nc" d="M%g %g l10 10 M%g %g l-10 10"/>' % (x - 5, y - 5, x + 5, y - 5))


def bnc(x, y, shell_to):
    add('<circle class="w" cx="%g" cy="%g" r="15"/>' % (x, y))
    add('<circle class="dot" cx="%g" cy="%g" r="3.5"/>' % (x, y))
    wire((x, y + 15), (x, shell_to))
    gnd(x, shell_to)


def schmitt(x, y, ref, chip):
    """74LVC1G14 inverting Schmitt trigger: input at (x, y), output lead from (x+60, y)."""
    add('<polygon class="p" points="%g,%g %g,%g %g,%g"/>' % (x, y - 25, x, y + 25, x + 50, y))
    add('<circle class="p" cx="%g" cy="%g" r="5"/>' % (x + 55, y))
    add('<path class="w thin" d="M%g %g h8 v-10 h8 M%g %g v10 h8 v-10"/>' % (x + 8, y + 5, x + 12, y - 5))
    text(x - 6, y - 6, "2", "pin", "end")
    text(x + 66, y - 6, "4", "pin")
    text(x + 24, y - 18, "5", "pin")
    text(x + 24, y + 30, "3", "pin")
    text(x + 74, y + 34, ref)
    text(x + 74, y + 51, chip, "s")


def supply_with_decoupling(x, y_chip_top, y_bar, y_cap, ref):
    """VCC lead from the gate up to a +3V3 bar, with a 100 nF cap to ground beside it."""
    wire((x, y_chip_top), (x, y_bar))
    vcc(x, y_bar)
    dot(x, y_cap)
    wire((x, y_cap), (x + 20, y_cap))
    cap_h(x + 20, x + 60, y_cap)
    wire((x + 60, y_cap), (x + 80, y_cap))
    gnd(x + 80, y_cap, stub=8)
    text(x + 98, y_cap + 5, "%s  100 nF" % ref)


# ------------------------------------------------------------------ page
add('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">' % (W, H, W, H))
add("""<style>
  .w{stroke:#111;stroke-width:2;fill:none;stroke-linejoin:round;stroke-linecap:round}
  .thin{stroke-width:1.5}
  .p{fill:#fff;stroke:#111;stroke-width:2}
  .dot{fill:#111}
  text{font-family:Helvetica,Arial,sans-serif}
  .t{font-size:15px;fill:#111}
  .s{font-size:13px;fill:#555}
  .pin{font-size:12px;fill:#777}
  .net{font-size:14px;fill:#1a56a8;font-weight:bold}
  .h1{font-size:23px;font-weight:bold;fill:#111}
  .h2{font-size:18px;font-weight:bold;fill:#111}
  .box{fill:#f6f7f9;stroke:#c9ccd2;stroke-width:1}
  .nc{stroke:#b3261e;stroke-width:2.5;fill:none}
</style>""")
add('<rect width="%d" height="%d" fill="#fff"/>' % (W, H))
text(30, 40, "Si570 generator: external 10 MHz reference and Si570 sample inputs", "h1")
text(30, 64, "All logic runs from the ESP32's 3.3 V. Values are for a CMOS Si570 (e.g. CAC000141). Red × = leave unconnected.", "s")

# ------------------------------------------------------------------ 1: reference -> GPIO34
text(30, 110, "1 %s 10 MHz reference %s GPIO34" % (DOT, ARROW), "h2")
y1 = 260
bnc(80, y1, 310)
text(80, y1 - 60, "J1", "t", "middle")
text(80, y1 - 43, "10 MHz REF IN", "s", "middle")
text(80, y1 - 27, "from GPSDO", "s", "middle")
wire((80, y1), (190, y1))
dot(190, y1)
res_v(190, y1, 340, "R1", "51 " + OHM)
gnd(190, 340)
cap_h(230, 290, y1, "C1  100 nF")
wire((190, y1), (230, y1))
wire((290, y1), (340, y1))
dot(340, y1)
res_v(340, 160, y1, "R2", "12 k" + OHM)
vcc(340, 160)
res_v(340, y1, 350, "R3", "10 k" + OHM)
gnd(340, 350)
text(355, y1 + 104, "bias = 1.50 V", "s")
wire((340, y1), (450, y1))
schmitt(450, y1, "U1", "74LVC1G14")
supply_with_decoupling(470, y1 - 15, 165, 195, "C3")
wire((470, y1 + 15), (470, 310))
gnd(470, 310)
wire((510, y1), (600, y1))
res_h(600, 680, y1, "R4", "33 " + OHM)
wire((680, y1), (990, y1))

# ------------------------------------------------------------------ 2: Si570 -> RF OUT + sample -> GPIO35
text(30, 445, "2 %s Si570 output %s RF OUT, and a sample %s GPIO35" % (DOT, ARROW, ARROW), "h2")
sx0, sx1, sy0, sy1 = 180, 320, 475, 760
add('<rect class="p" x="%d" y="%d" width="%d" height="%d"/>' % (sx0, sy0, sx1 - sx0, sy1 - sy0))
text((sx0 + sx1) / 2, sy0 + 28, "Si570", "h2", "middle")
text((sx0 + sx1) / 2, sy0 + 46, "U3 (CMOS)", "s", "middle")
for py, inside, net in ((560, "6 VDD", "+3V3 *"), (600, "7 SDA", "ESP32 D21"), (640, "8 SCL", "ESP32 D22"),
                        (680, "2 OE", "ESP32 D4 (optional)")):
    wire((sx0 - 20, py), (sx0, py))
    text(sx0 + 8, py + 5, inside)
    text(sx0 - 26, py + 5, net, "net", "end")
wire((sx0 - 40, 720), (sx0, 720))
text(sx0 + 8, 725, "3 GND")
gnd(sx0 - 40, 720, stub=6)
for py, label in ((620, "CLK%s 5" % DASH), (680, "NC 1")):
    wire((sx1, py), (sx1 + 18, py))
    nc_mark(sx1 + 18, py)
    text(sx1 - 8, py + 5, label, "t", "end")
y2 = 560
wire((sx1, y2), (370, y2))
text(sx1 - 8, y2 + 5, "CLK+ 4", "t", "end")
dot(370, y2)
# RF output path
wire((370, y2), (400, y2))
res_h(400, 480, y2, "R7", "33 " + OHM)
wire((480, y2), (520, y2))
cap_h(520, 580, y2, "C2  100 nF", "(your existing cap)")
wire((580, y2), (680, y2))
bnc(680, y2, 605)
text(680, y2 - 44, "J2", "t", "middle")
text(704, y2 - 27, "RF OUT", "s")
text(704, y2 + 32, "to counter / load", "s")
# sample tap
res_v(370, y2, 640, "R5", "100 " + OHM)
y3 = 700
wire((370, 640), (370, y3), (450, y3))
dot(410, y3)
res_v(410, y3, 780, "R8", "100 k" + OHM, right=False)
gnd(410, 780)
schmitt(450, y3, "U2", "74LVC1G14")
supply_with_decoupling(470, y3 - 15, 620, 650, "C4")
wire((470, y3 + 15), (470, 760))
gnd(470, 760)
wire((510, y3), (600, y3))
res_h(600, 680, y3, "R6", "33 " + OHM)
wire((680, y3), (990, y3))
text(30, 800, "* VDD: the ESP32 3V3, or the Si570's own 3.3 V", "s")
text(30, 816, "  regulator with the grounds joined. Decouple", "s")
text(30, 832, "  VDD with 100 nF + 10 %sF at the chip." % MU, "s")

# ------------------------------------------------------------------ ESP32
ex0, ex1, ey0, ey1 = 1010, 1230, 100, 830
add('<rect class="p" x="%d" y="%d" width="%d" height="%d"/>' % (ex0, ey0, ex1 - ex0, ey1 - ey0))
text((ex0 + ex1) / 2, ey0 + 30, "ESP32 DevKit", "h2", "middle")
text((ex0 + ex1) / 2, ey0 + 48, "30-pin", "s", "middle")
for py, name, net in ((170, "3V3", "+3V3"), (y1, "D34", None), (340, "D27", "nc"),
                      (420, "D21  SDA", "to Si570 7"), (460, "D22  SCL", "to Si570 8"),
                      (500, "D4  OE", "to Si570 2"), (y3, "D35", None), (790, "GND", "gnd")):
    wire((ex0 - 20, py), (ex0, py))
    text(ex0 + 10, py + 5, name)
    if net == "nc":
        nc_mark(ex0 - 20, py)
    elif net == "gnd":
        wire((ex0 - 40, py), (ex0 - 20, py))
        gnd(ex0 - 40, py, stub=6)
    elif net:
        text(ex0 - 28, py + 5, net, "net", "end")
for py, a, b in ((y1, "GPIO34, input only", "reference"), (340, "internal counter", "gate: leave open"),
                 (y3, "GPIO35, input only", "Si570 sample")):
    text(ex0 + 10, py + 22, a, "s")
    text(ex0 + 10, py + 38, b, "s")

# ------------------------------------------------------------------ notes and parts
add('<rect class="box" x="30" y="860" width="760" height="205" rx="6"/>')
text(46, 887, "Notes", "h2")
notes = [
    "Reference level at J1: a sine of +6 to +13 dBm, or a 3.3 V CMOS square wave. R1 terminates",
    "the coax; R2/R3 bias U1 at the centre of its worst-case switching window (about 0.95%s2.04 V)." % DASH,
    "Fit R5 and U2 within about 2 cm of the Si570 CLK+ pin. The tap adds about 4 pF behind 100 %s." % OHM,
    "R8 holds U2's input low when OE switches the Si570 output off.",
    "GPIO34/35 are input-only and must never see more than 3.3 V. Leave D27 unconnected.",
    "The ESP32 counts the sample up to 40 MHz; above that the firmware holds the last correction.",
    "R7 + the chip's ~20 %s output %s 50 %s, so J2 can drive a 1 M%s counter over coax cleanly." % (OHM, "≈", OHM, OHM),
]
for i, n in enumerate(notes):
    text(46, 915 + i * 21, n)
add('<rect class="box" x="810" y="860" width="420" height="205" rx="6"/>')
text(826, 887, "Parts", "h2")
parts = [
    ("U1, U2", "74LVC1G14, SOT-23-5"),
    ("", "pin 2 in, 4 out, 5 VCC, 3 GND"),
    ("R1", "51 " + OHM),
    ("R2 / R3", "12 k%s / 10 k%s" % (OHM, OHM)),
    ("R4, R6, R7", "33 " + OHM),
    ("R5 / R8", "100 %s / 100 k%s" % (OHM, OHM)),
    ("C1, C3, C4", "100 nF ceramic (C2 already fitted)"),
]
for i, (ref, val) in enumerate(parts):
    text(826, 915 + i * 21, ref)
    text(916, 915 + i * 21, val)

add("</svg>")
with open(sys.argv[1], "w", encoding="utf-8") as f:
    f.write("\n".join(out) + "\n")
