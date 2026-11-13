#!/usr/bin/env python3
"""Build docs/Eco_Eco_Electrical_Schematic_and_Build_Guide.pdf.

    pip install reportlab==4.2.5
    python3 tools/make_electrical_pdf.py

Needs the DejaVu fonts. Stops with an error if a pin in the firmware headers no
longer matches the drawings, so the schematic and the code cannot drift apart.
"""
import re
import sys
from pathlib import Path

from reportlab.lib import colors
from reportlab.lib.pagesizes import A3, A4, landscape
from reportlab.lib.styles import ParagraphStyle
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.pdfmetrics import stringWidth
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (BaseDocTemplate, Flowable, Frame, NextPageTemplate, PageBreak,
                                PageTemplate, Paragraph, Spacer, Table, TableStyle)
from reportlab.platypus.tableofcontents import TableOfContents

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "docs" / "Eco_Eco_Electrical_Schematic_and_Build_Guide.pdf"
REV = "D"
DATE = "13 Nov 2026"

FONT_DIR = Path("/usr/share/fonts/truetype/dejavu")
for name, file in (("Body", "DejaVuSansCondensed.ttf"), ("BodyB", "DejaVuSansCondensed-Bold.ttf"),
                   ("Mono", "DejaVuSansMono.ttf"), ("MonoB", "DejaVuSansMono-Bold.ttf")):
    pdfmetrics.registerFont(TTFont(name, str(FONT_DIR / file)))
pdfmetrics.registerFontFamily("Body", normal="Body", bold="BodyB", italic="Body", boldItalic="BodyB")
pdfmetrics.registerFontFamily("Mono", normal="Mono", bold="MonoB", italic="Mono", boldItalic="MonoB")

HEX = colors.HexColor
INK = HEX("#14120B")
MUTE = HEX("#6B6658")
RULE = HEX("#C9C4B4")
PANEL = HEX("#F6F3E8")
GOLD = HEX("#FFCC00")
WARN = HEX("#FFF4CC")
WHITE = colors.white
RED = HEX("#C8102E")
BLACK = HEX("#111111")
YEL = HEX("#A67C00")
BLUE = HEX("#1F5FBF")
GREEN = HEX("#1E8449")
PURPLE = HEX("#7D3C98")
ORANGE = HEX("#D35400")
TEAL = HEX("#0E7C86")
GREY = HEX("#8A8578")
BROWN = HEX("#8B5A2B")


# ---------------------------------------------------------------- firmware cross-check
def check_pins():
    """The drawings below use these numbers. Fail loudly if the firmware moved a pin."""
    s3 = (ROOT / "src/s3/config_pins.h").read_text()
    cam = (ROOT / "src/cam/config_cam.h").read_text()
    audio = (ROOT / "src/audio/config_audio.h").read_text()
    expect = [
        (s3, r"PIN_BATT_ADC = (\d+)", "1"), (s3, r"PIN_MOTOR_BUS_ADC = (\d+)", "2"),
        (s3, r"PIN_LEFT_RPWM = (\d+)", "4"), (s3, r"PIN_LEFT_LPWM = (\d+)", "5"),
        (s3, r"PIN_RIGHT_RPWM = (\d+)", "6"), (s3, r"PIN_RIGHT_LPWM = (\d+)", "7"),
        (s3, r"PIN_DRIVER_EN = (\d+)", "15"), (s3, r"PIN_SONAR_TRIG\[4\] = \{([^}]+)\}", "10, 12, 14, 17"),
        (s3, r"PIN_SONAR_ECHO\[4\] = \{([^}]+)\}", "11, 13, 16, 18"), (s3, r"PIN_CAM_UART_TX = (\d+)", "41"),
        (s3, r"PIN_CAM_UART_RX = (\d+)", "42"), (s3, r"PIN_ESTOP_SENSE = (\d+)", "21"),
        (cam, r"LINK_PIN_RX = (\d+)", "13"), (cam, r"LINK_PIN_TX = (\d+)", "14"), (cam, r"PAN_PIN = (\d+)", "2"),
        (audio, r"MIC_PIN_BCLK = (\d+)", "26"), (audio, r"MIC_PIN_WS = (\d+)", "25"), (audio, r"MIC_PIN_SD = (\d+)", "34"),
        (audio, r"SPK_PIN_BCLK = (\d+)", "27"), (audio, r"SPK_PIN_LRC = (\d+)", "32"),
        (audio, r"SPK_PIN_DIN = (\d+)", "33"), (audio, r"SPK_PIN_SD = (\d+)", "23"),
    ]
    for text, pattern, want in expect:
        found = re.search(pattern, text)
        if not found or found.group(1) != want:
            sys.exit(f"Pin mismatch: {pattern} is {found and found.group(1)}, drawing says {want}. Update this PDF.")


# ---------------------------------------------------------------- drawing kit
class Pen:
    """Draws with the origin at the sheet's top-left, y growing downward."""

    def __init__(self, canv, w, h):
        self.c, self.W, self.H = canv, w, h

    def Y(self, y):
        return self.H - y

    def wire(self, *pts, color=RED, w=1.1, dash=None):
        c = self.c
        c.saveState()
        c.setStrokeColor(color)
        c.setLineWidth(w)
        c.setLineCap(1)
        c.setLineJoin(1)
        if dash:
            c.setDash(*dash)
        path = c.beginPath()
        path.moveTo(pts[0][0], self.Y(pts[0][1]))
        for x, y in pts[1:]:
            path.lineTo(x, self.Y(y))
        c.drawPath(path, stroke=1, fill=0)
        c.restoreState()

    def link(self, *pts):
        self.wire(*pts, color=GREY, w=0.8, dash=(3, 2))

    def dot(self, x, y, color=INK, r=2.1):
        self.c.setFillColor(color)
        self.c.circle(x, self.Y(y), r, stroke=0, fill=1)

    def text(self, x, y, s, size=7.5, color=INK, bold=False, mono=False, anchor="l", bg=False):
        font = ("MonoB" if bold else "Mono") if mono else ("BodyB" if bold else "Body")
        w = stringWidth(s, font, size)
        x0 = x if anchor == "l" else x - w if anchor == "r" else x - w / 2
        c = self.c
        if bg:
            c.setFillColor(WHITE)
            c.rect(x0 - 1.5, self.Y(y) - 1.8, w + 3, size + 1.5, stroke=0, fill=1)
        c.setFillColor(color)
        c.setFont(font, size)
        c.drawString(x0, self.Y(y), s)
        return w

    def lines(self, x, y, rows, size=7.5, lead=None, **kw):
        lead = lead or size + 2.2
        for i, row in enumerate(rows):
            self.text(x, y + i * lead, row, size=size, **kw)

    def rect(self, x, y, w, h, fill=None, stroke=INK, lw=0.8, r=2.5):
        c = self.c
        c.saveState()
        c.setLineWidth(lw)
        if stroke:
            c.setStrokeColor(stroke)
        if fill:
            c.setFillColor(fill)
        c.roundRect(x, self.Y(y + h), w, h, r, stroke=1 if stroke else 0, fill=1 if fill else 0)
        c.restoreState()

    def zone(self, x, y, w, h, title):
        self.rect(x, y, w, h, stroke=RULE, r=0)
        self.rect(x, y, w, 15, fill=PANEL, stroke=RULE, r=0)
        self.text(x + 6, y + 11, title, size=8, bold=True)

    def part(self, x, y, w, h, ref, title="", rows=(), fill=PANEL, size=7.2):
        self.rect(x, y, w, h, fill=fill)
        self.text(x + 4, y + 10, ref + ("  " + title if title else ""), size=size + 0.3, bold=True)
        self.lines(x + 4, y + 20, rows, size=size)

    def tag(self, x, y, s, side="r", color=INK, size=7.2, bold=False, mono=False):
        """Net label: the tip touches (x, y). The same name elsewhere is the same net."""
        font = ("MonoB" if bold else "Mono") if mono else ("BodyB" if bold else "Body")
        tw = stringWidth(s, font, size)
        h, d = size + 4, 5
        sign = 1 if side == "r" else -1
        c = self.c
        c.saveState()
        c.setStrokeColor(color)
        c.setFillColor(WHITE)
        c.setLineWidth(0.7)
        path = c.beginPath()
        path.moveTo(x, self.Y(y))
        path.lineTo(x + sign * d, self.Y(y - h / 2))
        path.lineTo(x + sign * (d + tw + 6), self.Y(y - h / 2))
        path.lineTo(x + sign * (d + tw + 6), self.Y(y + h / 2))
        path.lineTo(x + sign * d, self.Y(y + h / 2))
        path.close()
        c.drawPath(path, stroke=1, fill=1)
        c.restoreState()
        left = x + d + 3 if side == "r" else x - d - 3 - tw
        self.text(left, y + size * 0.35, s, size=size, color=color, bold=bold, mono=mono)
        return d + tw + 6

    def fuse(self, x, y, label, vert=False, color=RED):
        w, h = (9, 24) if vert else (24, 9)
        self.rect(x - w / 2, y - h / 2, w, h, fill=WHITE, stroke=color, r=1)
        if vert:
            self.wire((x, y - h / 2), (x, y + h / 2), color=color, w=0.7)
            self.text(x + 8, y + 3, label, size=7, bold=True)
        else:
            self.wire((x - w / 2, y), (x + w / 2, y), color=color, w=0.7)
            self.text(x, y - 7, label, size=7, bold=True, anchor="c")

    def res(self, x, y, label="", vert=False, color=INK, side="r"):
        w, h = (7, 20) if vert else (20, 7)
        self.rect(x - w / 2, y - h / 2, w, h, fill=WHITE, stroke=color, r=0.5)
        if not label:
            return
        if vert:
            if side == "r":
                self.text(x + 6, y + 2.5, label, size=6.8)
            else:
                self.text(x - 6, y + 2.5, label, size=6.8, anchor="r")
        else:
            self.text(x, y - 6, label, size=6.8, anchor="c")

    def cap(self, x, y, label="", polar=False, color=INK, side="r"):
        """Vertical capacitor centred at (x, y): leads end 2.2 above and below centre."""
        c = self.c
        c.saveState()
        c.setStrokeColor(color)
        c.setLineWidth(1.2)
        for dy in (-2.2, 2.2):
            c.line(x - 6, self.Y(y + dy), x + 6, self.Y(y + dy))
        c.restoreState()
        if polar:
            self.text(x + 7, y - 2, "+", size=7)
        if label:
            if side == "r":
                self.text(x + 10, y + 2.5, label, size=6.8)
            else:
                self.text(x - 10, y + 2.5, label, size=6.8, anchor="r")

    def gnd(self, x, y, stub=5):
        self.wire((x, y), (x, y + stub), color=BLACK)
        for dy, half in ((0, 6), (2.6, 4), (5.2, 1.8)):
            self.wire((x - half, y + stub + dy), (x + half, y + stub + dy), color=BLACK, w=1)

    def xt(self, x, y, label, y2=None):
        top, bottom = y - 7, (y2 if y2 is not None else y) + 7
        self.rect(x - 5, top, 10, bottom - top, fill=GOLD, stroke=INK, lw=0.7, r=1)
        self.wire((x, top), (x, bottom), color=INK, w=0.6)
        self.text(x, top - 2.5, label, size=6.8, bold=True, anchor="c")

    def _map(self, x, y, vert):
        return (lambda u, v: (x - v, y + u)) if vert else (lambda u, v: (x + u, y + v))

    def no_contact(self, x, y, vert=False, half=15, color=RED):
        m = self._map(x, y, vert)
        a, b = m(-half, 0), m(half, 0)
        self.wire(a, m(half - 2, -9), color=color, w=1.2)
        for px, py in (a, b):
            self.dot(px, py, color=color, r=1.9)

    def nc_contact(self, x, y, vert=False, half=15, color=RED):
        m = self._map(x, y, vert)
        a, b = m(-half, 0), m(half, 0)
        self.wire(a, m(half, 3), color=color, w=1.2)
        self.wire(b, m(half, 5), color=color, w=1.2)
        for px, py in (a, b):
            self.dot(px, py, color=color, r=1.9)

    def coil(self, x, y, w=36, h=15):
        self.rect(x - w / 2, y - h / 2, w, h, fill=WHITE, stroke=RED, r=0.5)
        self.wire((x - w / 2, y + h / 2), (x + w / 2, y - h / 2), color=RED, w=0.7)

    def diode(self, x, y, cathode="l", color=INK):
        s = -1 if cathode == "l" else 1
        c = self.c
        c.saveState()
        c.setStrokeColor(color)
        c.setFillColor(WHITE)
        c.setLineWidth(1)
        path = c.beginPath()
        path.moveTo(x - s * 6, self.Y(y - 5))
        path.lineTo(x + s * 4, self.Y(y))
        path.lineTo(x - s * 6, self.Y(y + 5))
        path.close()
        c.drawPath(path, stroke=1, fill=1)
        c.line(x + s * 4, self.Y(y - 5), x + s * 4, self.Y(y + 5))
        c.restoreState()

    def motor(self, x, y, label, r=12):
        self.c.setStrokeColor(INK)
        self.c.setFillColor(WHITE)
        self.c.setLineWidth(1)
        self.c.circle(x, self.Y(y), r, stroke=1, fill=1)
        self.text(x, y + 3, "M", size=9, bold=True, anchor="c")
        self.text(x + r + 3, y + 3, label, size=7, bold=True)

    def grid(self, x, y, widths, rows, size=7.2, lead=12.5, head=True):
        """A small table drawn inside a sheet."""
        total = sum(widths)
        for i, row in enumerate(rows):
            top = y + i * lead
            if i == 0 and head:
                self.rect(x, top, total, lead, fill=INK, stroke=INK, r=0)
            elif i % 2 == 0:
                self.rect(x, top, total, lead, fill=PANEL, stroke=None, r=0)
            cx = x
            for w, cell in zip(widths, row):
                self.text(cx + 3, top + lead - 3.6, str(cell), size=size, bold=(i == 0 and head),
                          color=WHITE if (i == 0 and head) else INK)
                cx += w
        self.rect(x, y, total, lead * len(rows), stroke=RULE, r=0)
        return y + lead * len(rows)

    def note(self, x, y, w, rows, title=None, fill=WARN, size=7.5, lead=10):
        h = lead * len(rows) + (lead + 2 if title else 0) + 6
        self.rect(x, y, w, h, fill=fill, stroke=RULE)
        top = y + 10
        if title:
            self.text(x + 5, top, title, size=size, bold=True)
            top += lead + 1
        self.lines(x + 5, top, rows, size=size, lead=lead)
        return y + h


SHEETS = []


def sheet(number, title, status, size):
    """Registers a drawing function as a numbered sheet."""
    def wrap(fn):
        SHEETS.append(dict(number=number, title=title, status=status, size=size, fn=fn))
        return fn
    return wrap


class Sheet(Flowable):
    def __init__(self, info):
        super().__init__()
        self.info = info
        page = landscape(A3) if info["size"] == "A3" else landscape(A4)
        self.width, self.height = page[0] - 36, page[1] - 36.5

    def wrap(self, *_):
        return self.width, self.height

    def draw(self):
        c, info = self.canv, self.info
        p = Pen(c, self.width, self.height)
        info["fn"](p, self.width, self.height - 30)
        p.rect(0, 0, self.width, self.height, stroke=INK, lw=1, r=0)
        y = self.height - 30
        p.rect(0, y, self.width, 30, fill=INK, stroke=INK, r=0)
        p.text(10, y + 19, "ECO ECO ROVER", size=10, bold=True, color=GOLD)
        p.text(112, y + 13, f"Sheet {info['number']}: {info['title']}", size=9.5, bold=True, color=WHITE)
        p.text(112, y + 25, info["status"], size=7.2, color=GOLD)
        right = f"Sheet {info['number']} of {len(SHEETS)}   ·   Rev {REV}   ·   {DATE}   ·   Team Eco eco"
        p.text(self.width - 10, y + 19, right, size=7.5, color=WHITE, anchor="r")


# ---------------------------------------------------------------- sheet 1, master schematic
S3_LEFT = [
    ("3V3", "U4, U5 VCC", RED), ("3V3", "not used", GREY), ("RST", "not used", GREY),
    ("GPIO4", "U4 RPWM, left", YEL), ("GPIO5", "U4 LPWM, left", BLUE),
    ("GPIO6", "U5 RPWM, right", YEL), ("GPIO7", "U5 LPWM, right", BLUE),
    ("GPIO15", "U4, U5 R_EN + L_EN", BROWN), ("GPIO16", "US3 Echo, via R15", PURPLE),
    ("GPIO17", "US4 Trig", GREEN), ("GPIO18", "US4 Echo, via R17", PURPLE),
    ("GPIO8", "spare", GREY), ("GPIO3", "avoid: strapping", GREY), ("GPIO46", "avoid: strapping", GREY),
    ("GPIO9", "spare", GREY), ("GPIO10", "US1 Trig", GREEN), ("GPIO11", "US1 Echo, via R11", PURPLE),
    ("GPIO12", "US2 Trig", GREEN), ("GPIO13", "US2 Echo, via R13", PURPLE), ("GPIO14", "US3 Trig", GREEN),
    ("5V", "+5V LOGIC (input)", RED), ("GND", "GND", BLACK),
]
S3_RIGHT = [
    ("GND", "GND", BLACK), ("TX 43", "avoid: serial monitor", GREY), ("RX 44", "avoid: serial monitor", GREY),
    ("GPIO1", "BT2 sense, R21 (optional)", TEAL), ("GPIO2", "bus sense R23 = E-stop", TEAL),
    ("GPIO42", "U2 IO14 (camera TX)", TEAL), ("GPIO41", "U2 IO13 (camera RX)", TEAL),
    ("GPIO40", "spare", GREY), ("GPIO39", "spare", GREY), ("GPIO38", "avoid: RGB LED", GREY),
    ("GPIO37", "avoid: PSRAM", GREY), ("GPIO36", "avoid: PSRAM", GREY), ("GPIO35", "avoid: PSRAM", GREY),
    ("GPIO0", "avoid: strapping", GREY), ("GPIO45", "avoid: strapping", GREY), ("GPIO48", "avoid: RGB LED", GREY),
    ("GPIO47", "avoid", GREY), ("GPIO21", "not used: 1-pole E-stop", GREY),
    ("GPIO20", "avoid: USB", GREY), ("GPIO19", "avoid: USB", GREY), ("GND", "GND", BLACK), ("GND", "GND", BLACK),
]


def motor_pair(p, xout, y_ma, y_mb, xa, xb, y1, y2, m1, m2, r):
    """Two motors in parallel: M+ feeds one common side, M− the other."""
    xm = (xa + xb) / 2
    p.wire((xout, y_ma), (xa, y_ma), color=GREEN, w=1.4)
    p.wire((xa, y1), (xa, y2), color=GREEN, w=1.4)
    p.dot(xa, y_ma, color=GREEN)
    p.wire((xout, y_mb), (xb, y_mb), (xb, y1), color=BLUE, w=1.4)
    for y, ref in ((y1, m1), (y2, m2)):
        p.wire((xa, y), (xm - r, y), color=GREEN, w=1.4)
        p.wire((xm + r, y), (xb, y), color=BLUE, w=1.4)
        p.motor(xm, y, "", r=r)
        p.text(xb + 5, y + 3, ref, size=7.5, bold=True)
    p.dot(xb, y2, color=BLUE)


def power_row(p, y, fuse, conn, xbus=470):
    p.wire((xbus, y), (xbus + 23, y))
    p.fuse(xbus + 36, y, fuse)
    p.wire((xbus + 48, y), (xbus + 75, y))
    p.xt(xbus + 82, y, conn)
    p.wire((xbus + 87, y), (xbus + 130, y))


@sheet(1, "Master schematic: every board, every connected pin",
       "Design to build from. Power side not built yet. Tested pins are listed in Section 10.", "A3")
def sheet_master(p, W, H):
    # ---- power zone
    p.zone(0, 0, W, 236, "POWER AND E-STOP: two 3S LiPo packs  (detail on Sheets 2 and 3)")
    # motor pack
    p.part(8, 24, 72, 62, "BT1", "", ["3S LiPo 850 mAh", "motor pack", "12.6 V full"])
    p.text(76, 45, "+", size=8, bold=True, anchor="r")
    p.text(76, 77, "−", size=8, bold=True, anchor="r")
    p.wire((80, 42), (122, 42), w=1.7)
    p.xt(101, 42, "J1 XT30", y2=74)
    p.fuse(135, 42, "F1 10 A")
    p.wire((147, 42), (240, 42), w=1.7)
    p.text(162, 37, "BT1+ 9.9–12.6 V", size=7, color=RED, bold=True)
    p.no_contact(255, 42)
    p.text(255, 26, "SW1 E-STOP, 2-pin", size=6.8, bold=True, anchor="c", color=RED)
    p.wire((270, 42), (470, 42), w=1.7)
    p.xt(305, 42, "J2 XT30", y2=74)
    p.dot(470, 42, color=RED)
    p.text(466, 56, "MOTOR BUS", size=7, color=RED, bold=True, anchor="r")
    p.text(466, 65, "0 V or 9.9–12.6 V", size=7, color=RED, anchor="r")
    p.wire((80, 74), (432, 74), color=BLACK, w=1.7)
    p.gnd(432, 74, stub=3)
    p.text(376, 98, "STAR GND, near driver B−", size=6.8, bold=True)
    p.lines(112, 98, ["SW1 is a plain 2-pin switch, ON = run. It cuts the", "motors and the servo in hardware. The S3 sees it only",
                      "as motor-bus voltage on GPIO2, latches ESTOP and", "refuses RESET until the bus is back (Sheet 3)."], size=6.8, lead=8.6, color=MUTE)
    p.wire((470, 30), (470, 146), w=1.7)
    for y, conn, dest in ((30, "J3 XT30", "U4 B+, left driver"), (58, "J4 XT30", "U5 B+, right driver")):
        p.wire((470, y), (552, y))
        p.xt(559, y, conn)
        p.wire((564, y), (600, y))
        p.tag(600, y, dest, color=RED)
    p.wire((470, 88), (552, 88))
    p.xt(559, 88, "J5 XT30")
    p.wire((564, 88), (600, 88))
    p.part(600, 72, 92, 32, "U7", "XL4015", ["servo 5.0 V"])
    p.wire((692, 88), (706, 88))
    p.tag(706, 88, "+5V SERVO: SV1 red", color=RED)
    p.wire((470, 118), (502, 118))
    p.res(512, 118, "R25 1 k 0.5 W, bleed")
    p.wire((522, 118), (540, 118), color=BLACK)
    p.gnd(540, 118, stub=2)
    p.wire((470, 146), (488, 146))
    p.tag(488, 146, "R23: E-stop sense, U1 GPIO2 (Sheet 3)", color=RED)

    # ---- logic pack
    p.part(8, 186, 72, 48, "BT2", "", ["3S LiPo 850 mAh", "logic pack"])
    p.text(76, 207, "+", size=8, bold=True, anchor="r")
    p.text(76, 229, "−", size=8, bold=True, anchor="r")
    p.wire((80, 204), (122, 204), w=1.4)
    p.xt(101, 204, "J6 XT30", y2=226)
    p.fuse(135, 204, "F2 3 A")
    p.wire((147, 204), (240, 204), w=1.4)
    p.no_contact(255, 204)
    p.text(255, 188, "SW2 POWER, 2-pin", size=6.8, bold=True, anchor="c")
    p.wire((270, 204), (590, 204), w=1.4)
    p.dot(300, 204, color=RED)
    p.wire((300, 204), (300, 166))
    p.tag(300, 166, "R21: BT2 sense, U1 GPIO1 (optional)", color=RED, size=6.6)
    p.dot(330, 204, color=RED)
    p.wire((330, 204), (330, 182))
    p.tag(330, 182, "FAN1 12 V, current unknown", color=RED, size=6.6)
    p.xt(470, 204, "J7 XT30")
    p.part(590, 192, 110, 26, "U6", "XL4015 logic", [])
    p.text(594, 213, "5.0 V", size=7)
    p.wire((700, 204), (714, 204))
    p.tag(714, 204, "+5V LOGIC: U1 5V, U2 5V, US1–US4 VCC, optional U3 5V and U9 VIN", color=RED, bold=True)
    p.wire((80, 226), (150, 226), color=BLACK, w=1.4)
    p.gnd(150, 226, stub=1)
    p.text(160, 233, "same STAR GND", size=6.4, color=MUTE)

    # ---- legend
    x, y = 842, 24
    p.rect(x, y, 305, 172, fill=PANEL, stroke=RULE)
    p.text(x + 8, y + 13, "How to read this sheet", size=8, bold=True)
    legend = [(RED, "Red: every positive line. Label the voltage at each junction."),
              (BLACK, "Black: GND and battery negative."),
              (YEL, "Yellow: RPWM (forward). Blue: LPWM (reverse)."),
              (BROWN, "Brown: motor driver EN."),
              (GREEN, "Green: sonar Trig. Violet: sonar Echo."),
              (TEAL, "Teal: camera link and voltage sense (GPIO2 = E-stop)."),
              (ORANGE, "Orange: servo signal.")]
    for i, (col, label) in enumerate(legend):
        yy = y + 26 + i * 12
        p.wire((x + 8, yy), (x + 28, yy), color=col, w=1.6)
        p.text(x + 34, yy + 2.5, label, size=7.2)
    rows = ["A tag points to the same net elsewhere on the sheet: follow the name.",
            "Crossing wires without a dot are not connected. Dashed grey is a",
            "mechanical link. Gold blocks are XT connectors.",
            "Header order follows ESP32-S3-DevKitC-1. If your board's silkscreen",
            "differs from this sheet, stop and check before you solder."]
    p.lines(x + 8, y + 112, rows, size=7.2, lead=9.4)
    p.text(x + 8, y + 164, "Print on A3, or zoom in on screen.", size=7.2, bold=True)

    top = 242
    # ---- drive zone
    p.zone(0, top, 405, 262, "DRIVE: one BTS7960 (IBT-2) per side, two motors in parallel  (Sheet 4)")
    for i, (ref, side, rpwm, lpwm, r1, r2, r3, fb, jb, m1, m2) in enumerate((
            ("U4", "left", "GPIO4", "GPIO5", "R1", "R2", "R5", "F5", "J3", "M1", "M2"),
            ("U5", "right", "GPIO6", "GPIO7", "R3", "R4", "R6", "F6", "J4", "M3", "M4"))):
        y = top + 22 + i * 120
        p.part(128, y, 92, 112, ref, f"BTS7960 {side}", [])
        for pin, py, dest, col in (("VCC", 20, "U1 3V3", RED), ("GND", 34, "GND", BLACK),
                                   ("RPWM", 48, f"U1 {rpwm}", YEL), ("LPWM", 62, f"U1 {lpwm}", BLUE),
                                   ("R_EN+L_EN", 76, "U1 GPIO15", BROWN)):
            p.tag(62, y + py, dest, side="l", color=col, size=6.8)
            p.wire((62, y + py), (128, y + py), color=col)
            p.text(132, y + py + 2.5, pin, size=6.8, mono=True)
        # Pull-downs. A drop crosses the lines below it without a dot: not connected.
        for px, py, col in ((80, 48, YEL), (96, 62, BLUE), (112, 76, BROWN)):
            p.dot(px, y + py, color=col)
            p.wire((px, y + py), (px, y + 82), color=col)
            p.res(px, y + 92, "", vert=True)
            p.gnd(px, y + 102, stub=1)
        p.text(74, y + 92, f"{r1}, {r2}, {r3}", size=6.4, bold=True, anchor="r")
        p.text(74, y + 101, "10 k each", size=6.4, anchor="r")
        p.text(132, y + 96, "R_IS  n.c.", size=6.4, mono=True, color=MUTE)
        p.text(132, y + 106, "L_IS  n.c.", size=6.4, mono=True, color=MUTE)
        p.text(216, y + 25, "B+", size=7, mono=True, anchor="r")
        p.text(216, y + 45, "B−", size=7, mono=True, anchor="r")
        p.text(216, y + 73, "M+", size=7, mono=True, anchor="r")
        p.text(216, y + 107, "M−", size=7, mono=True, anchor="r")
        p.wire((220, y + 22), (232, y + 22))
        p.tag(232, y + 22, f"{jb}: MOTOR BUS", color=RED, size=6.8)
        p.wire((220, y + 42), (232, y + 42), color=BLACK)
        p.tag(232, y + 42, f"{jb} −: STAR GND", color=BLACK, size=6.8)
        p.text(250, y + 66, "18 AWG", size=6.4, color=MUTE)
        motor_pair(p, 220, y + 70, y + 104, 292, 370, y + 62, y + 90, m1, m2, 11)

    # ---- sonar zone
    sy = top + 268
    p.zone(0, sy, 405, H - sy - 4, "SONARS: HC-SR04 ×4, each Echo through its own divider  (Sheet 5)")
    block = [("US1", "front centre", "GPIO10", "GPIO11", "R11", "R12"), ("US2", "front left", "GPIO12", "GPIO13", "R13", "R14"),
             ("US3", "front right", "GPIO14", "GPIO16", "R15", "R16"), ("US4", "rear", "GPIO17", "GPIO18", "R17", "R18")]
    for i, (ref, where, trig, echo, ra, rb) in enumerate(block):
        x = 6 + (i % 2) * 200
        y = sy + 20 + (i // 2) * 121
        p.part(x, y, 54, 112, ref, "", [where], size=6.6)
        for pin, py in (("VCC", 34), ("Trig", 52), ("Echo", 70), ("GND", 100)):
            p.text(x + 52, y + py + 2.5, pin, size=6.8, mono=True, anchor="r")
        p.wire((x + 54, y + 34), (x + 104, y + 34))
        p.tag(x + 104, y + 34, "+5V LOGIC", color=RED, size=6.6)
        p.wire((x + 54, y + 52), (x + 104, y + 52), color=GREEN)
        p.tag(x + 104, y + 52, f"U1 {trig}", color=GREEN, size=6.6)
        p.wire((x + 54, y + 70), (x + 68, y + 70), color=PURPLE)
        p.res(x + 78, y + 70, "", color=PURPLE)
        p.text(x + 78, y + 64, f"{ra} 1 k", size=6.3, anchor="c")
        p.wire((x + 88, y + 70), (x + 104, y + 70), color=PURPLE)
        p.dot(x + 96, y + 70, color=PURPLE)
        p.tag(x + 104, y + 70, f"U1 {echo}", color=PURPLE, size=6.6)
        p.wire((x + 96, y + 70), (x + 96, y + 78), color=PURPLE)
        p.res(x + 96, y + 87, "", vert=True, color=PURPLE)
        p.text(x + 102, y + 90, f"{rb} 1.8 k", size=6.3)
        p.wire((x + 54, y + 100), (x + 70, y + 100), color=BLACK)
        p.gnd(x + 70, y + 100, stub=1)
        p.gnd(x + 96, y + 97, stub=1)
        p.text(x + 112, y + 104, f"C{11 + i} 100 µF + C{15 + i} 100 nF", size=6.1, color=MUTE)

    # ---- S3
    zx, zw = 411, 330
    p.zone(zx, top, zw, H - top - 4, "U1  ESP32-S3 MAIN CONTROLLER (N16R8, DevKitC-1 header)")
    bx, bw, ry, step = 528, 96, top + 34, 20.6
    p.rect(bx, ry - 14, bw, step * 22 + 8, fill=PANEL)
    p.text(bx + bw / 2, ry + 10 * step, "U1", size=14, bold=True, anchor="c")
    p.text(bx + bw / 2, ry + 10 * step + 12, "ESP32-S3", size=8, anchor="c")
    for i, (pin, dest, col) in enumerate(S3_LEFT):
        y = ry + i * step
        used = col != GREY
        p.text(bx + 4, y, pin, size=7, mono=True, bold=used, color=INK if used else MUTE)
        p.text(bx - 3, y - 7, f"{i + 1}", size=5.4, color=MUTE, anchor="r")
        if used:
            p.wire((bx, y - 2.5), (bx - 14, y - 2.5), color=col)
            p.tag(bx - 14, y - 2.5, dest, side="l", color=col, size=6.8)
        else:
            p.text(bx - 6, y, dest, size=6.6, color=MUTE, anchor="r")
    for i, (pin, dest, col) in enumerate(S3_RIGHT):
        y = ry + i * step
        used = col != GREY
        p.text(bx + bw - 4, y, pin, size=7, mono=True, bold=used, color=INK if used else MUTE, anchor="r")
        p.text(bx + bw + 3, y - 7, f"{i + 1}", size=5.4, color=MUTE)
        if used:
            p.wire((bx + bw, y - 2.5), (bx + bw + 14, y - 2.5), color=col)
            p.tag(bx + bw + 14, y - 2.5, dest, color=col, size=6.8)
        else:
            p.text(bx + bw + 6, y, dest, size=6.6, color=MUTE)

    # ---- camera and servo
    cx = 747
    cw = W - cx
    p.zone(cx, top, cw, 262, "U2  ESP32-CAM AND SV1  SG90 PAN SERVO  (Sheet 6)")
    ux, uw, uy, st = 842, 84, top + 42, 22
    p.rect(ux, uy - 16, uw, st * 8 + 8, fill=PANEL)
    p.text(ux + uw / 2, uy - 19, "U2 ESP32-CAM (AI-Thinker)", size=7, bold=True, anchor="c")
    cam_left = [("5V", "+5V LOGIC", RED), ("GND", "GND", BLACK), ("IO12", "avoid: strapping", GREY), ("IO13", "U1 GPIO41", TEAL),
                ("IO15", "avoid", GREY), ("IO14", "U1 GPIO42", TEAL), ("IO2", "SV1 orange", ORANGE), ("IO4", "flash LED, off", GREY)]
    cam_right = ["3V3 not used", "IO16 PSRAM", "IO0 flash mode", "GND", "VCC not used", "U0R program", "U0T program", "GND"]
    for i, (pin, dest, col) in enumerate(cam_left):
        y = uy + i * st
        used = col != GREY
        p.text(ux + 4, y, pin, size=7, mono=True, bold=used, color=INK if used else MUTE)
        if used:
            p.wire((ux, y - 2.5), (ux - 12, y - 2.5), color=col)
            p.tag(ux - 12, y - 2.5, dest, side="l", color=col, size=6.8)
        else:
            p.text(ux - 5, y, dest, size=6.6, color=MUTE, anchor="r")
    for i, label in enumerate(cam_right):
        p.text(ux + uw - 3, uy + i * st, label.split(" ")[0], size=7, mono=True, color=MUTE, anchor="r")
        if " " in label:
            p.text(ux + uw + 4, uy + i * st, label.split(" ", 1)[1], size=6.4, color=MUTE)
    p.note(cx + 6, top + 214, 250, ["C1 470 µF + C2 100 nF across 5V and GND, at the pin, short wires.",
                                    "Leave the microSD slot empty: it shares IO13 and IO14 with",
                                    "the link."], size=6.5, lead=8.4)
    sx = 996
    p.part(sx, top + 72, 92, 76, "SV1", "SG90", ["180° positional"], size=6.8)
    p.text(sx + 4, top + 111, "orange: signal", size=6.8, color=ORANGE)
    p.text(sx + 4, top + 125, "red: V+", size=6.8, color=RED)
    p.text(sx + 4, top + 139, "brown: GND", size=6.8)
    p.wire((sx, top + 108), (sx - 8, top + 108), color=ORANGE)
    p.tag(sx - 8, top + 108, "U2 IO2", side="l", color=ORANGE, size=6.6)
    p.wire((sx + 92, top + 122), (sx + 102, top + 122))
    p.tag(sx + 102, top + 122, "+5V SERVO", color=RED, size=6.4)
    p.wire((sx + 92, top + 136), (sx + 104, top + 136), color=BLACK)
    p.gnd(sx + 104, top + 136, stub=2)
    p.lines(cx + 266, top + 166, ["C3 470 µF across V+ and GND, at the servo.",
                                  "Brown also joins U2 GND: the signal needs it.",
                                  "U7 is on the motor bus, so the E-stop stops",
                                  "the servo too. Keep 45–135° travel."], size=6.6, lead=9.2, color=MUTE)

    # ---- voltage sense
    vy = top + 268
    p.zone(cx, vy, 190, H - vy - 4, "VOLTAGE SENSE  (Sheet 7)")
    for i, (src, r1, r2, c1, dest) in enumerate((("BT2 +", "R21", "R22", "C21", "U1 GPIO1"), ("MOTOR BUS", "R23", "R24", "C22", "U1 GPIO2"))):
        y = vy + 44 + i * 118
        p.text(cx + 8, y - 14, ("logic pack after SW2, optional", "after SW1: the E-stop sense, required")[i], size=6.4, color=MUTE)
        p.tag(cx + 52, y, src, side="l", color=RED, size=6.6)
        p.wire((cx + 52, y), (cx + 64, y))
        p.res(cx + 76, y, f"{r1} 100 k")
        p.wire((cx + 86, y), (cx + 128, y), color=TEAL)
        p.dot(cx + 106, y, color=TEAL)
        p.tag(cx + 128, y, dest, color=TEAL, size=6.6)
        p.wire((cx + 106, y), (cx + 106, y + 14), color=TEAL)
        p.res(cx + 106, y + 24, "", vert=True)
        p.text(cx + 100, y + 27, f"{r2} 22 k", size=6.6, anchor="r")
        p.wire((cx + 106, y + 34), (cx + 106, y + 44), color=BLACK)
        p.gnd(cx + 106, y + 44, stub=1)
        p.wire((cx + 106, y + 9), (cx + 126, y + 9), (cx + 126, y + 20), color=TEAL)
        p.cap(cx + 126, y + 22, f"{c1}")
        p.text(cx + 136, y + 33, "100 nF", size=6.4)
        p.wire((cx + 126, y + 24), (cx + 126, y + 38), color=BLACK)
        p.gnd(cx + 126, y + 38, stub=1)

    # ---- audio
    ax = cx + 196
    p.zone(ax, vy, W - ax, H - vy - 4, "U3  AUDIO BOARD, optional (Sheet 7)")
    p.part(ax + 6, vy + 22, 90, 88, "U8", "INMP441 mic", [], size=6.8)
    for i, row in enumerate(("VDD: U3 3V3", "GND, L/R: GND", "SCK: GPIO26", "WS: GPIO25", "SD: GPIO34")):
        p.text(ax + 10, vy + 44 + i * 12, row, size=6.8)
    p.part(ax + 102, vy + 22, W - ax - 108, 88, "U9", "MAX98357A amp", [], size=6.8)
    for i, row in enumerate(("VIN: +5V LOGIC", "BCLK: GPIO27", "LRC: GPIO32", "DIN: GPIO33", "SD: GPIO23")):
        p.text(ax + 106, vy + 44 + i * 12, row, size=6.8)
    p.lines(ax + 8, vy + 126, ["U3 = ESP32 DevKit. 5V pin on +5V LOGIC,",
                               "GND on GND. Its pin names above are its own",
                               "GPIOs, not the S3's.", "",
                               "LS1 4 Ω 3 W speaker on U9 + and −.",
                               "U9 GND on GND, GAIN pin open (9 dB).", "",
                               "No wire to U1 except power and ground.",
                               "It talks over Wi-Fi only."], size=6.8, lead=9)


# ---------------------------------------------------------------- sheet 2, power
@sheet(2, "Power distribution, fuses and connectors", "Design. Not built or measured yet. Two 3S LiPo packs: BT1 motors and servo, BT2 logic.", "A4")
def sheet_power(p, W, H):
    p.text(12, 18, "Each junction shows its voltage, as rule 3.2.3 asks. Write the same label on the wire or the board.", size=8, color=MUTE)
    # ---- motor pack
    y = 70
    p.part(12, y - 22, 76, 66, "BT1", "", ["3S LiPo 850 mAh", "motor pack", "12.6 V full"])
    p.wire((88, y), (150, y), w=2)
    p.wire((88, y + 34), (360, y + 34), color=BLACK, w=2)
    p.xt(110, y, "J1 XT30", y2=y + 34)
    p.fuse(165, y, "F1 10 A", color=RED)
    p.text(165, y + 16, "at the pack", size=6.6, color=MUTE, anchor="c")
    p.wire((177, y), (232, y), w=2)
    p.no_contact(250, y, half=18)
    p.text(250, y - 16, "SW1 E-STOP", size=7.5, bold=True, anchor="c", color=RED)
    p.text(250, y + 16, "2-pin, Sheet 3", size=6.6, color=MUTE, anchor="c")
    p.wire((268, y), (400, y), w=2)
    p.xt(300, y, "J2 XT30", y2=y + 34)
    p.text(356, y - 9, "MOTOR BUS", size=7.5, color=RED, bold=True, anchor="c")
    p.text(356, y + 11, "0 V or 9.9–12.6 V", size=6.6, color=RED, anchor="c")
    p.gnd(360, y + 34, stub=4)
    p.text(350, y + 47, "STAR GND", size=7.5, bold=True, anchor="r")
    p.dot(400, y, color=RED)
    p.text(120, y - 28, "18 AWG red", size=6.8, color=MUTE)
    p.text(120, y + 46, "18 AWG black", size=6.8, color=MUTE)
    rows = [(y, "J3 XT30", "U4 BTS7960 left, B+", "18 AWG. M1, M2 through U4"),
            (y + 28, "J4 XT30", "U5 BTS7960 right, B+", "18 AWG. M3, M4 through U5"),
            (y + 56, "J5 XT30", "U7 XL4015 servo, IN+", "18 AWG. U7 out 5.0 V: +5V SERVO to SV1 red, C3 470 µF")]
    p.wire((400, y), (400, y + 110), w=2)
    for ry, conn, dest, why in rows:
        p.wire((400, ry), (440, ry))
        p.xt(447, ry, conn)
        p.wire((452, ry), (474, ry))
        p.tag(474, ry, dest, color=RED, bold=True)
        p.text(478, ry + 13, why, size=6.6, color=MUTE)
    p.wire((400, y + 84), (430, y + 84))
    p.res(442, y + 84, "R25 1 kΩ 0.5 W, bleed")
    p.wire((452, y + 84), (470, y + 84), color=BLACK)
    p.gnd(470, y + 84, stub=2)
    p.wire((400, y + 110), (474, y + 110))
    p.tag(474, y + 110, "R23 to U1 GPIO2: E-stop sense (Sheet 3)", color=RED)
    # ---- logic pack
    ly = 236
    p.part(12, ly - 22, 76, 66, "BT2", "", ["3S LiPo 850 mAh", "logic pack", "12.6 V full"])
    p.wire((88, ly), (150, ly), w=1.5)
    p.wire((88, ly + 34), (200, ly + 34), color=BLACK, w=1.5)
    p.gnd(200, ly + 34, stub=4)
    p.text(212, ly + 46, "same STAR GND", size=7, bold=True)
    p.xt(110, ly, "J6 XT30", y2=ly + 34)
    p.fuse(165, ly, "F2 3 A", color=RED)
    p.text(165, ly + 16, "at the pack", size=6.6, color=MUTE, anchor="c")
    p.wire((177, ly), (232, ly), w=1.5)
    p.no_contact(250, ly, half=18)
    p.text(250, ly - 16, "SW2 POWER", size=7.5, bold=True, anchor="c")
    p.text(250, ly + 16, "2-pin", size=6.6, color=MUTE, anchor="c")
    p.wire((268, ly), (420, ly), w=1.5)
    p.dot(300, ly, color=RED)
    p.wire((300, ly), (300, ly + 30))
    p.tag(300, ly + 30, "FAN1 12 V fan, current unknown", color=RED)
    p.dot(330, ly, color=RED)
    p.wire((330, ly), (330, ly - 30))
    p.tag(330, ly - 30, "R21 to U1 GPIO1: BT2 sense (optional)", color=RED, size=7)
    p.xt(380, ly, "J7 XT30")
    p.part(420, ly - 18, 96, 38, "U6", "XL4015 logic", ["5.0 V, input 8–36 V"])
    p.wire((516, ly), (566, ly), w=1.5)
    p.text(540, ly - 5, "+5.0 V", size=7.5, color=RED, bold=True, anchor="c")
    p.dot(566, ly, color=RED)
    loads = ["U1 ESP32-S3, 5V pin", "U2 ESP32-CAM, 5V pin + C1, C2", "US1 to US4, VCC", "U3 audio board, 5V pin (optional)", "U9 amp, VIN (optional)"]
    p.wire((566, ly - 10), (566, ly + 46), w=1.2)
    for i, load in enumerate(loads):
        yy = ly - 10 + i * 14
        p.wire((566, yy), (582, yy))
        p.tag(582, yy, load, color=RED, size=7)
    p.text(584, ly + 64, "22 AWG red. Logic stays on when the E-stop is OFF,", size=6.8, color=MUTE)
    p.text(584, ly + 73, "so the rover can still report why it stopped.", size=6.8, color=MUTE)
    # ---- ground
    gy = 328
    p.wire((12, gy), (566, gy), color=BLACK, w=2)
    p.gnd(30, gy, stub=4)
    p.text(44, gy - 6, "STAR GND: run every return to one point near the driver B− terminals", size=7.5, bold=True)
    grounds = [("BT1 −", "J1 −, 18 AWG"), ("BT2 −", "J6 −, 22 AWG"), ("U4 B−", "J3 −, 18 AWG"), ("U5 B−", "J4 −, 18 AWG"),
               ("U7 IN−", "J5 −, 18 AWG"), ("U6 IN−", "J7 −, 22 AWG"), ("FAN1 −", "22 AWG"), ("Logic board", "22 AWG, one wire")]
    for i, (dest, note) in enumerate(grounds):
        x = 22 + i * 68
        p.wire((x + 26, gy), (x + 26, gy + 10), color=BLACK)
        p.text(x + 26, gy + 20, dest, size=6.8, bold=True, anchor="c")
        p.text(x + 26, gy + 29, note, size=6.4, color=MUTE, anchor="c")
    # ---- fuses and connectors
    table = [["Fuse", "Protects", "Rating", "Wire", "Why this size"],
             ["F1", "BT1 motor pack", "10 A slow", "18 AWG", "provisional: 4 motors at ≤2.5 A stall each. Resize after T17"],
             ["F2", "BT2 logic pack", "3 A slow", "22 AWG", "about 1 A at 11.1 V for the logic, plus the fan"]]
    end = p.grid(12, 372, [30, 80, 50, 46, 300], table)
    p.note(528, 372, W - 540, ["F1 assumes ≤2.5 A stall per 25GA motor:",
                               "UNKNOWN until measured (T17).",
                               "SW1 must be DC-rated at ≥ F1. An AC",
                               "rating alone does not count.",
                               "Never power the servo from a board's",
                               "5 V pin or from U6.",
                               "Charge only with a 3S LiPo balance",
                               "charger, in the LiPo bag."], title="Before you buy", size=7.3, lead=10)
    conn = [["Connector", "Between", "Pin +", "Pin −"],
            ["J1 XT30", "BT1 and F1", "BT1 +", "GND"], ["J2 XT30", "SW1 and the bus splice", "MOTOR BUS", "GND"],
            ["J3, J4 XT30", "bus and U4, U5", "MOTOR BUS", "GND"], ["J5 XT30", "bus and U7", "MOTOR BUS", "GND"],
            ["J6 XT30", "BT2 and F2", "BT2 +", "GND"], ["J7 XT30", "SW2 and U6", "BT2 after SW2", "GND"]]
    end2 = p.grid(12, end + 10, [80, 130, 86, 40], conn, lead=11.5)
    p.lines(12, end2 + 12, ["If a pack has an XT60 plug, use XT60 for J1 or J6. A red JST-RCY plug does not meet rule 3.2.1.",
                            "The bus splice is soldered 18 AWG with heat shrink: no motor current on the vero board."], size=7.2, lead=9.6, color=MUTE)


# ---------------------------------------------------------------- sheet 3, E-stop
@sheet(3, "Hardware E-stop: switch in the motor line, sensed by the S3",
       "Design. Not built. Rule 1.2.1 NOT met (switch, not mushroom head). No-restart is firmware, not hardware.", "A4")
def sheet_estop(p, W, H):
    y = 56
    p.tag(80, y, "BT1 + after F1", side="l", color=RED, bold=True)
    p.wire((80, y), (172, y), w=2)
    p.no_contact(190, y, half=18)
    p.text(190, y - 18, "SW1 E-STOP", size=8, bold=True, anchor="c", color=RED)
    p.text(190, y + 16, "2-pin ON/OFF, ON = run", size=6.8, color=MUTE, anchor="c")
    p.wire((208, y), (420, y), w=2)
    p.xt(262, y, "J2 XT30")
    p.dot(340, y, color=RED)
    p.text(336, y - 6, "MOTOR BUS", size=7.5, color=RED, bold=True, anchor="r")
    p.tag(420, y, "J3, J4, J5: U4, U5, U7 (Sheets 2, 4)", color=RED, bold=True)
    p.wire((340, y), (340, y + 70), w=1.3)
    # bleed
    p.dot(340, y + 28, color=RED)
    p.wire((340, y + 28), (300, y + 28), (300, y + 38))
    p.res(300, y + 48, "R25 1 kΩ 0.5 W", vert=True, side="l")
    p.text(294, y + 60, "bleed: bus empties in about 1 s", size=6.4, color=MUTE, anchor="r")
    p.wire((300, y + 58), (300, y + 64), color=BLACK)
    p.gnd(300, y + 64, stub=1)
    # divider to GPIO2
    p.wire((340, y + 70), (370, y + 70))
    p.res(380, y + 70, "R23 100 kΩ")
    p.wire((390, y + 70), (500, y + 70), color=TEAL)
    p.dot(430, y + 70, color=TEAL)
    p.dot(462, y + 70, color=TEAL)
    p.tag(500, y + 70, "U1 GPIO2", color=TEAL, bold=True)
    p.wire((430, y + 70), (430, y + 80), color=TEAL)
    p.res(430, y + 90, "R24 22 kΩ", vert=True, side="l")
    p.wire((430, y + 100), (430, y + 106), color=BLACK)
    p.gnd(430, y + 106, stub=1)
    p.wire((462, y + 70), (462, y + 87.8), color=TEAL)
    p.cap(462, y + 90, "C22 100 nF")
    p.wire((462, y + 92.2), (462, y + 106), color=BLACK)
    p.gnd(462, y + 106, stub=1)
    p.lines(572, y + 62, ["12.6 V × 22 / 122 = 2.27 V at GPIO2.", "The S3 reads it every 10 ms. Below 6 V:",
                          "E-stop OFF. ESTOP latched, EN LOW,", "RESET refused until the bus is back."], size=7.2, lead=9.6)
    # behaviour
    rows = [["Action", "What happens", "Motor bus"],
            ["Power on: SW2 ON, SW1 OFF", "Logic runs. The S3 boots into ESTOP: no motor-bus voltage.", "0 V"],
            ["Switch SW1 ON", "Bus comes up. The S3 stays in ESTOP until RESET E-STOP, then a mode is chosen.", "9.9–12.6 V"],
            ["Switch SW1 OFF, driving or not", "Motors and servo lose power in hardware at once. The S3 sees < 6 V within 10 ms, latches ESTOP, sets EN LOW.", "0 V in ~1 s"],
            ["Switch SW1 back ON", "Nothing moves. The S3 holds ESTOP and EN LOW. RESET is refused until the bus is back.", "9.9–12.6 V"],
            ["S3 crash or reboot", "R5, R6 pull EN LOW: both drivers off. The S3 restarts disabled, in ESTOP if SW1 is OFF.", "no change"],
            ["Wi-Fi lost, dashboard closed", "The teleop timeout stops the motors within 0.3 s. SW1 still cuts power.", "no change"]]
    end = p.grid(12, 196, [150, 400, 56], rows, lead=13)
    rules_end = p.note(630, 196, W - 642, ["Met, 1.2.2: SW1 cuts motors and",
                                           "servo directly, no software.", "",
                                           "NOT MET, 1.2.1: a switch, not the",
                                           "red mushroom-head button.", "",
                                           "Partly, 1.2.4: no restart is done by",
                                           "the S3 and R5, R6, not hardware.", "",
                                           "Get the organisers' OK in writing.",
                                           "Label SW1 E-STOP, mount it outside."],
                       title="Rules", size=7.2, lead=9.6)
    review_end = p.note(12, max(end, rules_end) + 8, W - 24, [
        "SW1 carries all motor current. Its DC rating must be at least F1 (10 A at 12.6 V); an AC rating does not carry over. The KCD11 mini rocker from the earlier notes",
        "(commonly 3 A 250 V AC / 6 A 125 V AC, no DC rating) is too small here; it is fine for SW2. If the inspector wants the restart blocked in hardware, go back to the",
        "contactor and ARM button design (Rev C, Sheet 3). Remove the GPIO2 bench jumper before fitting R23/R24: left in, it hides the E-stop from the S3.",
        "Fail-safe: a broken R23 or GPIO2 wire reads 0 V, which the S3 treats as E-stop OFF. An open R24 still reads 0 V while the bus is off. Run T9–T11 with the wheels spinning."],
        title="Design review: check before inspection (not tested, nothing built yet)", size=7.2, lead=9.6)
    terminals = [["Terminal", "Connects to", "Terminal", "Connects to"],
                 ["SW1 terminal 1", "F1 out, BT1 +", "R23, top", "MOTOR BUS, at the splice"],
                 ["SW1 terminal 2", "J2 +, MOTOR BUS", "R23, bottom", "P12 pin 3, then the GPIO2 node"],
                 ["R25 1 kΩ 0.5 W", "MOTOR BUS to GND, at the U7 input", "R24, C22", "GPIO2 node to GND, on the logic board"],
                 ["SW2 terminal 1", "F2 out, BT2 +", "SW2 terminal 2", "J7 +, FAN1 +, R21"]]
    p.grid(12, review_end + 8, [110, 170, 90, 190], terminals, lead=12)
    p.lines(584, review_end + 20, ["A 2-pin switch has no polarity. Check", "with a meter: closed in ON, open in OFF."],
            size=7.2, lead=10, color=MUTE)


# ---------------------------------------------------------------- sheet 4, drive
@sheet(4, "Motor drivers and motors",
       "Design. Not built. BTS7960 drivers, 25GA 300 RPM 12 V motors (team list). Motor stall current to be measured (T17).", "A4")
def sheet_drive(p, W, H):
    for i, (ref, side, rpwm, lpwm, r1, r2, r3, fb, jb, m1, m2) in enumerate((
            ("U4", "LEFT", "GPIO4", "GPIO5", "R1", "R2", "R5", "F5", "J3", "M1", "M2"),
            ("U5", "RIGHT", "GPIO6", "GPIO7", "R3", "R4", "R6", "F6", "J4", "M3", "M4"))):
        x, y = 8 + i * 396, 22
        p.text(x + 6, y - 4, f"{side} SIDE", size=9, bold=True)
        p.part(x + 104, y + 6, 100, 176, ref, "BTS7960 (IBT-2)", [], size=7.5)
        for pin, py, dest, col in (("VCC", 32, "U1 3V3", RED), ("GND", 50, "GND", BLACK)):
            p.tag(x + 56, y + py, dest, side="l", color=col, bold=True)
            p.wire((x + 56, y + py), (x + 104, y + py), color=col, w=1.3)
            p.text(x + 108, y + py + 3, pin, size=8, mono=True, bold=True)
        for pin, py, dest, rr, col in (("RPWM", 72, f"U1 {rpwm}", r1, YEL), ("LPWM", 112, f"U1 {lpwm}", r2, BLUE),
                                       ("R_EN", 152, "U1 GPIO15", r3, BROWN)):
            p.tag(x + 56, y + py, dest, side="l", color=col, bold=True)
            p.wire((x + 56, y + py), (x + 104, y + py), color=col, w=1.3)
            p.dot(x + 82, y + py, color=col)
            p.wire((x + 82, y + py), (x + 82, y + py + 8), color=col)
            p.res(x + 82, y + py + 18, "", vert=True)
            p.text(x + 76, y + py + 17, rr, size=7, bold=True, anchor="r")
            p.text(x + 76, y + py + 25, "10 kΩ", size=6.6, anchor="r")
            p.gnd(x + 82, y + py + 28, stub=1)
            p.text(x + 108, y + py + 3, pin, size=8, mono=True, bold=True)
        # L_EN is joined to R_EN at the module.
        p.wire((x + 104, y + 164), (x + 96, y + 164), (x + 96, y + 152), color=BROWN, w=1.3)
        p.dot(x + 96, y + 152, color=BROWN)
        p.text(x + 108, y + 167, "L_EN", size=8, mono=True, bold=True)
        p.text(x + 108, y + 178, "R_IS, L_IS  n.c.", size=7, mono=True, color=MUTE)
        p.text(x + 200, y + 34, "B+", size=8, mono=True, bold=True, anchor="r")
        p.text(x + 200, y + 56, "B−", size=8, mono=True, bold=True, anchor="r")
        p.text(x + 200, y + 106, "M+", size=8, mono=True, bold=True, anchor="r")
        p.text(x + 200, y + 171, "M−", size=8, mono=True, bold=True, anchor="r")
        p.wire((x + 204, y + 31), (x + 222, y + 31), w=1.6)
        p.tag(x + 222, y + 31, f"{jb} +: MOTOR BUS", color=RED, size=7)
        p.wire((x + 204, y + 53), (x + 222, y + 53), color=BLACK, w=1.6)
        p.tag(x + 222, y + 53, f"{jb} −: STAR GND", color=BLACK, size=7)
        p.text(x + 214, y + 99, "18 AWG", size=6.4, color=MUTE)
        motor_pair(p, x + 204, y + 103, y + 168, x + 252, x + 330, y + 98, y + 140, m1, m2, 11)
    y = 222
    p.text(12, y, "Setup and pairing", size=9, bold=True)
    p.lines(12, y + 13, [
        "• VCC comes from the S3's 3V3 pin, not 5 V. The inputs go through a 74HC244 buffer powered from VCC; at 5 V it needs about 3.5 V for a HIGH.",
        "• Before wiring, meter VCC to B+ on each module: there must be no connection. Leave R_IS and L_IS unconnected; the firmware does not read them.",
        "• Forward = PWM on RPWM with LPWM LOW. Reverse = PWM on LPWM with RPWM LOW. 20 kHz at 10 bits; the BTS7960 is specified up to 25 kHz.",
        "• EN is HIGH only while a side is driven. EN LOW turns all four half-bridges off, so the motors coast. E-stop, disarm and faults set EN LOW at once.",
        "• The 10 kΩ pull-downs keep every input LOW while the S3 boots or resets. Solder them at the logic board: R5 on U4's EN wire, R6 on U5's.",
        "• Check each motor alone on a bench supply first, then wire both motors on a side to spin the same way for the same polarity.",
        "• If a whole side runs backwards, swap that side's M+ and M−, or set MOTOR_LEFT_INVERT or MOTOR_RIGHT_INVERT in config_hardware.h.",
        "• M+ and M− swap polarity, so they are neither fixed + nor GND. Use a pair that is not red or black, such as green and blue, and label them.",
        "• Route motor wires away from sonar, servo and camera-link wires (rule 3.1.5). Run motor returns to the star point, not through the logic board."],
        size=7.6, lead=11.4)
    tb = [["IBT-2 pin", "Left module U4", "Right module U5"],
          ["RPWM (forward)", "U1 GPIO4, R1 to GND", "U1 GPIO6, R3 to GND"],
          ["LPWM (reverse)", "U1 GPIO5, R2 to GND", "U1 GPIO7, R4 to GND"],
          ["R_EN + L_EN, joined", "U1 GPIO15, R5 to GND", "U1 GPIO15, R6 to GND"],
          ["R_IS, L_IS", "not connected", "not connected"],
          ["VCC (logic buffer)", "U1 3V3", "U1 3V3"], ["GND (logic)", "GND", "GND"],
          ["B+, B−", "J3 (motor bus)", "J4 (motor bus)"], ["M+, M−", "M1, M2 in parallel", "M3, M4 in parallel"]]
    p.grid(12, 346, [120, 142, 142], tb)
    p.note(428, 346, W - 440, [
        "Team decision, 11 Nov 2026: two BTS7960 (IBT-2) modules.",
        "The firmware drives only this module (src/s3/motors.cpp).",
        "",
        "Heatsink: at least one seller says it is not insulated from",
        "the board. Treat it as live: it must touch no metal and no",
        "other module, or fit an insulating thermal pad.",
        "",
        "Motors: 25GA 300 RPM, 12 V. Stall current UNKNOWN: measure",
        "one on a bench supply (T17) before fixing F1 (Sheet 2)."], title="Driver", size=7.3, lead=9.6)


# ---------------------------------------------------------------- sheet 5, sonars
@sheet(5, "Sonars: HC-SR04", "US1 tested on 3.3 V only, without a divider. The 5 V circuit below is not tested yet.", "A4")
def sheet_sonar(p, W, H):
    x, y = 30, 30
    p.part(x, y, 96, 136, "US1", "HC-SR04", ["front centre"], size=7.5)
    for pin, py in (("VCC", 44), ("Trig", 72), ("Echo", 100), ("GND", 126)):
        p.text(x + 92, y + py + 3, pin, size=8, mono=True, bold=True, anchor="r")
    p.wire((x + 96, y + 44), (x + 290, y + 44), w=1.3)
    p.tag(x + 290, y + 44, "+5V LOGIC", color=RED, bold=True)
    p.text(x + 262, y + 40, "5.0 V", size=7, color=RED, bold=True, anchor="c")
    p.wire((x + 96, y + 72), (x + 290, y + 72), color=GREEN, w=1.3)
    p.tag(x + 290, y + 72, "U1 GPIO10", color=GREEN, bold=True)
    p.text(x + 200, y + 68, "3.3 V from the S3", size=6.8, color=MUTE, anchor="c")
    p.wire((x + 96, y + 100), (x + 170, y + 100), color=PURPLE, w=1.3)
    p.text(x + 118, y + 96, "5 V", size=6.8, color=PURPLE)
    p.res(x + 182, y + 100, "R11 1 kΩ", color=PURPLE)
    p.wire((x + 192, y + 100), (x + 290, y + 100), color=PURPLE, w=1.3)
    p.dot(x + 232, y + 100, color=PURPLE)
    p.text(x + 262, y + 96, "3.2 V", size=7, color=PURPLE, bold=True, anchor="c")
    p.tag(x + 290, y + 100, "U1 GPIO11", color=PURPLE, bold=True)
    p.wire((x + 232, y + 100), (x + 232, y + 114), color=PURPLE)
    p.res(x + 232, y + 124, "R12 1.8 kΩ", vert=True, color=PURPLE)
    p.wire((x + 232, y + 134), (x + 232, y + 148), color=BLACK)
    p.gnd(x + 232, y + 148, stub=1)
    p.wire((x + 96, y + 126), (x + 112, y + 126), color=BLACK, w=1.3)
    p.gnd(x + 112, y + 148, stub=1)
    p.wire((x + 112, y + 126), (x + 112, y + 148), color=BLACK, w=1.3)
    # decoupling
    p.wire((x + 136, y + 44), (x + 136, y + 118), color=RED)
    p.dot(x + 136, y + 44, color=RED)
    p.cap(x + 136, y + 121, "", polar=True)
    p.wire((x + 136, y + 124), (x + 136, y + 148), color=BLACK)
    p.gnd(x + 136, y + 148, stub=1)
    p.wire((x + 160, y + 44), (x + 160, y + 118), color=RED)
    p.dot(x + 160, y + 44, color=RED)
    p.cap(x + 160, y + 121, "")
    p.wire((x + 160, y + 124), (x + 160, y + 148), color=BLACK)
    p.gnd(x + 160, y + 148, stub=1)
    p.text(x + 124, y + 170, "C11 100 µF", size=6.8, anchor="c")
    p.text(x + 170, y + 170, "C15 100 nF", size=6.8, anchor="c")
    p.text(x + 148, y + 180, "at the sensor, as close as you can", size=6.4, color=MUTE, anchor="c")
    # table
    tb = [["Sonar", "Position", "Trig", "Echo", "Divider", "Caps"],
          ["US1 front_center", "forward, centred, level", "GPIO10", "GPIO11", "R11, R12", "C11, C15"],
          ["US2 front_left", "forward-left, about 30° out", "GPIO12", "GPIO13", "R13, R14", "C12, C16"],
          ["US3 front_right", "forward-right, about 30° out", "GPIO14", "GPIO16", "R15, R16", "C13, C17"],
          ["US4 rear", "backward. Enables reverse", "GPIO17", "GPIO18", "R17, R18", "C14, C18"]]
    p.grid(390, 30, [76, 110, 40, 40, 44, 44], tb, size=6.7, lead=13)
    p.lines(392, 108, ["Owned: US1. To buy: US2, US3, US4. Fit US1, then US4, then US2 and US3. Enable each in SONAR_FITTED.",
                       "Reverse stays blocked until the rear sonar is fitted: that is on purpose.",
                       "Sensors fire one at a time, in 40 ms slots, so they do not hear each other.",
                       "Measure each sensor face to the bumper and enter it in SONAR_FACE_TO_BUMPER_MM."], size=7.2, lead=10.2)
    p.note(392, 156, W - 404, ["R12 may be 2 kΩ if that is what you have: 3.33 V, still safe.",
                               "NEVER connect the 5 V Echo straight to an S3 pin."], fill=WARN, size=7.6, lead=11)
    # results
    y = 232
    rows = [["Bench result (2 Nov 2026): US1 on 3.3 V, no divider, no capacitors", "Reading", "Meaning"],
            ["Ruler 100 mm, object propped", "settled at 125 mm", "25 mm long: outside the ±20 mm window"],
            ["Ruler 200 mm, object propped", "198 mm, then 181–191 mm", "object seems to have moved in the test"],
            ["Unmeasured close object, propped still", "134 of 134 valid, 162–179 mm", "steady at close range, no strays"],
            ["Wall about 1.8 m away, nothing moving", "29–45 % single stray readings", "gets worse with more Wi-Fi traffic"],
            ["Echo wire loose", "no_echo 31 of 31", "never a fake number"]]
    end = p.grid(12, y, [300, 150, 200], rows, lead=13)
    p.note(12, end + 10, W - 24, [
        "An HC-SR04 is a 5 V part, so 3.3 V is out of spec. These numbers are a warning, not a calibration. Do not calibrate on 3.3 V.",
        "The capacitors are a proposal: Wi-Fi noise on the supply is the suspected cause of the strays, and it is not proven. Repeat the ruler test at 10, 20 and 50 cm after you build",
        "this circuit, using test T15 in Section 9, then enter corrections in SONAR_SCALE and SONAR_FACE_TO_BUMPER_MM. The proximity task must stop within 100 ± 20 mm."],
        title="What to do with this", fill=PANEL, size=7.4, lead=10.2)


# ---------------------------------------------------------------- sheet 6, camera and servo
@sheet(6, "ESP32-CAM, camera link and SG90 pan servo", "Link tested on the bench. Servo signal tested with the earlier MG996R; the SG90 and the final power path are not tested.", "A4")
def sheet_camera(p, W, H):
    ux, uy, st = 160, 46, 28
    p.rect(ux, uy - 18, 110, st * 8 + 6, fill=PANEL)
    p.text(ux + 55, uy - 24, "U2 ESP32-CAM (AI-Thinker layout)", size=8, bold=True, anchor="c")
    left = [("5V", RED), ("GND", BLACK), ("IO12", None), ("IO13", TEAL), ("IO15", None), ("IO14", TEAL), ("IO2", ORANGE), ("IO4", None)]
    right = ["3V3", "IO16", "IO0", "GND", "VCC", "U0R", "U0T", "GND"]
    right_note = ["not used", "PSRAM CS: do not use", "GND at reset = flash mode", "", "not used", "IO3, programming", "IO1, programming", ""]
    for i, (pin, col) in enumerate(left):
        y = uy + i * st
        p.text(ux + 5, y + 3, pin, size=8, mono=True, bold=bool(col), color=INK if col else MUTE)
        if col and col != ORANGE:
            p.wire((ux, y), (ux - 30, y), color=col, w=1.3)
    for i, pin in enumerate(right):
        y = uy + i * st
        p.text(ux + 105, y + 3, pin, size=8, mono=True, color=MUTE, anchor="r")
        p.text(ux + 115, y + 3, right_note[i], size=6.8, color=MUTE)
    p.tag(130, uy, "+5V LOGIC", side="l", color=RED, bold=True)
    p.tag(130, uy + st, "GND", side="l", color=BLACK, bold=True)
    p.tag(130, uy + 3 * st, "U1 GPIO41 (S3 TX)", side="l", color=TEAL, bold=True)
    p.tag(130, uy + 5 * st, "U1 GPIO42 (S3 RX)", side="l", color=TEAL, bold=True)
    p.text(ux - 5, uy + 2 * st + 3, "strapping: avoid", size=6.8, color=MUTE, anchor="r")
    p.text(ux - 5, uy + 4 * st + 3, "pulses at boot: avoid", size=6.8, color=MUTE, anchor="r")
    p.text(ux - 5, uy + 7 * st + 3, "flash LED, kept OFF", size=6.8, color=MUTE, anchor="r")
    # capacitors at the 5 V pin
    for cx_ in (138, 150):
        p.dot(cx_, uy, color=RED)
        p.dot(cx_, uy + st, color=BLACK)
        p.wire((cx_, uy), (cx_, uy + st / 2 - 2.2), color=RED)
        p.wire((cx_, uy + st / 2 + 2.2), (cx_, uy + st), color=BLACK)
        p.cap(cx_, uy + st / 2)
    p.text(130, uy + st / 2 + 2.5, "C1 470 µF, C2 100 nF", size=6.8, bold=True, anchor="r")
    # IO2 to servo
    y2 = uy + 6 * st
    p.wire((ux, y2), (70, y2), (70, uy + 8 * st + 18), (430, uy + 8 * st + 18), (430, 96), (470, 96), color=ORANGE, w=1.3)
    p.text(ux + 90, uy + 8 * st + 14, "servo signal, 50 Hz, 3.3 V", size=7, color=ORANGE, anchor="c")
    sx, sy = 470, 66
    p.part(sx, sy, 110, 92, "SV1", "SG90", [], size=7.5)
    p.text(sx + 6, sy + 33, "orange: signal", size=8, color=ORANGE)
    p.text(sx + 6, sy + 51, "red: V+", size=8, color=RED)
    p.text(sx + 6, sy + 69, "brown: GND", size=8)
    p.wire((sx + 110, sy + 48), (sx + 190, sy + 48), w=1.4)
    p.dot(sx + 138, sy + 48, color=RED)
    p.tag(sx + 190, sy + 48, "+5V SERVO, U7", color=RED, bold=True)
    p.wire((sx + 110, sy + 66), (sx + 138, sy + 66), color=BLACK, w=1.4)
    p.wire((sx + 138, sy + 48), (sx + 138, sy + 53), color=RED)
    p.cap(sx + 138, sy + 56, "", polar=True)
    p.wire((sx + 138, sy + 58), (sx + 138, sy + 66), color=BLACK)
    p.text(sx + 150, sy + 62, "C3 470 µF", size=7, bold=True)
    p.wire((sx + 138, sy + 66), (sx + 138, sy + 82), color=BLACK, w=1.4)
    p.dot(sx + 138, sy + 66, color=BLACK)
    p.gnd(sx + 138, sy + 82, stub=1)
    p.text(sx + 150, sy + 86, "same GND as U2", size=7)
    p.note(470, 190, W - 482, ["• U7 sits on the motor bus, so the E-stop also stops the servo.",
                               "• U2 stays on from U6 and holds the pan during E-stop.",
                               "• Starts at 90°, keeps 45–135°, slews at 120°/s.",
                               "• Must be the 180° positional type: a 360° SG90 cannot hold.",
                               "• If it jitters on a 3.3 V signal, add a 74AHCT125 buffer on 5 V.",
                               "• Check the camera ribbon cannot snag at either end of travel.",
                               "• SG90 is light (~1.8 kg·cm): balance the camera on the horn."], title="Servo", fill=PANEL, size=7.4, lead=10.6)
    y = 318
    tb = [["Link wire", "From", "To", "Colour", "Signal"],
          ["W60", "U1 GPIO41", "U2 IO13", "teal (not red or black)", "115200 8N1, 3.3 V both sides"],
          ["W61", "U2 IO14", "U1 GPIO42", "white (not red or black)", "115200 8N1, 3.3 V both sides"],
          ["W28", "U1 GND", "U2 GND", "black", "the link needs this ground"]]
    end = p.grid(12, y, [56, 72, 72, 150, 146], tb)
    p.note(512, y, W - 524, ["• Leave the microSD slot EMPTY: it shares IO13 and IO14.",
                             "• The MB board is only for flashing over USB. Press IO0 + RST",
                             "   to flash, RST alone to run. It does not ride on the rover.",
                             "• After the first flash, updates go over Wi-Fi.",
                             "• Short, thick 5 V wires: brownouts are common on this board."],
           title="ESP32-CAM", fill=PANEL, size=7.3, lead=10)
    p.text(12, end + 16, "Bench result: link steady, 138 of 138 samples ok, 0 bad messages in 30 s. Servo sweep 45–135° smooth with the earlier MG996R (team observation).", size=7.4)
    p.text(12, end + 28, "Camera was on the MB or fed from the DevKit VIN for these tests, not from U6. Repeat them once U6 is built.", size=7.4, color=MUTE)


# ---------------------------------------------------------------- sheet 7, sense and audio
@sheet(7, "Voltage sensing and optional audio board", "Dividers not fitted; the motor-bus divider is required for the E-stop. Audio board runs; mic and speaker not wired.", "A4")
def sheet_sense(p, W, H):
    p.text(12, 18, "VOLTAGE SENSE (motor bus required, logic pack optional)", size=9, bold=True)
    for i, (src, r1, r2, c1, dest, where) in enumerate((("BT2 after SW2", "R21", "R22", "C21", "U1 GPIO1", "logic pack, optional"),
                                                        ("MOTOR BUS", "R23", "R24", "C22", "U1 GPIO2", "after SW1: E-stop sense"))):
        x, y = 90 + i * 300, 56
        p.tag(x, y, src, side="l", color=RED, bold=True)
        p.wire((x, y), (x + 24, y), w=1.2)
        p.res(x + 36, y, f"{r1} 100 kΩ")
        p.wire((x + 46, y), (x + 120, y), color=TEAL, w=1.2)
        p.dot(x + 72, y, color=TEAL)
        p.dot(x + 100, y, color=TEAL)
        p.tag(x + 120, y, dest, color=TEAL, bold=True)
        p.wire((x + 72, y), (x + 72, y + 16), color=TEAL)
        p.res(x + 72, y + 26, f"{r2} 22 kΩ", vert=True, side="l")
        p.wire((x + 72, y + 36), (x + 72, y + 50), color=BLACK)
        p.gnd(x + 72, y + 50, stub=1)
        p.wire((x + 100, y), (x + 100, y + 23), color=TEAL)
        p.cap(x + 100, y + 26, f"{c1} 100 nF")
        p.wire((x + 100, y + 28), (x + 100, y + 50), color=BLACK)
        p.gnd(x + 100, y + 50, stub=1)
        p.text(x - 60, y + 22, where, size=6.8, color=MUTE)
    p.lines(12, 128, ["12.6 V × 22 / 122 = 2.27 V at the ADC pin. R23/R24 are required: they are how the S3 sees the E-stop (ESTOP_FROM_MOTOR_BUS). Put R23 at the bus splice",
                      "and R24, C22 on the logic board. R21/R22 are optional: set BATTERY_SENSE_FITTED after calibrating with BATTERY_CAL_GAIN. While the bus is measured the",
                      "firmware caps PWM duty at 12.0 V / bus voltage; otherwise at a fixed 0.95. Calibrate the bus with MOTOR_BUS_CAL_GAIN."], size=7.4, lead=10.4)
    y = 176
    p.rect(0, y, W, 1, fill=RULE, stroke=None, r=0)
    p.text(12, y + 18, "AUDIO BOARD (optional, two-way audio task, teleop only)", size=9, bold=True)
    bx, by, st = 340, y + 44, 20
    p.rect(bx, by - 14, 110, st * 9 + 10, fill=PANEL)
    p.text(bx + 55, by - 19, "U3 ESP32 DevKit", size=8, bold=True, anchor="c")
    left_pins = [("3V3", RED), ("GND", BLACK), ("GPIO26", TEAL), ("GPIO25", TEAL), ("GPIO34", TEAL)]
    right_pins = [("5V", RED), ("GND", BLACK), ("GPIO27", TEAL), ("GPIO32", TEAL), ("GPIO33", TEAL), ("GPIO23", TEAL)]
    mic = ["VDD", "GND, L/R", "SCK", "WS", "SD"]
    amp = ["VIN", "GND", "BCLK", "LRC", "DIN", "SD (HIGH = on)"]
    p.part(60, by - 6, 170, st * 5 + 12, "U8", "INMP441 mic (name to verify)", [], size=7.2)
    p.part(560, by - 6, 170, st * 6 + 12, "U9", "MAX98357A I²S amplifier", [], size=7.2)
    for i, (pin, col) in enumerate(left_pins):
        yy = by + 14 + i * st
        p.text(bx + 5, yy + 3, pin, size=7.6, mono=True, bold=True)
        p.wire((bx, yy), (230, yy), color=col, w=1.2)
        p.text(226, yy + 3, mic[i], size=7.6, mono=True, anchor="r")
    for i, (pin, col) in enumerate(right_pins):
        yy = by + 14 + i * st
        p.text(bx + 105, yy + 3, pin, size=7.6, mono=True, bold=True, anchor="r")
        p.wire((bx + 110, yy), (560, yy), color=col, w=1.2)
        p.text(564, yy + 3, amp[i], size=7.6, mono=True)
    p.tag(490, by + 14 - 7, "+5V LOGIC", color=RED, size=6.6)
    p.lines(60, by + st * 5 + 20, ["L/R tied to GND selects the left channel.", "3.3 V supply only."], size=7, lead=9.4, color=MUTE)
    p.wire((730, by + 30), (748, by + 30), color=INK)
    p.wire((730, by + 60), (748, by + 60), color=INK)
    p.part(748, by + 18, 48, 54, "LS1", "", ["4 Ω", "3 W"], size=7.2)
    p.text(726, by + 33, "+", size=7.6, mono=True, anchor="r")
    p.text(726, by + 63, "−", size=7.6, mono=True, anchor="r")
    p.lines(560, by + st * 6 + 22, ["GAIN pin open = 9 dB. Up to 0.7 A peak at full volume."], size=7, lead=9.4, color=MUTE)
    p.note(12, y + 248, W - 24, ["U3 stays on its own board so audio work can never delay the motor control loop. It has no wire to the S3 except power and ground, and it talks over Wi-Fi only.",
                                 "U3 and these two parts are provisional until bring-up stage 9. U3 itself runs and reports to the S3; the microphone and speaker have not been wired."],
           fill=PANEL, size=7.4, lead=10.4)


# ---------------------------------------------------------------- sheet 8, bench
@sheet(8, "Bench wiring in use today (not the rover)", "Tested on the desk. Shortcuts here are NOT allowed on the rover.", "A4")
def sheet_bench(p, W, H):
    p.text(12, 18, "This is what is on the desk now, so you can see what has really run. Do not copy the shortcuts into the rover build.", size=8, color=MUTE)
    p.part(300, 40, 130, 190, "U1", "ESP32-S3", ["USB to PC (CH343)"], size=7.6)
    for name, y in (("GPIO41", 80), ("GPIO42", 104)):
        p.text(304, y + 3, name, size=7.6, mono=True, bold=True)
    for name, y in (("GPIO2", 125), ("GPIO10", 152), ("GPIO11", 176), ("GND", 212)):
        p.text(426, y + 3, name, size=7.6, mono=True, bold=True, anchor="r")
    p.text(304, 215, "GND", size=7.6, mono=True, bold=True)
    p.part(40, 60, 130, 118, "U2", "ESP32-CAM", ["on MB (USB power), or 5V from", "DevKit VIN and GND"], size=7)
    p.text(164, 103, "IO13", size=7.6, mono=True, bold=True, anchor="r")
    p.text(164, 127, "IO14", size=7.6, mono=True, bold=True, anchor="r")
    p.text(164, 151, "IO2", size=7.6, mono=True, bold=True, anchor="r")
    p.text(164, 171, "GND", size=7.6, mono=True, bold=True, anchor="r")
    p.wire((170, 100), (230, 100), (230, 80), (300, 80), color=TEAL, w=1.3)
    p.wire((170, 124), (250, 124), (250, 104), (300, 104), color=TEAL, w=1.3)
    p.text(235, 76, "S3 TX", size=6.8, color=TEAL)
    p.text(254, 118, "camera TX", size=6.8, color=TEAL)
    p.wire((170, 168), (270, 168), (270, 212), (300, 212), color=BLACK, w=1.3)
    p.text(274, 196, "GND", size=6.8)
    p.part(40, 214, 130, 72, "SV1", "servo", ["MG996R tested; SG90 next", "orange: IO2. Red, brown: own", "5 V supply, − to U2 GND."], size=7)
    p.wire((170, 148), (190, 148), (190, 200), (20, 200), (20, 232), (40, 232), color=ORANGE, w=1.3)
    # jumper
    p.wire((430, 125), (486, 125), color=ORANGE, w=1.3)
    p.tag(486, 125, "U1 3V3", color=RED, size=6.8)
    p.text(434, 121, "jumper", size=7, color=ORANGE, bold=True)
    p.lines(442, 52, ["GPIO2 to 3V3 stands in for 'motor bus present'.",
                      "Use it only while nothing that moves is connected."], size=7, lead=9.4, color=MUTE)
    # sonar
    p.part(640, 60, 110, 112, "US1", "HC-SR04", ["front centre"], size=7.4)
    for name, y in (("VCC", 104), ("Trig", 124), ("Echo", 144), ("GND", 164)):
        p.text(644, y + 3, name, size=7.6, mono=True, bold=True)
    p.wire((640, 124), (590, 124), (590, 152), (430, 152), color=GREEN, w=1.3)
    p.wire((640, 144), (604, 144), (604, 176), (430, 176), color=PURPLE, w=1.3)
    p.wire((640, 104), (570, 104), color=RED, w=1.3)
    p.tag(570, 104, "U1 3V3 pin", side="l", color=RED)
    p.wire((640, 164), (620, 164), (620, 212), (430, 212), color=BLACK, w=1.3)
    p.text(505, 208, "GND", size=6.8)
    # wires for pins into the S3 right edge: labels sit on the board side
    rows = [["Bench shortcut", "Why it is only for the desk", "On the rover"],
            ["Sonar VCC on 3.3 V, Echo direct", "HC-SR04 is a 5 V part; readings are 25 mm long at 100 mm", "5 V + R11/R12 divider + caps (Sheet 5)"],
            ["GPIO2 jumper to 3V3", "Tells the S3 'motor bus present' with no real E-stop", "R23/R24 on the motor bus (Sheet 3)"],
            ["Servo on a bench 5 V supply", "No E-stop on that supply", "U7 on the motor bus (Sheets 2, 6)"],
            ["Camera on MB or DevKit VIN", "USB or board-fed power, no capacitors", "U6 + C1, C2 (Sheet 6)"],
            ["Jumper wires, no solder", "Rules 3.1.1 to 3.1.3 need soldered boards and crimped connectors", "Veroboard or PCB, female headers"]]
    end = p.grid(12, 300, [170, 320, 200], rows, lead=13)
    p.note(12, end + 10, W - 24, ["The S3 5V pin did not power the camera on the bench, so treat it as an input only. Its use as the +5V LOGIC input is not tested. Unplug USB the first time you power it from U6.",
                                  "Sonar on 5 V without a divider would feed 5 V into GPIO11 and can damage the S3. If you have no resistors yet, stay on 3.3 V and treat every reading as rough."],
           title="Warnings", size=7.4, lead=10.2)


# ---------------------------------------------------------------- text styles and table helper
BODY = ParagraphStyle("Body", fontName="Body", fontSize=9, leading=12.4, textColor=INK, spaceAfter=5)
SMALL = ParagraphStyle("Small", parent=BODY, fontSize=8, leading=10.6, spaceAfter=3)
CELL = ParagraphStyle("Cell", parent=BODY, fontSize=7.6, leading=9.6, spaceAfter=0)
HEAD = ParagraphStyle("Head", parent=CELL, fontName="BodyB", textColor=WHITE)
H1 = ParagraphStyle("H1", fontName="BodyB", fontSize=14, leading=18, textColor=INK, spaceBefore=4, spaceAfter=6)
H2 = ParagraphStyle("H2", fontName="BodyB", fontSize=10, leading=13, textColor=INK, spaceBefore=8, spaceAfter=3)
TITLE = ParagraphStyle("Title", fontName="BodyB", fontSize=30, leading=34, textColor=INK, spaceAfter=2)
SUB = ParagraphStyle("Sub", fontName="Body", fontSize=15, leading=19, textColor=MUTE, spaceAfter=12)
BULLET = ParagraphStyle("Bullet", parent=BODY, leftIndent=12, bulletIndent=2, spaceAfter=3)

C = '<font color="#1E8449"><b>CONFIRMED</b></font>'
R = '<font color="#1F5FBF"><b>RECOMMENDED</b></font>'
U = '<font color="#B03A2E"><b>UNKNOWN</b></font>'


def para(text, style=BODY):
    return Paragraph(text, style)


def bullets(items, style=BULLET):
    return [Paragraph(i, style, bulletText="•") for i in items]


def table(rows, widths, head=True, style=CELL):
    data = [[Paragraph(str(c), HEAD if (head and r == 0) else style) for c in row] for r, row in enumerate(rows)]
    t = Table(data, colWidths=widths, repeatRows=1 if head else 0)
    cmds = [("VALIGN", (0, 0), (-1, -1), "TOP"), ("LEFTPADDING", (0, 0), (-1, -1), 4), ("RIGHTPADDING", (0, 0), (-1, -1), 4),
            ("TOPPADDING", (0, 0), (-1, -1), 2.6), ("BOTTOMPADDING", (0, 0), (-1, -1), 3),
            ("LINEBELOW", (0, 0), (-1, -1), 0.4, RULE), ("BOX", (0, 0), (-1, -1), 0.6, RULE)]
    if head:
        cmds.append(("BACKGROUND", (0, 0), (-1, 0), INK))
    t.setStyle(TableStyle(cmds))
    return t


def callout(flow, width, fill=WARN):
    t = Table([[flow]], colWidths=[width])
    t.setStyle(TableStyle([("BACKGROUND", (0, 0), (-1, -1), fill), ("BOX", (0, 0), (-1, -1), 0.6, RULE),
                           ("LEFTPADDING", (0, 0), (-1, -1), 9), ("RIGHTPADDING", (0, 0), (-1, -1), 9),
                           ("TOPPADDING", (0, 0), (-1, -1), 7), ("BOTTOMPADDING", (0, 0), (-1, -1), 6)]))
    return t


class Heading(Paragraph):
    """A section title that also goes in the contents and the PDF outline."""


# ---------------------------------------------------------------- document content
def wire_rows():
    h = ["Wire", "From", "To", "Net and voltage", "Colour", "Size", "Notes"]
    return [
        h,
        ["W01", "BT1 +", "J1 + then F1 in", "BT1 + 12.6 V", "red", "18 AWG", "F1 as close to the pack as possible"],
        ["W02", "F1 out", "SW1 terminal 1", "BT1 + 12.6 V", "red", "18 AWG", ""],
        ["W03", "SW1 terminal 2", "J2 + then bus splice", "MOTOR BUS 0 / 12.6 V", "red", "18 AWG", "Soldered splice, heat shrink"],
        ["W04", "Bus splice", "J3 +, U4 B+", "MOTOR BUS", "red", "18 AWG", "Left driver"],
        ["W05", "Bus splice", "J4 +, U5 B+", "MOTOR BUS", "red", "18 AWG", "Right driver"],
        ["W06", "Bus splice", "J5 +, U7 IN+", "MOTOR BUS", "red", "18 AWG", "Servo buck; R25 bleed across U7 IN+ / IN−"],
        ["W07", "BT2 +", "J6 + then F2 in, F2 out to SW2", "BT2 + 12.6 V", "red", "22 AWG", "F2 as close to the pack as possible"],
        ["W08", "SW2 terminal 2", "J7 +, U6 IN+; FAN1 +; R21", "BT2 after SW2", "red", "22 AWG", "Check fan current on its label"],
        ["W09", "U6 OUT+", "+5V LOGIC point", "+5V LOGIC", "red", "22 AWG", "Set U6 to 5.0 V before connecting"],
        ["W10", "+5V LOGIC", "U1 5V pin", "+5V LOGIC", "red", "22 AWG", "Unplug USB the first time"],
        ["W11", "+5V LOGIC", "U2 5V pin, C1, C2", "+5V LOGIC", "red", "22 AWG", "Short wires"],
        ["W12", "+5V LOGIC", "US1–US4 VCC, C11–C18", "+5V LOGIC", "red", "24 AWG", "One wire per sonar"],
        ["W13", "+5V LOGIC", "U3 5V pin, U9 VIN", "+5V LOGIC", "red", "22 AWG", "Optional audio"],
        ["W14", "U7 OUT+", "SV1 red, C3", "+5V SERVO", "red", "18 AWG", "Set U7 to 5.0 V before connecting"],
        ["W20", "BT1 −, BT2 −", "J1 −, J6 −, J2 −, star point", "GND", "black", "18 AWG", "Both packs share this ground"],
        ["W21", "Star point", "J3 −, U4 B−", "GND", "black", "18 AWG", "Motor current return"],
        ["W22", "Star point", "J4 −, U5 B−", "GND", "black", "18 AWG", "Motor current return"],
        ["W23", "Star point", "J5 −, U7 IN−", "GND", "black", "18 AWG", ""],
        ["W24", "Star point", "J7 −, U6 IN−", "GND", "black", "22 AWG", ""],
        ["W25", "Star point", "R25 bottom (at U7 IN−)", "GND", "black", "22 AWG", "Bleed resistor return"],
        ["W26", "Star point", "FAN1 −", "GND", "black", "22 AWG", ""],
        ["W27", "Star point", "Logic board GND", "GND", "black", "22 AWG", "No motor current through the logic board"],
        ["W28", "Logic GND", "U1, U2, US1–US4 GND", "GND", "black", "24 AWG", "Also R1–R6, R12, R14, R16, R18, R22, R24, C21, C22"],
        ["W29", "Logic GND", "U4 GND, U5 GND", "GND", "black", "24 AWG", "Driver logic ground"],
        ["W30", "U7 OUT−", "SV1 brown, C3, U2 GND", "GND", "black", "18 AWG", "The servo signal needs this common ground"],
        ["W31", "Bus splice", "R23 top (100 kΩ, at the splice)", "MOTOR BUS", "red", "24 AWG", "Short: R23 sits at the bus end"],
        ["W32", "R23 bottom", "P12 pin 3, GPIO2 node: R24, C22", "E-stop sense, ≤2.3 V", "teal", "24 AWG", "Broken = treated as E-stop OFF"],
        ["W33", "After SW2", "R21 top (100 kΩ), then P12 pin 1", "BT2 sense, ≤2.3 V", "teal", "24 AWG", "Optional"],
        ["W38", "U1 GPIO15", "U4 and U5 R_EN + L_EN", "EN 3.3 V", "brown", "24 AWG", "R5 (U4 wire), R6 (U5 wire) 10 kΩ to GND"],
        ["W39", "U1 3V3", "U4 VCC, U5 VCC", "+3V3", "red", "24 AWG", "Driver logic buffer only"],
        ["W40", "U1 GPIO4", "U4 RPWM", "PWM 3.3 V, 20 kHz, forward", "yellow", "24 AWG", "R1 10 kΩ to GND"],
        ["W41", "U1 GPIO5", "U4 LPWM", "PWM 3.3 V, 20 kHz, reverse", "blue", "24 AWG", "R2 10 kΩ to GND"],
        ["W42", "U1 GPIO6", "U5 RPWM", "PWM 3.3 V, 20 kHz, forward", "yellow", "24 AWG", "R3 10 kΩ to GND"],
        ["W43", "U1 GPIO7", "U5 LPWM", "PWM 3.3 V, 20 kHz, reverse", "blue", "24 AWG", "R4 10 kΩ to GND"],
        ["W44", "U4 M+", "M1, M2 first lead", "Motor, swaps polarity", "green", "18 AWG", "Not red or black"],
        ["W45", "U4 M−", "M1, M2 second lead", "Motor, swaps polarity", "blue", "18 AWG", "Not red or black"],
        ["W46", "U5 M+", "M3, M4 first lead", "Motor, swaps polarity", "green", "18 AWG", "Not red or black"],
        ["W47", "U5 M−", "M3, M4 second lead", "Motor, swaps polarity", "blue", "18 AWG", "Not red or black"],
        ["W50", "U1 GPIO10", "US1 Trig", "Trig 3.3 V", "green", "24 AWG", ""],
        ["W51", "US1 Echo", "R11, then U1 GPIO11", "Echo 5 V, then 3.2 V", "violet", "24 AWG", "R12 from GPIO11 side to GND"],
        ["W52", "U1 GPIO12", "US2 Trig", "Trig 3.3 V", "green", "24 AWG", ""],
        ["W53", "US2 Echo", "R13, then U1 GPIO13", "Echo 5 V, then 3.2 V", "violet", "24 AWG", "R14 from GPIO13 side to GND"],
        ["W54", "U1 GPIO14", "US3 Trig", "Trig 3.3 V", "green", "24 AWG", ""],
        ["W55", "US3 Echo", "R15, then U1 GPIO16", "Echo 5 V, then 3.2 V", "violet", "24 AWG", "R16 from GPIO16 side to GND"],
        ["W56", "U1 GPIO17", "US4 Trig", "Trig 3.3 V", "green", "24 AWG", ""],
        ["W57", "US4 Echo", "R17, then U1 GPIO18", "Echo 5 V, then 3.2 V", "violet", "24 AWG", "R18 from GPIO18 side to GND"],
        ["W60", "U1 GPIO41", "U2 IO13", "Link S3 to camera, 3.3 V", "teal", "24 AWG", "115200 8N1"],
        ["W61", "U2 IO14", "U1 GPIO42", "Link camera to S3, 3.3 V", "white", "24 AWG", "115200 8N1"],
        ["W62", "U2 IO2", "SV1 orange", "Servo signal, 50 Hz", "orange", "24 AWG", ""],
        ["W70", "P12 pin 1", "R22, C21, U1 GPIO1", "2.27 V full", "teal", "on board", "Optional"],
        ["W71", "P12 pin 3", "R24, C22, U1 GPIO2", "2.27 V full", "teal", "on board", "Required: E-stop sense"],
    ]


def bom_rows():
    return [
        ["Ref", "Part", "Qty", "Label", "Notes"],
        ["M1–M4", "25GA 300 RPM DC gear motor, 12 V, with 25GA mount, hex long shaft and 85 mm wheel", "4", f"{C} team list. Stall current {U}", "Measure stall on a bench supply (T17); it sets F1. No encoder, so traverse cannot finish as COMPLETE."],
        ["U4, U5", "BTS7960 / IBT-2 motor driver module: two BTS7960B half-bridges, 74HC244 input buffer", "2", "team decision, 11 Nov 2026", "Seller data: 5.5–27 V, 3.3–5 V logic, PWM ≤25 kHz, current limit 43 A typical. VCC from U1 3V3. Heatsink may be live."],
        ["BT1, BT2", "3S LiPo 850 mAh (11.1 V, 12.6 V full)", "2", f"{C} team list", "BT1 motors and servo, BT2 logic. No BMS: stop at 10.5 V, never below 9.9 V. Check each pack's plug and C rating."],
        ["—", "3S LiPo balance charger", "1", f"{R}, must add", "Not in the list. Never charge a LiPo from a plain power supply."],
        ["—", "Fire-resistant LiPo bag", "1", f"{R}, must add", "Rule 3.3.2: carry and charge in it."],
        ["SW1", "ON/OFF switch, 2-pin, labelled E-STOP", "1", f'{C} team list. <font color="#B03A2E"><b>Does not meet rule 1.2.1</b></font>', f"Carries all motor current: DC rating must be ≥ F1 (10 A). Rating {U}: read the print."],
        ["SW2", "ON/OFF switch, 2-pin, labelled POWER", "1", f"{C} team list", "Logic pack only, about 1 A plus the fan."],
        ["F1, F2", "Inline blade fuse holders, slow-blow fuses 10 A and 3 A, plus spares", "2", f"{R}, must add", "Rule 3.3.1. F1 provisional until T17."],
        ["J1–J7", "XT30 pairs", "7", f"{R}, must add", "Rule 3.2.1. Use XT60 for J1 or J6 if that pack has an XT60 plug."],
        ["U6, U7", "XL4015 DC-DC buck, 5 A, 8–36 V in", "2", f"{C} team list", "U6 logic 5.0 V from BT2; U7 servo 5.0 V from the motor bus."],
        ["FAN1", "12 V DC 4-inch case fan", "1", f"{C} team list. Current {U}", "On BT2 after SW2. Read its label."],
        ["U1", "ESP32-S3-N16R8 board, DevKitC-1 header (marked ESP32 23 2022 V1.3)", "1", f"{C} owned", "Runs today on the bench."],
        ["U2", "ESP32-CAM with GC2145 camera (RHYX M21-45, PID 0x2145)", "1", f"{C} owned", "AI-Thinker layout, confirmed by a working camera init."],
        ["—", "ESP32-CAM-MB flashing board (FT232R)", "1", f"{C} owned", "No auto-reset: IO0 + RST to flash. Not on the rover."],
        ["US1–US4", "HC-SR04 ultrasonic sensor", "4", f"{C} team list", "US1 wired and tested. Rear enables reverse."],
        ["SV1", "SG90 servo, 180° positional, + pan bracket", "1", f"{C} team list; bracket must add", "The earlier MG996R moved and held; the SG90 is not tested."],
        ["R1–R6", "10 kΩ pull-down at driver inputs", "6", R, "RPWM and LPWM of each module (R1–R4), each module's EN wire (R5, R6)."],
        ["R11, 13, 15, 17", "1 kΩ, Echo series", "4", R, ""],
        ["R12, 14, 16, 18", "1.8 kΩ, Echo to GND", "4", R, "2 kΩ is also fine (3.33 V)."],
        ["R21, R23", "100 kΩ, voltage sense top", "2", f"{R}, must add", "R23 required (E-stop sense); R21 optional."],
        ["R22, R24", "22 kΩ, voltage sense bottom", "2", f"{R}, must add", "R24 required; R22 optional."],
        ["R25", "1 kΩ 0.5 W, motor-bus bleed", "1", f"{R}, must add", "Empties the bus in about 1 s after SW1 opens."],
        ["C1", "470 µF 16 V electrolytic at U2 5V", "1", R, "ESP32-CAM brownouts are common."],
        ["C2, C15–C18, C21, C22", "100 nF ceramic", "7", R, "U2 5V, each sonar, each ADC pin."],
        ["C3", "470 µF 16 V electrolytic at the servo", "1", R, "Servo current spikes."],
        ["C11–C14", "100 µF 16 V electrolytic at each sonar", "4", f"{R}, new, not tested", "Against the stray readings seen on the bench."],
        ["—", "Wire: 14, 18, 22, 24 and 26 AWG", "—", R, "Red and black for power, other colours for signals (rule 3.2.3)."],
        ["—", "Double-sided vero board, female headers × 2, 4-pin JST connectors 15 × 90° + 15 × straight", "—", f"{C} team list", "Rule 3.1. P1–P13 assignment in docs/03_electrical.md §3.10."],
        ["—", "Dupont housings, heat shrink, cable ties", "—", f"{R}, must add", "IBT-2 control headers are male pins."],
        ["U3", "ESP32 DevKit (D0WD-V3, 26 MHz crystal, CP2102)", "1", f"{C} owned, optional", "Audio only."],
        ["U8", "INMP441 I²S microphone (list says \"INA 441\")", "1", f"{C} team list; name {U}", "Optional. Read the marking first."],
        ["U9", "MAX98357A I²S amplifier", "1", f"{C} team list", "Optional."],
        ["LS1", "4 Ω 3 W speaker", "1", "team notes", "Optional."],
    ]


def test_rows():
    blank = ""
    return [
        ["#", "Test", "How", "Expected", "Measured", "OK"],
        ["T1", "No short on the power rails", "Packs unplugged. Meter BT1 + and BT2 + to GND, the bus to GND, the 5 V rails to GND.", "No continuity beep", blank, "☐"],
        ["T2", "Pack voltages", "Plug in each pack. Meter after F1 and after F2.", "10.5–12.6 V each", blank, "☐"],
        ["T3", "Set U6", "SW2 ON, nothing on the U6 output. Adjust the output.", "4.9–5.1 V", blank, "☐"],
        ["T4", "Echo divider", "5 V on the sonar side of each divider. Meter on the GPIO side, board not plugged in.", "3.2 V (3.33 V with 2 kΩ)", blank, "☐"],
        ["T5", "S3 from U6", "USB unplugged. Plug in U1 only.", "S3 boots, Wi-Fi joins, no restarts", blank, "☐"],
        ["T6", "Camera from U6", "Plug in U2 with C1, C2. Watch the dashboard for 60 s.", "Link ok, no restarts", blank, "☐"],
        ["T7", "Power on, E-stop OFF", "GPIO2 bench jumper removed. J3, J4, J5 unplugged. SW2 ON, SW1 OFF.", "Bus 0 V, S3 in ESTOP", blank, "☐"],
        ["T8", "E-stop ON", "Switch SW1 ON, then RESET E-STOP in the dashboard.", "Bus = BT1; ESTOP held until RESET", blank, "☐"],
        ["T9", "E-stop", "Switch SW1 OFF.", "Bus ~0 V within 1 s, ESTOP latched", blank, "☐"],
        ["T10", "Back ON does not restart", "Switch SW1 back ON. Try RESET while it is still OFF. Repeat after T17 with wheels raised and spinning.", "No motion. RESET refused while OFF", blank, "☐"],
        ["T11", "Sense wire broken", "SW1 ON, unplug the GPIO2 sense lead (P12).", "S3 latches ESTOP", blank, "☐"],
        ["T12", "Set U7", "SW1 ON, servo not plugged in. Adjust the U7 output.", "4.9–5.1 V", blank, "☐"],
        ["T13", "Servo from U7", "Plug in SV1 with C3. Move the pan slider. Switch E-stop OFF while it moves.", "Moves 45–135°; stops when the bus drops", blank, "☐"],
        ["T14", "Driver logic", "Motors off. Meter VCC on each module, then RPWM, LPWM and EN while holding RST on the S3.", "VCC 3.3 V. Inputs about 0 V", blank, "☐"],
        ["T15", "Sonar accuracy on 5 V", "Ruler, flat object fixed in place, sonar fixed. 10, 20, 50 cm. Hands away. Close spare dashboard tabs.", "Note the error. Window at 100 mm is ±20 mm", blank, "☐"],
        ["T16", "Voltage sense", "Compare the dashboard bus (and BT2) to a meter, then set MOTOR_BUS_CAL_GAIN (and BATTERY_CAL_GAIN).", "Matches the meter after calibration", blank, "☐"],
        ["T17", "Each motor alone", "Bench supply 12 V: one motor stalled for ≤2 s, note the current; set F1. Then wheels raised, one motor at a time on low duty.", "Stall A =; same direction per side", blank, "☐"],
        ["T18", "Stop test for inspection", "Wheels raised, motors running. Switch E-stop OFF.", "Wheels stop, bus 0 V on a meter", blank, "☐"],
    ]


def build_story():
    a4w = A4[0] - 2 * 42
    lw = landscape(A4)[0] - 2 * 34
    s = []
    # ---- cover
    s += [Spacer(1, 24), para("Eco eco Rover", TITLE), para("Electrical schematic and build guide", SUB)]
    s.append(table([["For", "Electrical team, Team Eco eco, URRT Robo Cup"],
                    ["Revision", f"{REV}, {DATE}"],
                    ["Repository", "github.com/Jannatul-Naim/Eco_Eco"],
                    ["Rulebook", "Mechanical &amp; Electrical Rulebook V1.3"],
                    ["Pins come from", "src/s3/config_pins.h, src/cam/config_cam.h, src/audio/config_audio.h"],
                    ["Rebuild this PDF", "python3 tools/make_electrical_pdf.py"]], [90, a4w - 90], head=False))
    s.append(Spacer(1, 12))
    s.append(callout([para("<b>Status of this design</b>", BODY),
                      para("Build from this document, but know what it is. The power side (packs, fuses, E-stop switch, buck converters, motor drivers, motors) has <b>not been built or tested</b>. Section 10 lists exactly what has run on real hardware. Rev D follows the team's component list of 13 Nov 2026.", SMALL)], a4w))
    s.append(Spacer(1, 8))
    s.append(para("Before you buy or solder", H2))
    s += bullets(["<b>Measure one motor's stall current</b> (T17). F1 = 10 A assumes the 25GA motors stall at 2.5 A or less.",
                  "<b>Buy what the list is missing:</b> fuses and holders, XT30 pairs, resistors, capacitors, a 3S LiPo balance charger, a LiPo bag (Section 7).",
                  "<b>Read the E-stop switch rating.</b> SW1 carries all motor current, so it must be DC-rated at 10 A or more.",
                  "<b>Drivers: two BTS7960 (IBT-2) modules</b>, by team decision. Power their VCC from the S3's 3.3 V, not 5 V (Sheet 4).",
                  "<b>Never connect the 5 V sonar Echo directly to an S3 pin.</b> Use the divider on Sheet 5.",
                  "<b>Never power the servo</b> from a board's 5 V pin or from the logic buck.",
                  "<b>E-stop: the team chose a 2-pin switch</b> in the motor line instead of the red mushroom-head button rule 1.2.1 asks for, and no contactor: \"no restart\" is done by the S3, not hardware. Get the organisers' approval in writing before you build it. An inspector may refuse it, and a rover that fails the E-stop test may not run (rule 1.2.3).",
                  "<b>No breadboard anywhere on the rover</b> (rule 3.1.2). The rover is not allowed to run with one.",
                  "<b>Check your board's silkscreen</b> against Sheet 1. If a label differs, stop and ask before you solder."])
    s.append(para("Contents", H2))
    toc = TableOfContents()
    toc.levelStyles = [ParagraphStyle("TOC", parent=SMALL, fontSize=8.6, leading=11.6, spaceAfter=0)]
    toc.dotsMinLevel = 0
    s.append(toc)
    # ---- section 1
    s += [PageBreak(), Heading("1. How to read this document", H1)]
    s.append(table([["Label", "Meaning"],
                    [C, "The team has said it owns this or has decided on it."],
                    [R, "Proposed here, with the reason given. Replace it if you have a better verified part."],
                    [U, "Needed but not identified yet. Do not wire anything that depends on it until it is checked."]], [90, a4w - 90]))
    s.append(para("Reference letters", H2))
    s.append(table([["Ref", "What", "Ref", "What"],
                    ["BT1, BT2", "motor pack, logic pack", "U1", "ESP32-S3 main controller (only motor authority)"],
                    ["F1, F2", "fuses, one per pack", "U2", "ESP32-CAM: video, QR, motion, servo"],
                    ["J1–J7", "XT30 power connectors", "U3", "ESP32 DevKit: audio, optional"],
                    ["P1–P13", "4-pin JST signal connectors", "U4, U5", "BTS7960 motor drivers, left and right"],
                    ["SW1", "E-stop switch, motor line", "U6, U7", "XL4015 bucks, logic and servo"],
                    ["SW2", "power switch, logic line", "U8, U9, LS1", "microphone, amplifier, speaker, optional"],
                    ["M1–M4", "motors", "US1–US4", "sonars (US, so they are not confused with the S3)"],
                    ["R, C", "resistors, capacitors", "SV1", "SG90 pan servo"]], [58, 150, 58, a4w - 266]))
    s.append(para("How the parts fit together", H2))
    s += bullets(["<b>S3 (U1)</b> is the only board that can command the motors. It runs missions, the sonars and the dashboard.",
                  "<b>ESP32-CAM (U2)</b> handles video, QR, motion and the pan servo. It talks to the S3 over three wires, not Wi-Fi: S3 TX, S3 RX and GND.",
                  "<b>DevKit (U3)</b> handles audio and talks over Wi-Fi only. It is optional.",
                  "<b>The E-stop works without any of them.</b> SW1 cuts the motor line directly, so motors and servo lose power even if every board has crashed. The S3 watches the bus voltage to keep motion from restarting."])
    # ---- section 2
    s.append(Heading("2. Decisions needed before buying", H1))
    s.append(table([["Item", "Assumed here", "Why it matters", "How to settle it", "Decided"],
                    ["Motors", "25GA 300 RPM 12 V, stall ≤2.5 A assumed", "Sets F1 and the SW1 rating", "Measure one stalled on a bench supply (T17)", "motor yes; stall open"],
                    ["Motor drivers", "2 × BTS7960 (IBT-2), VCC from 3.3 V", "Logic level and EN wiring", "Check VCC and inputs with test T14", "yes, 11 Nov 2026"],
                    ["Batteries", "2 × 3S LiPo 850 mAh, split motor / logic", "Run time and LiPo safety", "Buy a 3S balance charger and a LiPo bag; check plugs", "yes, 13 Nov 2026"],
                    ["E-stop SW1", "2-pin switch in the motor line, no contactor", "1.2.1 not met; no-restart is firmware. SW1 must carry the motor current", "Read its rating; ask the organisers in writing", "team decision, 13 Nov 2026"],
                    ["Fan", "12 V 4-inch, on BT2 after SW2", "Its current adds to F2", "Read the label", ""],
                    ["Sonar count", "4 in the list, 1 wired", "Reverse is blocked until the rear sonar is fitted", "Wire the rear one next", "yes"]],
                   [62, 96, 118, 150, a4w - 426]))
    # ---- section 3
    s.append(PageBreak())
    s.append(Heading("3. Rulebook checklist", H1))
    s.append(table([["Rule", "Requires", "This design", "Where"],
                    ["1.2.1", "Visible red mushroom E-stop, reachable from outside without crossing moving parts", '<font color="#B03A2E"><b>NOT MET</b></font> by team decision: SW1 is a 2-pin switch. Get the organisers\' approval in writing', "Sheet 3"],
                    ["1.2.2", "Removes power from all actuators, in hardware, without software or wireless", "SW1 cuts the motor line. Motors and the servo buck are on the bus", "Sheet 3"],
                    ["1.2.3", "Tested at inspection", "Tests T9 and T18", "Section 9"],
                    ["1.2.4", "Release must not restart motion", '<font color="#B03A2E"><b>Firmware, not hardware:</b></font> the S3 latches ESTOP on bus loss and refuses RESET until the bus is back; R5, R6 hold EN LOW', "Sheet 3, T10"],
                    ["3.1.1–3", "Soldered veroboard or PCB, no breadboards, modules in soldered female headers, other joints soldered or crimped", "BOM and build list", "Section 7, 8"],
                    ["3.1.4", "Insulated, tied down, strain relief, no exposed conductors", "Build list", "Section 8"],
                    ["3.1.5", "Suitable wire gauge, motor wires apart from signals", "Wire schedule gives each size", "Section 5"],
                    ["3.2.1–2", "XT or Deans on every power link, polarised battery connector", "J1–J7 (XT30)", "Sheet 2"],
                    ["3.2.3", "Red positive with voltage labelled at each junction, black GND, other colours for signals", "Colours in the wire schedule", "Sheet 2, Section 5"],
                    ["3.3.1–3", "≤24 V nominal, fused, secured, fire-resistant bag, no damaged packs", "3S, F1 and F2, bag in the BOM", "Sheet 2"],
                    ["3.4.2", "Schematic shows controller, drivers, sensors, motors, battery, fuse, E-stop, every pin and rail voltage", "Sheets 1–7, pin maps", "Section 4, 6"],
                    ["3.4.3", "Schematic and pin map in the GitHub repository", "This PDF, in docs/", "Repository"],
                    ["3.4.4", "Built circuit must match the schematic before the M4 freeze", "Change the sheets when the build changes", "tools/ script"]],
                   [40, 212, 170, a4w - 422]))
    s.append(Spacer(1, 6))
    s.append(para("Rule 3.4.1 names schematic software or a neat pen-and-paper diagram. These sheets are drawn by a script. If a judge asks for a KiCad or EasyEDA project, redraw from Sheet 1.", SMALL))
    # ---- section 4 (sheets)
    s.append(NextPageTemplate("SheetA3"))
    s.append(PageBreak())
    for i, info in enumerate(SHEETS):
        next_size = SHEETS[i + 1]["size"] if i + 1 < len(SHEETS) else None
        s.append(Sheet(info))
        if next_size:
            s.append(NextPageTemplate("SheetA3" if next_size == "A3" else "SheetA4"))
            s.append(PageBreak())
    # ---- section 5
    s.append(NextPageTemplate("LandA4"))
    s.append(PageBreak())
    s.append(Heading("5. Wire schedule", H1))
    s.append(para("Tick each wire as you make it. Servo and module pigtails keep their factory colours; extension leads follow the code.", SMALL))
    s.append(table(wire_rows(), [32, 104, 130, 124, 58, 46, lw - 494]))
    # ---- section 6
    s.append(NextPageTemplate("PortA4"))
    s.append(PageBreak())
    s.append(Heading("6. Pin maps", H1))
    s.append(para("U1 ESP32-S3", H2))
    t_yes = '<font color="#1E8449"><b>bench</b></font>'
    s.append(table([["GPIO", "Name in firmware", "Direction", "Connects to", "Notes", "Tested"],
                    ["1", "PIN_BATT_ADC", "in, ADC1", "R21, R22, C21 (BT2)", "optional", "no"],
                    ["2", "PIN_MOTOR_BUS_ADC", "in, ADC1", "R23, R24, C22 (bus)", "required: E-stop sense", "no"],
                    ["4", "PIN_LEFT_RPWM", "out, PWM", "U4 RPWM, left forward", "R1 to GND", "no"],
                    ["5", "PIN_LEFT_LPWM", "out, PWM", "U4 LPWM, left reverse", "R2 to GND", "no"],
                    ["6", "PIN_RIGHT_RPWM", "out, PWM", "U5 RPWM, right forward", "R3 to GND", "no"],
                    ["7", "PIN_RIGHT_LPWM", "out, PWM", "U5 LPWM, right reverse", "R4 to GND", "no"],
                    ["10", "PIN_SONAR_TRIG[0]", "out", "US1 Trig", "front centre", t_yes],
                    ["11", "PIN_SONAR_ECHO[0]", "in", "US1 Echo via R11, R12", "internal pull-down on", t_yes + ", 3.3 V"],
                    ["12", "PIN_SONAR_TRIG[1]", "out", "US2 Trig", "front left", "no"],
                    ["13", "PIN_SONAR_ECHO[1]", "in", "US2 Echo via R13, R14", "", "no"],
                    ["14", "PIN_SONAR_TRIG[2]", "out", "US3 Trig", "front right", "no"],
                    ["15", "PIN_DRIVER_EN", "out", "U4, U5 R_EN + L_EN", "R5, R6 to GND", "no"],
                    ["16", "PIN_SONAR_ECHO[2]", "in", "US3 Echo via R15, R16", "", "no"],
                    ["17", "PIN_SONAR_TRIG[3]", "out", "US4 Trig", "rear", "no"],
                    ["18", "PIN_SONAR_ECHO[3]", "in", "US4 Echo via R17, R18", "", "no"],
                    ["21", "PIN_ESTOP_SENSE", "in, pull-up", "not used", "2-pole switch only", "no"],
                    ["41", "PIN_CAM_UART_TX", "out", "U2 IO13", "115200 8N1", t_yes],
                    ["42", "PIN_CAM_UART_RX", "in", "U2 IO14", "115200 8N1", t_yes]],
                   [30, 100, 50, 124, 90, a4w - 394]))
    s.append(Spacer(1, 4))
    s.append(para("Spare: 8, 9, 39, 40. Avoid: 0, 3, 45, 46 (strapping) · 19, 20 (USB) · 26–32 (flash) · 33–37 (octal PSRAM) · 38, 48 (RGB LED) · 43, 44 (serial monitor) · 47. ADC2 pins do not work while Wi-Fi runs, so sensing stays on ADC1.", SMALL))
    s.append(para("U2 ESP32-CAM (AI-Thinker)", H2))
    s.append(table([["GPIO", "Use", "Connects to", "Tested"],
                    ["13", "link RX", "U1 GPIO41", t_yes], ["14", "link TX", "U1 GPIO42", t_yes],
                    ["2", "pan servo signal, 50 Hz, strapping pin: nothing may pull it HIGH at reset", "SV1 orange", t_yes],
                    ["4", "flash LED, kept off", "none", ""], ["1, 3", "programming and serial monitor", "MB board only", t_yes],
                    ["0", "camera XCLK and boot strap: GND at reset = flash mode", "camera", t_yes],
                    ["32, 26, 27, 25, 23, 22, 35, 34, 39, 36, 21, 19, 18, 5", "camera, on the board", "camera", t_yes],
                    ["16", "PSRAM, do not use", "none", ""], ["12, 15", "avoid: 12 sets flash voltage at boot, 15 pulses at power-up", "none", ""]],
                   [100, 230, 80, a4w - 410]))
    s.append(para("U3 ESP32 DevKit (optional)", H2))
    s.append(table([["GPIO", "Use", "Part pin", "Tested"],
                    ["26", "I²S0 BCLK", "U8 SCK", "no"], ["25", "I²S0 WS", "U8 WS", "no"], ["34", "I²S0 data in", "U8 SD", "no"],
                    ["27", "I²S1 BCLK", "U9 BCLK", "no"], ["32", "I²S1 LRC", "U9 LRC", "no"], ["33", "I²S1 data out", "U9 DIN", "no"],
                    ["23", "amp enable, HIGH = on", "U9 SD", "no"]], [60, 200, 100, a4w - 360]))
    # ---- section 7
    s.append(NextPageTemplate("LandA4"))
    s.append(PageBreak())
    s.append(Heading("7. Bill of materials", H1))
    s.append(table(bom_rows(), [78, 240, 26, 136, lw - 480]))
    s.append(para("Shopping list (beyond what the team owns)", H2))
    s.append(para("Must add to the team list: 2 inline blade-fuse holders with 10 A and 3 A slow-blow fuses (and spares) · 7 XT30 pairs · 3S LiPo balance charger · fire-resistant LiPo bag · resistors: 10 k × 6, 1 k × 4, 1.8 k × 4, 100 k × 2, 22 k × 2, 1 k 0.5 W × 1 · capacitors: 470 µF 16 V × 2, 100 µF 16 V × 4, 100 nF × 7 · 18 AWG and 22 AWG silicone wire red and black, 24–26 AWG coloured · Dupont housings, heat shrink, cable ties · SG90 pan bracket · recommended: LiPo cell checker. Replace SW1 only if its printed rating is below 10 A DC.", SMALL))
    s.append(callout(para("<b>Buy first:</b> the fuses, XT30 pairs, the charger and bag, and the resistors (R23, R24, R25 are needed before the E-stop works).", SMALL), lw))
    # ---- section 8
    s.append(NextPageTemplate("PortA4"))
    s.append(PageBreak())
    s.append(Heading("8. Calculations and build list", H1))
    s.append(para("5 V logic budget (U6, XL4015, 5 A)", H2))
    s.append(table([["Load", "Peak at 5 V"], ["U1 ESP32-S3, Wi-Fi peak", "0.35 A"], ["U2 ESP32-CAM", "0.5 A"], ["US1–US4", "0.06 A"],
                    ["U3 audio board", "0.3 A"], ["U9 amplifier, full volume", "0.7 A"], ["<b>Total</b>", "<b>1.9 A, well inside 5 A</b>"]], [260, a4w - 260]))
    s.append(para("Pack current, worst case", H2))
    s.append(table([["Pack", "Load", "Working", "Current"],
                    ["BT1", "Both sides stalled", "4 motors × 2.5 A (assumed; measure in T17)", "10 A"],
                    ["BT1", "Servo stalled, through U7", "1 A × 5 V / 0.85 efficiency (assumed) / 11.1 V", "0.5 A"],
                    ["BT1", "Bleed and divider", "12.6 V / 1 kΩ", "13 mA"],
                    ["BT2", "Logic, through U6", "1.9 A × 5 V / 0.85 efficiency (assumed) / 11.1 V", "1.0 A"],
                    ["BT2", "Fan", "label", f"{U}"]], [40, 120, 220, a4w - 380]))
    s.append(Spacer(1, 4))
    s.append(para("F1 = 10 A blows on a full four-motor stall at the assumed 2.5 A each, which is its job. An 850 mAh pack runs roughly 25–40 minutes of normal driving at 1.3–2 A (estimate): charge both packs before every run and time it in T17.", SMALL))
    s.append(para("Dividers", H2))
    s.append(table([["Divider", "Working", "Result"],
                    ["Sonar Echo, 1 k / 1.8 k", "5 V × 1.8 / (1 + 1.8)", "3.21 V"],
                    ["Sonar Echo, 1 k / 2 k", "5 V × 2 / (1 + 2)", "3.33 V"],
                    ["Pack or bus, 100 k / 22 k, full", "12.6 V × 22 / 122", "2.27 V"],
                    ["Pack or bus, 100 k / 22 k, LiPo floor", "9.9 V × 22 / 122", "1.79 V"],
                    ["E-stop threshold", "6 V × 22 / 122", "1.08 V"]], [170, 200, a4w - 370]))
    s.append(para("Motor voltage", H2))
    s.append(para("A full 3S pack is 12.6 V, 5 % above the 12 V motor rating. The firmware caps PWM duty at 12.0 V / motor-bus voltage while the bus is measured, and at a fixed 0.95 otherwise.", SMALL))
    s.append(para("Build list", H2))
    tick = ParagraphStyle("Tick", parent=SMALL, leftIndent=2, spaceAfter=2)
    s += [para(f"☐  {x}", tick) for x in ["Every module on soldered female headers on veroboard or PCB. No breadboard anywhere.",
                                     "Every power joint uses XT30, XT60 or Deans. No bare wire, clips or tape.",
                                     "Red = positive with the voltage labelled at each junction. Black = GND. Other colours for signals.",
                                     "Heat shrink on every joint, cable ties, strain relief at every connector.",
                                     "Motor wires routed away from sonar, servo and camera-link wires.",
                                     "SW1 labelled E-STOP, rigidly mounted and reachable from outside without crossing moving parts. SW2 labelled POWER.",
                                     "Both packs fused at the pack, strapped down against vibration, in the fire-resistant bag when charging.",
                                     "GPIO2 bench jumper removed once R23/R24 are fitted.",
                                     "Built circuit matches these sheets. If it does not, change the sheets before the M4 freeze."]]
    # ---- section 9
    s.append(NextPageTemplate("LandA4"))
    s.append(PageBreak())
    s.append(Heading("9. Commissioning tests", H1))
    s.append(para("Do these in order, with the wheels raised for anything that can move. Write down what you measure and send it back: nothing counts as passed until someone has seen it on the real rover. Full stages are in docs/06_bringup_guide.md.", SMALL))
    s.append(table(test_rows(), [26, 136, 280, 150, 70, lw - 662]))
    # ---- section 10
    s.append(NextPageTemplate("PortA4"))
    s.append(PageBreak())
    s.append(Heading("10. What has been tested, and open problems", H1))
    s.append(para("Run on real hardware (bench, 1–2 Nov 2026)", H2))
    s.append(table([["What", "Result"],
                    ["S3 on USB: boot, Wi-Fi, dashboard", "Runs. Telemetry 76–79 ms per frame"],
                    ["Camera link: GPIO41, 42 to IO13, 14", "138 of 138 samples ok, 0 bad messages in 30 s"],
                    ["E-stop sense, old 2-pole design: GPIO21", "Open: latches ESTOP at boot. Jumper to GND: released. Replaced by bus sensing on GPIO2 (Rev D), which is not tested"],
                    ["MG996R on IO2, separate 5 V supply", "Moves and holds. Sweep 45–135° smooth, no buzzing (team observation). The SG90 is not tested"],
                    ["HC-SR04 on GPIO10, 11, on 3.3 V", "Works, but 25 mm long at 100 mm, and strays at 1.8 m (Sheet 5)"],
                    ["AUTO QR mission", "COMPLETE 4 of 4 in 2.1–4.6 s, QR held in view, camera not on the servo"],
                    ["Heavy web traffic", "Crash found and fixed: ran 120 s under the same load, no restart"]], [210, a4w - 210]))
    s.append(para("Not tested at all", H2))
    s.append(para("LiPo packs, fuses, XT connectors · E-stop switch and motor-bus sensing · both XL4015 bucks · S3 and camera fed from U6 · motor drivers and motors · SG90 · sonars on 5 V with the divider and capacitors · sonars US2 to US4 · voltage dividers · microphone and speaker · camera mounted on the servo for the tracking task.", SMALL))
    s.append(para("Open problems", H2))
    s.append(table([["Problem", "Status", "Next step"],
                    ["S3 restarted once by itself (reset reason int_wdt), idle, on earlier firmware", "Cause unknown. Wiggling sonar wires did not bring it back. About 20 minutes of serial logging on the fixed firmware showed no restart", "Keep the serial log on during first motor tests"],
                    ["Sonar reads 25 mm long at 100 mm on 3.3 V", "Fails the ±20 mm window", "Build Sheet 5 on 5 V, then test T15"],
                    ["Sonar strays at long range", "Suspected supply noise, not proven", "Capacitors, 5 V, then a firmware filter if still needed"],
                    ["No-restart after the E-stop is firmware, not hardware", "Team decision: no contactor in the list, 13 Nov 2026", "Test T10; organisers' view at inspection"],
                    ["SW1 rating and the motors' stall current are unknown", "Nothing measured yet", "Read SW1's print; test T17; then set F1"],
                    ["S3 5V pin as a supply input", "Not tested. It did not feed the camera as an output", "Test T5 with USB unplugged"],
                    ["E-stop is a switch, not the mushroom-head button rule 1.2.1 asks for", "Team decision, 6 and 13 Nov 2026", "Organisers' approval in writing before inspection"],
                    ["Dashboard login added (13 Nov 2026), plain HTTP", "Keeps casual users out; not encrypted", "Set DASHBOARD_PASSWORD in src/s3/secrets.h"]], [190, 190, a4w - 380]))
    s.append(para("Revision history", H2))
    s.append(table([["Rev", "Date", "Change"], ["A", "2 Nov 2026", "First issue. Adds J2, sonar capacitors C11–C18 and the K1 re-latch check to the design in docs/03_electrical.md."],
                    ["B", "6 Nov 2026", "E-stop SW1 changed from the red mushroom button to a 2-pole ON-OFF switch, by team decision. Rule 1.2.1 marked NOT MET."],
                    ["C", "11 Nov 2026", "Motor drivers U4, U5 changed from MD30C to BTS7960 (IBT-2) by team decision: RPWM, LPWM and a shared EN; VCC from U1 3V3; EN pull-downs R5, R6. Firmware is BTS7960-only."],
                    ["D", "13 Nov 2026", "Team component list: two 3S LiPo 850 mAh packs (BT1 motors, BT2 logic), 25GA 12 V motors, SG90, XL4015 bucks, 2-pin switches. Contactor, ARM and hold path removed: SW1 cuts the motor line, the S3 senses it on GPIO2 (R23, R24, bleed R25). Fuses F1 10 A, F2 3 A; XT30 J1–J7."]], [34, 70, a4w - 104]))
    return s


# ---------------------------------------------------------------- page templates
def small_header(c, doc):
    w, h = c._pagesize
    c.saveState()
    c.setFillColor(INK)
    c.rect(0, h - 26, w, 26, stroke=0, fill=1)
    c.setFillColor(GOLD)
    c.setFont("BodyB", 9)
    c.drawString(34, h - 17, "ECO ECO ROVER")
    c.setFillColor(WHITE)
    c.setFont("Body", 8)
    c.drawString(112, h - 17, "Electrical schematic and build guide")
    c.drawRightString(w - 34, h - 17, f"Rev {REV}   ·   {DATE}")
    c.setFillColor(MUTE)
    c.setFont("Body", 8)
    c.drawRightString(w - 34, 18, f"Page {c.getPageNumber()}")
    c.restoreState()


class Doc(BaseDocTemplate):
    def beforeDocument(self):
        self.toc_key = 0

    def afterFlowable(self, f):
        title = None
        if isinstance(f, Heading):
            title = f.getPlainText()
        elif isinstance(f, Sheet):
            title = f"4.{f.info['number']}  Sheet {f.info['number']}: {f.info['title']}"
        if title:
            key = f"h{self.toc_key}"
            self.toc_key += 1
            self.canv.bookmarkPage(key)
            self.canv.addOutlineEntry(title, key, 0, False)
            self.notify("TOCEntry", (0, title, self.page, key))


def main():
    check_pins()
    OUT.parent.mkdir(exist_ok=True)
    doc = Doc(str(OUT), pagesize=A4, title="Eco eco Rover: Electrical schematic and build guide", author="Team Eco eco",
              subject="URRT Robo Cup, Mechanical & Electrical Rulebook V1.3, rules 3.4.2 and 3.4.3", creator="tools/make_electrical_pdf.py")
    aw, ah = A4
    lw, lh = landscape(A4)
    a3w, a3h = landscape(A3)
    def frame(x, y, w, h, fid):
        return Frame(x, y, w, h, id=fid, leftPadding=0, rightPadding=0, topPadding=0, bottomPadding=0)
    doc.addPageTemplates([
        PageTemplate("PortA4", [frame(42, 36, aw - 84, ah - 36 - 38, "p")], onPage=small_header, pagesize=A4),
        PageTemplate("LandA4", [frame(34, 32, lw - 68, lh - 32 - 36, "l")], onPage=small_header, pagesize=(lw, lh)),
        PageTemplate("SheetA4", [frame(18, 18, lw - 36, lh - 36, "s4")], pagesize=(lw, lh)),
        PageTemplate("SheetA3", [frame(18, 18, a3w - 36, a3h - 36, "s3")], pagesize=(a3w, a3h)),
    ])
    doc.multiBuild(build_story())
    print(f"Wrote {OUT}")


if __name__ == "__main__":
    main()
