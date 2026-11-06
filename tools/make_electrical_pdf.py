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
REV = "B"
DATE = "6 Nov 2026"

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


# ---------------------------------------------------------------- firmware cross-check
def check_pins():
    """The drawings below use these numbers. Fail loudly if the firmware moved a pin."""
    s3 = (ROOT / "src/s3/config_pins.h").read_text()
    cam = (ROOT / "src/cam/config_cam.h").read_text()
    audio = (ROOT / "src/audio/config_audio.h").read_text()
    expect = [
        (s3, r"PIN_BATT_ADC = (\d+)", "1"), (s3, r"PIN_MOTOR_BUS_ADC = (\d+)", "2"),
        (s3, r"PIN_LEFT_A = (\d+)", "4"), (s3, r"PIN_LEFT_B = (\d+)", "5"),
        (s3, r"PIN_RIGHT_A = (\d+)", "6"), (s3, r"PIN_RIGHT_B = (\d+)", "7"),
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
    ("3V3", "not used", GREY), ("3V3", "not used", GREY), ("RST", "not used", GREY),
    ("GPIO4", "U4 PWM, left driver", YEL), ("GPIO5", "U4 DIR, left driver", BLUE),
    ("GPIO6", "U5 PWM, right driver", YEL), ("GPIO7", "U5 DIR, right driver", BLUE),
    ("GPIO15", "BTS7960 EN only", GREY), ("GPIO16", "US3 Echo, via R15", PURPLE),
    ("GPIO17", "US4 Trig", GREEN), ("GPIO18", "US4 Echo, via R17", PURPLE),
    ("GPIO8", "spare", GREY), ("GPIO3", "avoid: strapping", GREY), ("GPIO46", "avoid: strapping", GREY),
    ("GPIO9", "spare", GREY), ("GPIO10", "US1 Trig", GREEN), ("GPIO11", "US1 Echo, via R11", PURPLE),
    ("GPIO12", "US2 Trig", GREEN), ("GPIO13", "US2 Echo, via R13", PURPLE), ("GPIO14", "US3 Trig", GREEN),
    ("5V", "+5V LOGIC (input)", RED), ("GND", "GND", BLACK),
]
S3_RIGHT = [
    ("GND", "GND", BLACK), ("TX 43", "avoid: serial monitor", GREY), ("RX 44", "avoid: serial monitor", GREY),
    ("GPIO1", "battery sense, R21", TEAL), ("GPIO2", "bus sense, R23", TEAL),
    ("GPIO42", "U2 IO14 (camera TX)", TEAL), ("GPIO41", "U2 IO13 (camera RX)", TEAL),
    ("GPIO40", "spare", GREY), ("GPIO39", "spare", GREY), ("GPIO38", "avoid: RGB LED", GREY),
    ("GPIO37", "avoid: PSRAM", GREY), ("GPIO36", "avoid: PSRAM", GREY), ("GPIO35", "avoid: PSRAM", GREY),
    ("GPIO0", "avoid: strapping", GREY), ("GPIO45", "avoid: strapping", GREY), ("GPIO48", "avoid: RGB LED", GREY),
    ("GPIO47", "avoid", GREY), ("GPIO21", "SW1 pole B, E-stop sense", ORANGE),
    ("GPIO20", "avoid: USB", GREY), ("GPIO19", "avoid: USB", GREY), ("GND", "GND", BLACK), ("GND", "GND", BLACK),
]


def motor_pair(p, xout, y_ma, y_mb, xa, xb, y1, y2, m1, m2, r):
    """Two motors in parallel: MA feeds one common side, MB the other."""
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
    p.zone(0, 0, W, 236, "POWER AND E-STOP  (detail on Sheets 2 and 3)")
    p.part(8, 24, 72, 62, "BT1", "", ["3S Li-ion + BMS", "11.1 V nominal", "12.6 V full"])
    p.text(76, 45, "+", size=8, bold=True, anchor="r")
    p.text(76, 77, "−", size=8, bold=True, anchor="r")
    p.wire((80, 42), (122, 42), w=1.7)
    p.xt(101, 42, "J1 XT60", y2=74)
    p.fuse(135, 42, "F1 25 A")
    p.wire((147, 42), (315, 42), w=1.7)
    p.dot(175, 42, color=RED)
    p.text(180, 37, "BAT+ 11.1–12.6 V", size=7, color=RED, bold=True)
    p.no_contact(330, 42)
    p.text(330, 28, "K1 contact, ≥40 A DC", size=6.8, bold=True, anchor="c")
    p.wire((345, 42), (470, 42), w=1.7)
    p.xt(382, 42, "J2 XT60", y2=74)
    p.dot(470, 42, color=RED)
    p.text(466, 56, "MOTOR BUS", size=7, color=RED, bold=True, anchor="r")
    p.text(466, 65, "0 V or 12.6 V", size=7, color=RED, anchor="r")
    p.wire((80, 74), (432, 74), color=BLACK, w=1.7)
    p.gnd(432, 74, stub=3)
    p.text(376, 98, "STAR GND, near driver B−", size=6.8, bold=True)
    p.wire((470, 30), (470, 146), w=1.7)
    power_row(p, 30, "F5 15 A", "J3 XT30")
    p.tag(600, 30, "U4 B+, left driver, 12.6 V", color=RED)
    power_row(p, 58, "F6 15 A", "J4 XT30")
    p.tag(600, 58, "U5 B+, right driver, 12.6 V", color=RED)
    power_row(p, 88, "F8 3 A", "J5 XT30")
    p.part(600, 72, 92, 32, "U7", "servo buck", ["5 V, ≥3 A"])
    p.wire((692, 88), (706, 88))
    p.tag(706, 88, "+5V SERVO: SV1 red", color=RED)
    p.wire((470, 146), (488, 146))
    p.tag(488, 146, "R23, bus sense (optional)", color=RED)

    # ---- E-stop latch
    p.wire((175, 42), (175, 222), w=1.3)
    p.dot(175, 118, color=RED)
    p.wire((175, 118), (208, 118))
    p.fuse(220, 118, "F4 1 A")
    p.wire((232, 118), (255, 118))
    p.no_contact(270, 118)
    p.text(270, 102, "SW2 ARM (NO)", size=6.8, bold=True, anchor="c")
    p.wire((285, 118), (428, 118))
    p.fuse(440, 118, "F7 1 A")
    p.text(375, 114, "hold path", size=6.8, color=MUTE, anchor="c")
    p.wire((452, 118), (470, 118))
    p.dot(470, 118, color=RED)
    p.dot(310, 118, color=RED)
    p.wire((310, 118), (310, 146))
    p.nc_contact(310, 161, vert=True)
    p.wire((310, 176), (310, 188), (352, 188))
    p.dot(335, 188, color=RED)
    p.coil(370, 188)
    p.text(370, 177, "K1 coil", size=6.8, bold=True, anchor="c")
    p.wire((388, 188), (412, 188), color=BLACK)
    p.gnd(412, 188)
    p.wire((335, 188), (335, 204), (365, 204), color=INK)
    p.diode(370, 204, cathode="l")
    p.wire((375, 204), (400, 204), (400, 188), color=INK)
    p.dot(400, 188, color=BLACK)
    p.text(380, 214, "D1 1N4007", size=6.8)
    p.link((370, 180), (370, 62), (330, 62), (330, 50))
    p.lines(190, 134, ["SW1 E-STOP switch, 2-pole, ON = run", "pole A cuts the coil, pole B tells the S3"], size=6.8, lead=8.6)
    p.tag(247, 176, "U1 GPIO21", side="l", color=ORANGE)
    p.nc_contact(262, 176, half=15, color=ORANGE)
    p.wire((277, 176), (290, 176), color=BLACK)
    p.gnd(290, 176)
    p.link((262, 168), (262, 156), (302, 156))

    # ---- logic branch
    p.wire((175, 222), (208, 222))
    p.fuse(220, 222, "F3 3 A")
    p.wire((232, 222), (262, 222))
    p.xt(269, 222, "J6 XT30")
    p.wire((274, 222), (590, 222))
    p.dot(445, 222, color=RED)
    p.wire((445, 222), (445, 215))
    p.tag(445, 215, "FAN1 12 V, current unknown", color=RED, size=6.6)
    p.part(590, 206, 110, 26, "U6", "logic buck", [])
    p.text(594, 226, "5 V, ≥3 A", size=7)
    p.wire((700, 222), (714, 222))
    p.tag(714, 222, "+5V LOGIC: U1 5V, U2 5V, US1–US4 VCC, optional U3 5V and U9 VIN", color=RED, bold=True)

    # ---- legend
    x, y = 842, 24
    p.rect(x, y, 305, 172, fill=PANEL, stroke=RULE)
    p.text(x + 8, y + 13, "How to read this sheet", size=8, bold=True)
    legend = [(RED, "Red: every positive line. Label the voltage at each junction."),
              (BLACK, "Black: GND and battery negative."),
              (YEL, "Yellow: PWM. Blue: DIR."),
              (GREEN, "Green: sonar Trig. Violet: sonar Echo."),
              (TEAL, "Teal: camera link and voltage sense."),
              (ORANGE, "Orange: servo signal and E-stop sense.")]
    for i, (col, label) in enumerate(legend):
        yy = y + 26 + i * 12
        p.wire((x + 8, yy), (x + 28, yy), color=col, w=1.6)
        p.text(x + 34, yy + 2.5, label, size=7.2)
    rows = ["A tag points to the same net elsewhere on the sheet: follow the name.",
            "Crossing wires without a dot are not connected. Dashed grey is a",
            "mechanical link. Gold blocks are XT connectors.",
            "Header order follows ESP32-S3-DevKitC-1. If your board's silkscreen",
            "differs from this sheet, stop and check before you solder."]
    p.lines(x + 8, y + 108, rows, size=7.2, lead=9.4)
    p.text(x + 8, y + 164, "Print on A3, or zoom in on screen.", size=7.2, bold=True)

    top = 242
    # ---- drive zone
    p.zone(0, top, 405, 262, "DRIVE: one MD30C per side, two motors in parallel  (Sheet 4)")
    for i, (ref, side, pwm, dirn, r1, r2, fb, jb, m1, m2) in enumerate((
            ("U4", "left", "GPIO4", "GPIO5", "R1", "R2", "F5", "J3", "M1", "M2"),
            ("U5", "right", "GPIO6", "GPIO7", "R3", "R4", "F6", "J4", "M3", "M4"))):
        y = top + 22 + i * 120
        p.part(128, y, 92, 112, ref, f"MD30C {side}", [])
        p.tag(62, y + 26, f"U1 {pwm}", side="l", color=YEL)
        p.wire((62, y + 26), (128, y + 26), color=YEL)
        p.dot(94, y + 26, color=YEL)
        p.wire((94, y + 26), (94, y + 34), color=YEL)
        p.res(94, y + 43, "", vert=True)
        p.text(99, y + 40, r1, size=6.6, bold=True)
        p.text(99, y + 48, "10 k", size=6.4)
        p.gnd(94, y + 53, stub=1)
        p.text(132, y + 29, "PWM", size=7, mono=True)
        p.tag(62, y + 70, f"U1 {dirn}", side="l", color=BLUE)
        p.wire((62, y + 70), (128, y + 70), color=BLUE)
        p.dot(94, y + 70, color=BLUE)
        p.wire((94, y + 70), (94, y + 76), color=BLUE)
        p.res(94, y + 85, "", vert=True)
        p.text(99, y + 82, r2, size=6.6, bold=True)
        p.text(99, y + 90, "10 k", size=6.4)
        p.gnd(94, y + 95, stub=1)
        p.text(132, y + 73, "DIR", size=7, mono=True)
        p.wire((128, y + 100), (116, y + 100), color=BLACK)
        p.gnd(116, y + 100, stub=2)
        p.text(132, y + 103, "GND", size=7, mono=True)
        p.text(216, y + 25, "B+", size=7, mono=True, anchor="r")
        p.text(216, y + 45, "B−", size=7, mono=True, anchor="r")
        p.text(216, y + 73, "MA", size=7, mono=True, anchor="r")
        p.text(216, y + 107, "MB", size=7, mono=True, anchor="r")
        p.wire((220, y + 22), (232, y + 22))
        p.tag(232, y + 22, f"{fb}, {jb}: MOTOR BUS", color=RED, size=6.8)
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
    p.zone(cx, top, cw, 262, "U2  ESP32-CAM AND SV1  MG996R PAN SERVO  (Sheet 6)")
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
    p.part(sx, top + 72, 92, 76, "SV1", "MG996R", ["180° positional"], size=6.8)
    p.text(sx + 4, top + 111, "orange: signal", size=6.8, color=ORANGE)
    p.text(sx + 4, top + 125, "red: V+", size=6.8, color=RED)
    p.text(sx + 4, top + 139, "brown: GND", size=6.8)
    p.wire((sx, top + 108), (sx - 8, top + 108), color=ORANGE)
    p.tag(sx - 8, top + 108, "U2 IO2", side="l", color=ORANGE, size=6.6)
    p.wire((sx + 92, top + 122), (sx + 102, top + 122))
    p.tag(sx + 102, top + 122, "+5V SERVO", color=RED, size=6.4)
    p.wire((sx + 92, top + 136), (sx + 104, top + 136), color=BLACK)
    p.gnd(sx + 104, top + 136, stub=2)
    p.lines(cx + 266, top + 166, ["C3 1000 µF across V+ and GND, at the servo.",
                                  "Brown also joins U2 GND: the signal needs it.",
                                  "U7 is on the motor bus, so the E-stop stops",
                                  "the servo too. Keep 45–135° travel."], size=6.6, lead=9.2, color=MUTE)

    # ---- voltage sense
    vy = top + 268
    p.zone(cx, vy, 190, H - vy - 4, "VOLTAGE SENSE, optional (Sheet 7)")
    for i, (src, r1, r2, c1, dest) in enumerate((("BAT+", "R21", "R22", "C21", "U1 GPIO1"), ("MOTOR BUS", "R23", "R24", "C22", "U1 GPIO2"))):
        y = vy + 44 + i * 118
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
@sheet(2, "Power distribution, fuses and connectors", "Design. Not built or measured yet.", "A4")
def sheet_power(p, W, H):
    p.text(12, 18, "Each junction shows its voltage, as rule 3.2.3 asks. Write the same label on the wire or the board.", size=8, color=MUTE)
    y = 62
    p.part(12, y - 22, 76, 66, "BT1", "", ["3S Li-ion + BMS", "BMS ≥25 A", "12.6 V full"])
    p.wire((88, y), (150, y), w=2)
    p.wire((88, y + 34), (420, y + 34), color=BLACK, w=2)
    p.xt(110, y, "J1 XT60", y2=y + 34)
    p.fuse(165, y, "F1 25 A", color=RED)
    p.text(165, y + 16, "next to the battery", size=6.6, color=MUTE, anchor="c")
    p.wire((177, y), (290, y), w=2)
    p.dot(225, y, color=RED)
    p.text(208, y - 9, "BAT+ 12.6 V", size=7.5, color=RED, bold=True)
    p.no_contact(305, y)
    p.text(305, y - 14, "K1 ≥40 A DC", size=7.5, bold=True, anchor="c")
    p.text(305, y + 16, "E-stop: Sheet 3", size=6.6, color=MUTE, anchor="c")
    p.wire((320, y), (462, y), w=2)
    p.xt(365, y, "J2 XT60", y2=y + 34)
    p.text(415, y - 9, "MOTOR BUS", size=7.5, color=RED, bold=True, anchor="c")
    p.text(415, y + 11, "0 V or 12.6 V", size=7, color=RED, anchor="c")
    p.gnd(420, y + 34, stub=4)
    p.text(434, y + 44, "STAR GND", size=7.5, bold=True)
    p.dot(462, y, color=RED)
    p.text(120, y - 28, "14 AWG red", size=6.8, color=MUTE)
    p.text(120, y + 46, "14 AWG black", size=6.8, color=MUTE)
    # branches
    rows = [(y - 18, "F5 15 A", "J3 XT30", "U4 MD30C left, B+", "14 AWG", "2 left motors, 11 A stall"),
            (y + 12, "F6 15 A", "J4 XT30", "U5 MD30C right, B+", "14 AWG", "2 right motors, 11 A stall"),
            (y + 42, "F8 3 A", "J5 XT30", "U7 servo buck IN+", "18 AWG", "U7 output: +5V SERVO to SV1 red, C3 1000 µF")]
    p.wire((462, y - 18), (462, y + 72), w=2)
    for ry, fuse, conn, dest, gauge, why in rows:
        p.wire((462, ry), (488, ry))
        p.fuse(500, ry, fuse)
        p.wire((512, ry), (540, ry))
        p.xt(547, ry, conn)
        p.wire((552, ry), (574, ry))
        p.tag(574, ry, dest, color=RED, bold=True)
        p.text(578, ry + 13, f"{gauge}. {why}", size=6.6, color=MUTE)
    p.wire((462, y + 72), (488, y + 72))
    p.fuse(500, y + 72, "F7 1 A")
    p.wire((512, y + 72), (574, y + 72))
    p.tag(574, y + 72, "E-stop hold path (Sheet 3)", color=RED)
    # logic branch
    ly = 196
    p.wire((225, y), (225, ly), w=1.5)
    p.wire((225, y + 72), (250, y + 72))
    p.dot(225, y + 72, color=RED)
    p.fuse(262, y + 72, "F4 1 A")
    p.wire((274, y + 72), (290, y + 72))
    p.tag(290, y + 72, "SW2 ARM (Sheet 3)", color=RED)
    p.wire((225, ly), (250, ly), w=1.5)
    p.fuse(262, ly, "F3 3 A")
    p.wire((274, ly), (300, ly), w=1.5)
    p.xt(307, ly, "J6 XT30")
    p.wire((312, ly), (420, ly), w=1.5)
    p.text(366, ly - 5, "18 AWG red, 12.6 V", size=6.8, color=MUTE, anchor="c")
    p.dot(345, ly, color=RED)
    p.wire((345, ly), (345, ly + 34))
    p.tag(345, ly + 34, "FAN1 12 V fan, current unknown", color=RED)
    p.part(420, ly - 18, 96, 38, "U6", "logic buck", ["5 V, ≥3 A, 6–24 V in"])
    p.wire((516, ly), (566, ly), w=1.5)
    p.text(540, ly - 5, "+5.0 V", size=7.5, color=RED, bold=True, anchor="c")
    p.dot(566, ly, color=RED)
    loads = ["U1 ESP32-S3, 5V pin", "U2 ESP32-CAM, 5V pin + C1, C2", "US1 to US4, VCC", "U3 audio board, 5V pin (optional)", "U9 amp, VIN (optional)"]
    p.wire((566, ly - 10), (566, ly + 46), w=1.2)
    for i, load in enumerate(loads):
        yy = ly - 10 + i * 14
        p.wire((566, yy), (582, yy))
        p.tag(582, yy, load, color=RED, size=7)
    p.text(584, ly + 66, "22 AWG red. Logic stays on when the E-stop is pressed,", size=6.8, color=MUTE)
    p.text(584, ly + 75, "so the rover can still report why it stopped.", size=6.8, color=MUTE)
    # ground
    gy = 282
    p.wire((12, gy), (520, gy), color=BLACK, w=2)
    p.gnd(30, gy, stub=4)
    p.text(44, gy - 6, "STAR GND: run every return to one point near the driver B− terminals", size=7.5, bold=True)
    grounds = [("U4 B−", "J3 −, 14 AWG"), ("U5 B−", "J4 −, 14 AWG"), ("U7 IN−", "J5 −, 18 AWG"), ("U6 IN−", "J6 −, 18 AWG"),
               ("K1 coil −, D1", "22 AWG"), ("FAN1 −", "22 AWG"), ("Logic board GND", "22 AWG, one wire")]
    for i, (dest, note) in enumerate(grounds):
        x = 22 + i * 72
        p.wire((x + 26, gy), (x + 26, gy + 10), color=BLACK)
        p.text(x + 26, gy + 20, dest, size=6.8, bold=True, anchor="c")
        p.text(x + 26, gy + 29, note, size=6.4, color=MUTE, anchor="c")
    # fuse table
    table = [["Fuse", "Protects", "Rating", "Wire", "Why this size"],
             ["F1", "whole rover", "25 A slow", "14 AWG", "22 A both sides stalled, plus logic and servo"],
             ["F2", "not used", "", "", "number kept free to match docs/03_electrical.md"],
             ["F3", "logic buck + fan", "3 A slow", "18 AWG", "about 1 A at 11.1 V, plus fan (check its label)"],
             ["F4", "ARM button path", "1 A slow", "22 AWG", "K1 coil only. Check the coil current"],
             ["F5, F6", "each driver branch", "15 A slow", "14 AWG", "2 × 5.5 A = 11 A per side at stall"],
             ["F7", "hold path", "1 A slow", "22 AWG", "K1 coil only. Check the coil current"],
             ["F8", "servo buck", "3 A slow", "18 AWG", "about 1.3 A at 11.1 V with the servo stalled"]]
    end = p.grid(12, 330, [46, 92, 50, 48, 240], table)
    p.note(502, 330, W - 514, ["Fuses, driver and wire sizes assume Pololu #4744 (5.5 A stall).",
                               "If you fit the 25GA motors from the team's notes, read their",
                               "stall current first and redo this table. Slow-blow automotive",
                               "blade fuses (ATO/ATC).", "",
                               "Never power the servo from a board's 5 V pin or from U6.",
                               "Never use a generic 12 V adapter as the charger."],
           title="Before you buy", size=7.3, lead=10)
    conn = [["Connector", "Between", "Pin +", "Pin −"],
            ["J1 XT60", "battery and F1", "BAT+", "GND"], ["J2 XT60", "K1 and motor bus", "MOTOR BUS", "GND"],
            ["J3, J4 XT30", "bus and U4, U5", "after F5, F6", "GND"], ["J5, J6 XT30", "U7, U6 inputs", "after F8, F3", "GND"]]
    p.grid(12, end + 10, [80, 120, 76, 40], conn, lead=11.5)
    p.lines(346, end + 22, ["J2 is added to what docs/03 first listed, so the E-stop assembly also",
                            "meets rule 3.2.1: every power link between battery, E-stop,",
                            "distribution and drivers uses an XT or Deans connector."], size=7.2, lead=9.6, color=MUTE)


# ---------------------------------------------------------------- sheet 3, E-stop
@sheet(3, "Hardware E-stop and contactor latch", "Design. Not built. E-stop is a simple switch by team decision: rule 1.2.1 is NOT met.", "A4")
def sheet_estop(p, W, H):
    y = 60
    p.tag(66, y, "BAT+ after F1", side="l", color=RED, bold=True)
    p.wire((66, y), (92, y))
    p.fuse(104, y, "F4 1 A")
    p.wire((116, y), (150, y))
    p.no_contact(168, y, half=18)
    p.text(168, y - 18, "SW2 ARM", size=8, bold=True, anchor="c")
    p.text(168, y + 16, "green, momentary NO", size=6.8, color=MUTE, anchor="c")
    p.wire((186, y), (252, y))
    p.dot(252, y, color=RED)
    p.text(246, y - 5, "N3", size=7.5, bold=True, anchor="r")
    p.tag(66, y + 44, "MOTOR BUS", side="l", color=RED, bold=True)
    p.wire((66, y + 44), (92, y + 44))
    p.fuse(104, y + 44, "F7 1 A")
    p.text(122, y + 56, "hold path: K1 keeps itself on through its own output", size=6.8, color=MUTE)
    p.wire((116, y + 44), (252, y + 44), (252, y))
    p.wire((252, y), (290, y))
    p.nc_contact(308, y, half=18)
    p.text(308, y - 18, "SW1 pole A", size=8, bold=True, anchor="c")
    p.text(313, y + 16, "closed when ON", size=6.4, color=MUTE)
    p.wire((326, y), (392, y))
    p.dot(366, y, color=RED)
    p.coil(410, y, w=36, h=18)
    p.text(410, y - 14, "K1 coil, 12 V", size=8, bold=True, anchor="c")
    p.wire((428, y), (470, y), color=BLACK)
    p.gnd(470, y)
    p.wire((366, y), (366, y + 28), (402, y + 28), color=INK)
    p.diode(408, y + 28, cathode="l")
    p.wire((413, y + 28), (450, y + 28), (450, y), color=INK)
    p.dot(450, y, color=BLACK)
    p.text(460, y + 28, "D1 1N4007, cathode at coil +", size=6.8)
    p.text(460, y + 37, "skip if K1 has a built-in diode (check polarity)", size=6.4, color=MUTE)
    # main contact
    my = y + 88
    p.tag(300, my, "BAT+ after F1", side="l", color=RED, bold=True)
    p.wire((300, my), (392, my), w=2)
    p.no_contact(410, my, half=18)
    p.text(410, my + 16, "K1 main contact, ≥40 A DC", size=7, bold=True, anchor="c")
    p.wire((428, my), (480, my), w=2)
    p.tag(480, my, "MOTOR BUS", color=RED, bold=True)
    p.link((410, y + 9), (410, y + 18), (428, y + 18), (428, my - 16), (410, my - 16), (410, my - 8))
    # sense
    sy = y + 136
    p.tag(66, sy, "U1 GPIO21", side="l", color=ORANGE, bold=True)
    p.text(10, sy + 15, "INPUT_PULLUP", size=6.6, color=MUTE)
    p.wire((66, sy), (150, sy), color=ORANGE)
    p.nc_contact(168, sy, half=18, color=ORANGE)
    p.text(168, sy - 14, "SW1 pole B", size=8, bold=True, anchor="c")
    p.text(168, sy + 16, "closed when ON", size=6.8, color=MUTE, anchor="c")
    p.wire((186, sy), (222, sy), color=BLACK)
    p.gnd(222, sy)
    p.link((168, sy - 22), (168, y + 62), (308, y + 62), (308, y - 2))
    p.text(240, sy + 3, "Switch ON: closed, LOW, run.", size=7)
    p.text(240, sy + 13, "Switch OFF or broken wire: open, HIGH, treated as E-stop.", size=7)
    p.text(174, y + 74, "SW1: 2-pole ON-OFF switch, latching. ON = run, OFF = stop.", size=6.8, bold=True)
    # behaviour
    rows = [["Action", "What happens", "Motor bus"],
            ["Power on", "K1 open. S3 boots with outputs disabled.", "0 V"],
            ["Press ARM (E-stop switch ON)", "Coil on, K1 closes, then K1 holds itself from the bus.", "12.6 V"],
            ["Switch E-stop OFF", "Pole A opens, coil drops, K1 opens. Pole B opens, S3 latches ESTOP.", "0 V"],
            ["Switch E-stop back ON", "Nothing restarts. K1 stays open and S3 stays in ESTOP.", "0 V"],
            ["Restart on purpose", "Switch E-stop ON, press ARM, then RESET E-STOP on the dashboard, then choose a mode.", "12.6 V"],
            ["S3 crash, Wi-Fi lost, dashboard closed", "K1 is not affected. The E-stop still cuts power. S3 watchdog reboots with outputs LOW.", "no change"]]
    end = p.grid(12, 232, [176, 372, 58], rows, lead=13)
    rules_end = p.note(630, 232, W - 642, ["Met, 1.2.2: SW1 cuts the coil",
                                           "directly, no software or wireless.", "",
                                           "NOT MET, 1.2.1: the rulebook asks",
                                           "for a red mushroom-head button. The",
                                           "team chose a switch. Get the organisers'",
                                           "OK in writing before inspection.", "",
                                           "Mount it outside, easy to reach without",
                                           "crossing moving parts. Label it E-STOP."],
                           title="Rules", size=7.2, lead=9.6)
    review_end = p.note(12, max(end, rules_end) + 8, W - 24, [
        "The hold path takes its power from the motor bus. After K1 opens, the bus can stay charged for a moment by the driver capacitors, and by the motors acting as",
        "generators while the wheels still turn. If someone switches the E-stop back ON in that moment, K1 could pull back in, which rule 1.2.4 forbids. The S3 would still hold outputs off, but the",
        "rule wants this in hardware. So: (1) run test T10 in Section 9 with the wheels spinning. (2) If K1 ever re-latches, fit one of these, and update this sheet. A contactor with an auxiliary NO",
        "contact is the clean fix: feed the hold path from BAT+ through that contact instead of from the bus. A 1 kΩ 1 W bleed resistor across the bus is the simple fix, but test it again.",
        "Also check: K1 coil current against F4 and F7, and the DC rating of the E-stop switch contacts against that coil current. Neither is known until the parts are chosen."],
        title="Design review: check this before inspection (not tested, no part fitted for it yet)", size=7.2, lead=9.6)
    terminals = [["Terminal", "Connects to", "Terminal", "Connects to"],
                 ["SW2 terminal 1", "F4, from BAT+", "K1 coil +", "SW1 pole A side 2, D1 cathode"],
                 ["SW2 terminal 2", "N3", "K1 coil −", "STAR GND, D1 anode"],
                 ["SW1 pole A, side 1", "N3", "K1 main contact, side A", "F1 out, BAT+"],
                 ["SW1 pole A, side 2", "K1 coil +", "K1 main contact, side B", "J2 +, MOTOR BUS"],
                 ["SW1 pole B, side 1", "U1 GPIO21", "SW1 pole B, side 2", "Logic GND"]]
    p.grid(12, review_end + 8, [128, 120, 128, 150], terminals, lead=12)
    p.lines(552, review_end + 20, ["Switch terminal marks vary. Find each pole with a meter:",
                       "both must be closed in ON and open in OFF."], size=7.2, lead=10, color=MUTE)


# ---------------------------------------------------------------- sheet 4, drive
@sheet(4, "Motor drivers and motors", "Design. Not built. Motor and driver models still to be confirmed (Section 2).", "A4")
def sheet_drive(p, W, H):
    for i, (ref, side, pwm, dirn, r1, r2, fb, jb, m1, m2) in enumerate((
            ("U4", "LEFT", "GPIO4", "GPIO5", "R1", "R2", "F5", "J3", "M1", "M2"),
            ("U5", "RIGHT", "GPIO6", "GPIO7", "R3", "R4", "F6", "J4", "M3", "M4"))):
        x, y = 8 + i * 396, 22
        p.text(x + 6, y - 4, f"{side} SIDE", size=9, bold=True)
        p.part(x + 104, y + 6, 100, 130, ref, "MD30C", [], size=7.5)
        p.tag(x + 56, y + 40, f"U1 {pwm}", side="l", color=YEL, bold=True)
        p.wire((x + 56, y + 40), (x + 104, y + 40), color=YEL, w=1.3)
        p.dot(x + 82, y + 40, color=YEL)
        p.wire((x + 82, y + 40), (x + 82, y + 48), color=YEL)
        p.res(x + 82, y + 58, "", vert=True)
        p.text(x + 76, y + 57, r1, size=7, bold=True, anchor="r")
        p.text(x + 76, y + 65, "10 kΩ", size=6.6, anchor="r")
        p.gnd(x + 82, y + 68, stub=1)
        p.text(x + 108, y + 43, "PWM", size=8, mono=True, bold=True)
        p.tag(x + 56, y + 90, f"U1 {dirn}", side="l", color=BLUE, bold=True)
        p.wire((x + 56, y + 90), (x + 104, y + 90), color=BLUE, w=1.3)
        p.dot(x + 82, y + 90, color=BLUE)
        p.wire((x + 82, y + 90), (x + 82, y + 96), color=BLUE)
        p.res(x + 82, y + 106, "", vert=True)
        p.text(x + 76, y + 105, r2, size=7, bold=True, anchor="r")
        p.text(x + 76, y + 113, "10 kΩ", size=6.6, anchor="r")
        p.gnd(x + 82, y + 116, stub=1)
        p.text(x + 108, y + 93, "DIR", size=8, mono=True, bold=True)
        p.wire((x + 104, y + 124), (x + 96, y + 124), color=BLACK)
        p.gnd(x + 96, y + 124, stub=1)
        p.text(x + 108, y + 127, "GND", size=8, mono=True, bold=True)
        p.text(x + 200, y + 34, "B+", size=8, mono=True, bold=True, anchor="r")
        p.text(x + 200, y + 56, "B−", size=8, mono=True, bold=True, anchor="r")
        p.text(x + 200, y + 86, "MA", size=8, mono=True, bold=True, anchor="r")
        p.text(x + 200, y + 131, "MB", size=8, mono=True, bold=True, anchor="r")
        p.wire((x + 204, y + 31), (x + 222, y + 31), w=1.6)
        p.tag(x + 222, y + 31, f"{jb} +, {fb}: 12.6 V", color=RED, size=7)
        p.wire((x + 204, y + 53), (x + 222, y + 53), color=BLACK, w=1.6)
        p.tag(x + 222, y + 53, f"{jb} −: STAR GND", color=BLACK, size=7)
        p.text(x + 214, y + 79, "18 AWG", size=6.4, color=MUTE)
        motor_pair(p, x + 204, y + 83, y + 128, x + 252, x + 330, y + 78, y + 110, m1, m2, 11)
    y = 196
    p.text(12, y, "Setup and pairing", size=9, bold=True)
    p.lines(12, y + 13, [
        "• Set the MD30C jumper or switch for external PWM (PWM + DIR, sign-magnitude). Check its manual.",
        "• The firmware drives 20 kHz PWM at 10 bits. MD30C logic high is 3 to 5.5 V, so 3.3 V from the S3 is fine.",
        "• The 10 kΩ pull-downs keep the driver inputs LOW while the S3 boots or resets. Solder them at the logic board.",
        "• Check each motor alone on a bench supply first, then wire both motors on a side to spin the same way for the same polarity.",
        "• If a whole side runs backwards, swap that side's MA and MB, or set MOTOR_LEFT_INVERT or MOTOR_RIGHT_INVERT in config_hardware.h.",
        "• MA and MB swap polarity, so they are neither fixed + nor GND. Use a pair that is not red or black, such as green and blue, and label them.",
        "• Route motor wires away from sonar, servo and camera-link wires (rule 3.1.5). Run motor returns to the star point, not through the logic board."],
        size=7.6, lead=11.4)
    tb = [["BTS7960 / IBT-2 pin (alternative driver)", "Left module", "Right module"],
          ["RPWM", "U1 GPIO4", "U1 GPIO6"], ["LPWM", "U1 GPIO5", "U1 GPIO7"],
          ["R_EN + L_EN, both modules together", "U1 GPIO15, each 10 kΩ to GND", "U1 GPIO15, each 10 kΩ to GND"],
          ["VCC (logic buffer)", "3.3 V, see note", "3.3 V, see note"], ["B+, B−", "F5, J3", "F6, J4"], ["M+, M−", "M1, M2 in parallel", "M3, M4 in parallel"]]
    end = p.grid(12, 296, [190, 120, 94], tb)
    p.note(428, 296, W - 440, [
        "To use BTS7960 modules, set MOTOR_DRIVER_TYPE to DRIVER_BTS7960 in",
        "src/s3/config_hardware.h and wire as in the table.",
        "IBT-2 inputs go through a 74HC244 buffer. At VCC = 5 V it needs ≥3.5 V for a",
        "HIGH, which 3.3 V GPIO does not guarantee. Power the module's VCC from 3.3 V",
        "(check on your module that VCC feeds only the buffer), or add a 74HCT245.",
        "",
        "Do not use the 10 A dual H-bridge from the team's notes with the baseline",
        "motors: one side can pull 11 A at stall."], title="Driver choice", size=7.3, lead=9.6)


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
@sheet(6, "ESP32-CAM, camera link and MG996R pan servo", "Link pins and servo signal tested on the bench, on a separate 5 V supply. Final power path not built.", "A4")
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
    p.part(sx, sy, 110, 92, "SV1", "MG996R", [], size=7.5)
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
    p.text(sx + 150, sy + 62, "C3 1000 µF", size=7, bold=True)
    p.wire((sx + 138, sy + 66), (sx + 138, sy + 82), color=BLACK, w=1.4)
    p.dot(sx + 138, sy + 66, color=BLACK)
    p.gnd(sx + 138, sy + 82, stub=1)
    p.text(sx + 150, sy + 86, "same GND as U2", size=7)
    p.note(470, 190, W - 482, ["• U7 sits on the motor bus, so the E-stop also stops the servo.",
                               "• U2 stays on from U6 and holds the pan during E-stop.",
                               "• Starts at 90°, keeps 45–135°, slews at 120°/s.",
                               "• Must be the 180° positional type. Team saw it hold position.",
                               "• If it jitters on a 3.3 V signal, add a 74AHCT125 buffer on 5 V.",
                               "• Check the camera ribbon cannot snag at either end of travel.",
                               "• Rigid bracket: the servo weighs 55 g."], title="Servo", fill=PANEL, size=7.4, lead=10.6)
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
    p.text(12, end + 16, "Bench result: link steady, 138 of 138 samples ok, 0 bad messages in 30 s. Servo sweep 45–135° smooth, no buzzing (team observation).", size=7.4)
    p.text(12, end + 28, "Camera was on the MB or fed from the DevKit VIN for these tests, not from U6. Repeat them once U6 is built.", size=7.4, color=MUTE)


# ---------------------------------------------------------------- sheet 7, sense and audio
@sheet(7, "Voltage sensing and optional audio board", "Dividers not fitted. Audio board runs, but microphone and speaker are not wired yet.", "A4")
def sheet_sense(p, W, H):
    p.text(12, 18, "VOLTAGE SENSE (optional, recommended)", size=9, bold=True)
    for i, (src, r1, r2, c1, dest, where) in enumerate((("BAT+ after F1", "R21", "R22", "C21", "U1 GPIO1", "before the contactor"),
                                                        ("MOTOR BUS", "R23", "R24", "C22", "U1 GPIO2", "after the contactor"))):
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
    p.lines(12, 128, ["12.6 V × 22 / 122 = 2.27 V at the ADC pin. Solder the divider at the logic board, then set BATTERY_SENSE_FITTED or MOTOR_BUS_SENSE_FITTED in",
                      "config_hardware.h, and calibrate against a multimeter with BATTERY_CAL_GAIN. With the battery divider fitted, the firmware caps PWM duty at 12.0 V / battery voltage.",
                      "Without it, duty is capped at a fixed 0.95. The bus divider lets the dashboard see that the contactor opened."], size=7.4, lead=10.4)
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
    p.part(60, by - 6, 170, st * 5 + 12, "U8", "INMP441 I²S microphone", [], size=7.2)
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
    for name, y in (("GPIO21", 125), ("GPIO10", 152), ("GPIO11", 176), ("GND", 212)):
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
    p.part(40, 214, 130, 72, "SV1", "MG996R", ["orange: IO2", "red, brown: separate 5 V", "supply. Supply − to U2 GND."], size=7)
    p.wire((170, 148), (190, 148), (190, 200), (20, 200), (20, 232), (40, 232), color=ORANGE, w=1.3)
    # jumper
    p.wire((430, 125), (486, 125), (486, 136), color=ORANGE, w=1.3)
    p.gnd(486, 136, stub=1)
    p.text(434, 121, "jumper to GND", size=7, color=ORANGE, bold=True)
    p.lines(442, 52, ["GPIO21 jumper stands in for the E-stop contact.",
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
            ["GPIO21 jumper to GND", "Tells the S3 'E-stop released' with no real switch", "SW1 pole B (Sheet 3)"],
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
        ["W01", "BT1 +", "J1 + then F1 in", "BAT+ 12.6 V", "red", "14 AWG", "F1 as close to the battery as possible"],
        ["W02", "F1 out", "K1 main contact", "BAT+ 12.6 V", "red", "14 AWG", ""],
        ["W03", "K1 main contact", "J2 + then bus point", "MOTOR BUS 0 / 12.6 V", "red", "14 AWG", ""],
        ["W04", "Bus point", "F5, J3 +, U4 B+", "MOTOR BUS", "red", "14 AWG", "Left driver"],
        ["W05", "Bus point", "F6, J4 +, U5 B+", "MOTOR BUS", "red", "14 AWG", "Right driver"],
        ["W06", "Bus point", "F8, J5 +, U7 IN+", "MOTOR BUS", "red", "18 AWG", "Servo buck"],
        ["W07", "F1 out", "F3, J6 +, U6 IN+", "BAT+ 12.6 V", "red", "18 AWG", "Logic buck. Not switched by K1"],
        ["W08", "After J6 +", "FAN1 +", "BAT+ 12.6 V", "red", "22 AWG", "Check fan current on its label"],
        ["W09", "U6 OUT+", "+5V LOGIC point", "+5V LOGIC", "red", "22 AWG", "Set U6 to 5.0 V before connecting"],
        ["W10", "+5V LOGIC", "U1 5V pin", "+5V LOGIC", "red", "22 AWG", "Unplug USB the first time"],
        ["W11", "+5V LOGIC", "U2 5V pin, C1, C2", "+5V LOGIC", "red", "22 AWG", "Short wires"],
        ["W12", "+5V LOGIC", "US1–US4 VCC, C11–C18", "+5V LOGIC", "red", "24 AWG", "One wire per sonar"],
        ["W13", "+5V LOGIC", "U3 5V pin, U9 VIN", "+5V LOGIC", "red", "22 AWG", "Optional audio"],
        ["W14", "U7 OUT+", "SV1 red, C3", "+5V SERVO", "red", "18 AWG", "Set U7 to 5.0 V before connecting"],
        ["W20", "BT1 −", "J1 −, J2 −, star point", "GND", "black", "14 AWG", ""],
        ["W21", "Star point", "J3 −, U4 B−", "GND", "black", "14 AWG", "Motor current return"],
        ["W22", "Star point", "J4 −, U5 B−", "GND", "black", "14 AWG", "Motor current return"],
        ["W23", "Star point", "J5 −, U7 IN−", "GND", "black", "18 AWG", ""],
        ["W24", "Star point", "J6 −, U6 IN−", "GND", "black", "18 AWG", ""],
        ["W25", "Star point", "K1 coil −, D1 anode", "GND", "black", "22 AWG", ""],
        ["W26", "Star point", "FAN1 −", "GND", "black", "22 AWG", ""],
        ["W27", "Star point", "Logic board GND", "GND", "black", "22 AWG", "No motor current through the logic board"],
        ["W28", "Logic GND", "U1, U2, US1–US4 GND", "GND", "black", "24 AWG", "Also R1–R4, R12–R18, R22, R24, C21, C22"],
        ["W29", "Logic GND", "U4 GND, U5 GND", "GND", "black", "24 AWG", "Driver logic ground"],
        ["W30", "U7 OUT−", "SV1 brown, C3, U2 GND", "GND", "black", "18 AWG", "The servo signal needs this common ground"],
        ["W31", "F1 out", "F4, SW2", "BAT+ 12.6 V", "red", "22 AWG", "ARM path"],
        ["W32", "SW2", "N3", "0 / 12.6 V", "red", "22 AWG", ""],
        ["W33", "Bus point", "F7, N3", "MOTOR BUS", "red", "22 AWG", "Hold path"],
        ["W34", "N3", "SW1 pole A, side 1", "0 / 12.6 V", "red", "22 AWG", ""],
        ["W35", "SW1 pole A, side 2", "K1 coil +, D1 cathode", "0 / 12.6 V", "red", "22 AWG", ""],
        ["W36", "U1 GPIO21", "SW1 pole B, side 1", "E-stop sense, 3.3 V", "orange", "24 AWG", "Open = treated as E-stop"],
        ["W37", "SW1 pole B, side 2", "Logic GND", "GND", "black", "24 AWG", ""],
        ["W40", "U1 GPIO4", "U4 PWM", "PWM 3.3 V, 20 kHz", "yellow", "24 AWG", "R1 10 kΩ to GND"],
        ["W41", "U1 GPIO5", "U4 DIR", "DIR 3.3 V", "blue", "24 AWG", "R2 10 kΩ to GND"],
        ["W42", "U1 GPIO6", "U5 PWM", "PWM 3.3 V, 20 kHz", "yellow", "24 AWG", "R3 10 kΩ to GND"],
        ["W43", "U1 GPIO7", "U5 DIR", "DIR 3.3 V", "blue", "24 AWG", "R4 10 kΩ to GND"],
        ["W44", "U4 MA", "M1, M2 first lead", "Motor, swaps polarity", "green", "18 AWG", "Not red or black"],
        ["W45", "U4 MB", "M1, M2 second lead", "Motor, swaps polarity", "blue", "18 AWG", "Not red or black"],
        ["W46", "U5 MA", "M3, M4 first lead", "Motor, swaps polarity", "green", "18 AWG", "Not red or black"],
        ["W47", "U5 MB", "M3, M4 second lead", "Motor, swaps polarity", "blue", "18 AWG", "Not red or black"],
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
        ["W70", "BAT+", "R21, then U1 GPIO1", "2.27 V after divider", "red, then teal", "26 AWG", "R22, C21 to GND. Optional"],
        ["W71", "MOTOR BUS", "R23, then U1 GPIO2", "2.27 V after divider", "red, then teal", "26 AWG", "R24, C22 to GND. Optional"],
    ]


def bom_rows():
    return [
        ["Ref", "Part", "Qty", "Label", "Notes"],
        ["M1–M4", "12 V gearmotor, Pololu #4744 (37D × 54L mm, 70:1, no encoder)", "4", f"{R} baseline. Four motors {C}. Team notes list 25GA 300 rpm: {U} rating", "150 RPM, 0.2 A no-load, 5.5 A stall at 12 V. #4754 is the encoder version, needed if traverse must finish as COMPLETE."],
        ["U4, U5", "Cytron MD30C, single channel, PWM + DIR", "2", f"{R}. Two drivers {C}, model {U}", "5–30 V, 30 A continuous, 80 A for ≤1 s, PWM ≤20 kHz."],
        ["(alt)", "BTS7960 / IBT-2", "2", "alternative from team notes", "Firmware supports it. See Sheet 4."],
        ["BT1", "3S Li-ion pack with BMS, XT60 lead", "1", R, "11.1 V nominal, 12.6 V full. BMS ≥25 A continuous. For example 3S2P high-drain 18650, about 5 Ah."],
        ["—", "3S Li-ion CC/CV charger, 12.6 V, 1–2 A", "1", R, "Never a generic 12 V adapter."],
        ["—", "Fire-resistant battery bag", "1", R, "Rule 3.3.2: carry and charge in it."],
        ["SW1", "2-pole ON-OFF switch (DPST), latching, labelled E-STOP, red if you can get it", "1", 'team decision, 6 Nov 2026. <font color="#B03A2E"><b>Does not meet rule 1.2.1</b></font>', "Contacts DC-rated above the K1 coil current at ≥14 V DC. The single-pole KCD11 from the notes cannot both cut the coil and tell the S3."],
        ["K1", "DC contactor, 12 V coil, contacts ≥40 A at ≥14 V DC", "1", R, "Check the DC rating, not AC. An auxiliary NO contact is a plus (Sheet 3)."],
        ["SW2", "Momentary NO push button, green, ARM", "1", R, "The deliberate restart (rule 1.2.4)."],
        ["D1", "1N4007 across K1 coil", "1", R, "Cathode to coil +. Skip if K1 has a built-in diode."],
        ["F1–F8", "Inline blade fuse holders and slow-blow fuses: 25 A, 15 A × 2, 3 A × 2, 1 A × 2", "7", R, "ATO/ATC. F2 is not used."],
        ["J1, J2", "XT60 pairs", "2", R, "J2 is new compared with docs/03 first version: rule 3.2.1."],
        ["J3–J6", "XT30 pairs", "4", R, "Driver branches and buck inputs."],
        ["U6", "5 V buck, ≥3 A continuous, 6–24 V in (logic)", "1", R, "S3, ESP32-CAM, sonars, optional audio."],
        ["U7", "5 V buck, ≥3 A continuous (servo)", "1", R, "On the motor bus. MG996R only."],
        ["FAN1", "12 V DC 4-inch case fan", "1", f"team notes. Current {U}", "On the F3 branch. Read its label."],
        ["U1", "ESP32-S3-N16R8 board, DevKitC-1 header (marked ESP32 23 2022 V1.3)", "1", f"{C} owned", "Runs today on the bench."],
        ["U2", "ESP32-CAM with GC2145 camera (RHYX M21-45, PID 0x2145)", "1", f"{C} owned", "AI-Thinker layout, confirmed by a working camera init."],
        ["—", "ESP32-CAM-MB flashing board (FT232R)", "1", f"{C} owned", "No auto-reset: IO0 + RST to flash. Not on the rover."],
        ["US1–US4", "HC-SR04 ultrasonic sensor", "4", f"1 {C} owned, 3 to buy", "Team notes say 2. Any subset works; rear enables reverse."],
        ["SV1", "MG996R servo, 180° positional + rigid pan bracket", "1", f"{C} owned", "Team saw it move and hold."],
        ["R1–R4", "10 kΩ pull-down at driver inputs", "4", R, "Two more for R_EN and L_EN with BTS7960."],
        ["R11, 13, 15, 17", "1 kΩ, Echo series", "4", R, ""],
        ["R12, 14, 16, 18", "1.8 kΩ, Echo to GND", "4", R, "2 kΩ is also fine (3.33 V)."],
        ["R21, R23", "100 kΩ, voltage sense top", "2", f"{R}, optional", ""],
        ["R22, R24", "22 kΩ, voltage sense bottom", "2", f"{R}, optional", ""],
        ["C1", "470 µF 16 V electrolytic at U2 5V", "1", R, "ESP32-CAM brownouts are common."],
        ["C2, C15–C18, C21, C22", "100 nF ceramic", "7", R, "U2 5V, each sonar, each ADC pin."],
        ["C3", "1000 µF 16 V electrolytic at the servo", "1", R, "Servo current spikes."],
        ["C11–C14", "100 µF 16 V electrolytic at each sonar", "4", f"{R}, new, not tested", "Against the stray readings seen on the bench."],
        ["—", "Wire: 14, 18, 22, 24 and 26 AWG", "—", R, "Red and black for power, other colours for signals (rule 3.2.3)."],
        ["—", "Veroboard or perfboard, female headers, JST-XH or crimp connectors, heat shrink, cable ties", "—", R, "Rule 3.1: no breadboard anywhere on the rover."],
        ["U3", "ESP32 DevKit (D0WD-V3, 26 MHz crystal, CP2102)", "1", f"{C} owned, optional", "Audio only."],
        ["U8", "INMP441 I²S microphone", "1", "team notes, provisional", "Optional."],
        ["U9", "MAX98357A I²S amplifier", "1", "team notes, provisional", "Optional."],
        ["LS1", "4 Ω 3 W speaker", "1", "team notes", "Optional."],
    ]


def test_rows():
    blank = ""
    return [
        ["#", "Test", "How", "Expected", "Measured", "OK"],
        ["T1", "No short on the power rails", "Battery off. Meter between BAT+ and GND, then bus and GND, then 5 V rails and GND.", "No continuity beep", blank, "☐"],
        ["T2", "Battery voltage at F1 out", "All other fuses out. Plug in the battery.", "9.6–12.6 V", blank, "☐"],
        ["T3", "Set U6", "F3 in, nothing on the U6 output. Adjust the output.", "4.9–5.1 V", blank, "☐"],
        ["T4", "Echo divider", "5 V on the sonar side of each divider. Meter on the GPIO side, board not plugged in.", "3.2 V (3.33 V with 2 kΩ)", blank, "☐"],
        ["T5", "S3 from U6", "USB unplugged. Plug in U1 only.", "S3 boots, Wi-Fi joins, no restarts", blank, "☐"],
        ["T6", "Camera from U6", "Plug in U2 with C1, C2. Watch the dashboard for 60 s.", "Link ok, no restarts", blank, "☐"],
        ["T7", "Power on, E-stop switch ON", "All fuses in, no motors, no servo. Battery on.", "K1 open, bus 0 V", blank, "☐"],
        ["T8", "ARM", "Press and release ARM.", "K1 closes and stays closed, bus = battery", blank, "☐"],
        ["T9", "E-stop", "Switch E-stop OFF.", "K1 opens, bus 0 V, dashboard shows ESTOP", blank, "☐"],
        ["T10", "Release does not restart", "Switch it back ON after a few seconds, then again about 1 s after switching OFF. Repeat after T17 with wheels raised and spinning.", "K1 stays open, bus stays 0 V", blank, "☐"],
        ["T11", "Sense wire broken", "Unplug the GPIO21 wire.", "S3 latches ESTOP", blank, "☐"],
        ["T12", "Set U7", "ARM on, servo not plugged in. Adjust the U7 output.", "4.9–5.1 V", blank, "☐"],
        ["T13", "Servo from U7", "Plug in SV1 with C3. Move the pan slider. Switch E-stop OFF while it moves.", "Moves 45–135°; stops when the bus drops", blank, "☐"],
        ["T14", "Driver inputs low at reset", "Meter on GPIO4–7 at the driver side while pressing RST on the S3. Motors off.", "About 0 V", blank, "☐"],
        ["T15", "Sonar accuracy on 5 V", "Ruler, flat object fixed in place, sonar fixed. 10, 20, 50 cm. Hands away. Close spare dashboard tabs.", "Note the error. Window at 100 mm is ±20 mm", blank, "☐"],
        ["T16", "Voltage sense", "Compare the dashboard to a meter, then set BATTERY_CAL_GAIN.", "Matches the meter after calibration", blank, "☐"],
        ["T17", "Each motor alone", "Wheels raised. One motor at a time on low duty.", "Same direction per side", blank, "☐"],
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
                      para("Build from this document, but know what it is. The power side (battery, fuses, contactor, E-stop, buck converters, motor drivers, motors) has <b>not been built or tested</b>. Section 10 lists exactly what has run on real hardware.", SMALL)], a4w))
    s.append(Spacer(1, 8))
    s.append(para("Before you buy or solder", H2))
    s += bullets(["<b>Read the motor label.</b> Fuses, driver and wire sizes assume Pololu #4744 (5.5 A stall). The team's notes list 25GA 300 rpm with an unknown rating.",
                  "<b>Confirm the driver.</b> The 10 A dual H-bridge in the notes is too small for the baseline motors. Use MD30C or BTS7960.",
                  "<b>Never connect the 5 V sonar Echo directly to an S3 pin.</b> Use the divider on Sheet 5.",
                  "<b>Never power the servo</b> from a board's 5 V pin or from the logic buck.",
                  "<b>E-stop: the team chose a 2-pole switch</b> instead of the red mushroom-head button rule 1.2.1 asks for. Get the organisers' approval in writing before you build it. An inspector may refuse it, and a rover that fails the E-stop test may not run (rule 1.2.3).",
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
                    ["BT1", "battery", "U1", "ESP32-S3 main controller (only motor authority)"],
                    ["F1, F3–F8", "fuses (F2 not used)", "U2", "ESP32-CAM: video, QR, motion, servo"],
                    ["J1–J6", "XT power connectors", "U3", "ESP32 DevKit: audio, optional"],
                    ["K1", "contactor", "U4, U5", "motor drivers, left and right"],
                    ["SW1", "E-stop switch", "U6, U7", "5 V buck converters, logic and servo"],
                    ["SW2", "ARM button", "U8, U9, LS1", "microphone, amplifier, speaker, optional"],
                    ["D1", "coil flyback diode", "US1–US4", "sonars (US, so they are not confused with the S3)"],
                    ["M1–M4", "motors", "SV1", "MG996R pan servo"],
                    ["R, C", "resistors, capacitors", "N3", "junction of ARM, hold path and E-stop pole A"]], [58, 150, 58, a4w - 266]))
    s.append(para("How the parts fit together", H2))
    s += bullets(["<b>S3 (U1)</b> is the only board that can command the motors. It runs missions, the sonars and the dashboard.",
                  "<b>ESP32-CAM (U2)</b> handles video, QR, motion and the pan servo. It talks to the S3 over three wires, not Wi-Fi: S3 TX, S3 RX and GND.",
                  "<b>DevKit (U3)</b> handles audio and talks over Wi-Fi only. It is optional.",
                  "<b>The E-stop works without any of them.</b> SW1 cuts the K1 coil directly, so motors and servo lose power even if every board has crashed."])
    # ---- section 2
    s.append(Heading("2. Decisions needed before buying", H1))
    s.append(table([["Item", "Assumed here", "Why it matters", "How to settle it", "Decided"],
                    ["Motors", "Pololu #4744, 5.5 A stall", "Sets every fuse, driver and wire size", "Read the label or datasheet of the motors you will really fit: voltage, no-load and stall current", ""],
                    ["Motor drivers", "2 × MD30C", "10 A dual H-bridge is below the 11 A per-side stall", "Name the part you own. For BTS7960, change MOTOR_DRIVER_TYPE", ""],
                    ["Battery", "3S Li-ion, BMS ≥25 A", "Stall worst case is 22 A", "Choose and buy with its charger and bag", ""],
                    ["Contactor K1", "12 V coil, ≥40 A DC", "Coil current sets F4, F7 and the E-stop contact rating", "Read its datasheet. Prefer one with an auxiliary NO contact (Sheet 3)", ""],
                    ["Fan", "12 V 4-inch, on F3", "Its current adds to the 3 A branch", "Read the label", ""],
                    ["Sonar count", "4 planned, 1 owned", "Reverse is blocked until the rear sonar is fitted", "Buy at least the rear one next", ""],
                    ["E-stop SW1", "2-pole ON-OFF switch", "Rule 1.2.1 asks for a red mushroom-head button; a rover that fails the E-stop test may not run", "Ask the organisers in writing", "Switch, team decision 6 Nov 2026"]],
                   [62, 96, 118, 150, a4w - 426]))
    # ---- section 3
    s.append(PageBreak())
    s.append(Heading("3. Rulebook checklist", H1))
    s.append(table([["Rule", "Requires", "This design", "Where"],
                    ["1.2.1", "Visible red mushroom E-stop, reachable from outside without crossing moving parts", '<font color="#B03A2E"><b>NOT MET</b></font> by team decision: SW1 is a 2-pole switch. Get the organisers\' approval in writing', "Sheet 3"],
                    ["1.2.2", "Removes power from all actuators, in hardware, without software or wireless", "SW1 cuts the K1 coil. Motors and servo are on the bus", "Sheet 3"],
                    ["1.2.3", "Tested at inspection", "Tests T9 and T18", "Section 9"],
                    ["1.2.4", "Release must not restart motion", "K1 latch needs ARM. Check the re-latch risk", "Sheet 3, T10"],
                    ["3.1.1–3", "Soldered veroboard or PCB, no breadboards, modules in soldered female headers, other joints soldered or crimped", "BOM and build list", "Section 7, 8"],
                    ["3.1.4", "Insulated, tied down, strain relief, no exposed conductors", "Build list", "Section 8"],
                    ["3.1.5", "Suitable wire gauge, motor wires apart from signals", "Wire schedule gives each size", "Section 5"],
                    ["3.2.1–2", "XT or Deans on every power link, polarised battery connector", "J1–J6", "Sheet 2"],
                    ["3.2.3", "Red positive with voltage labelled at each junction, black GND, other colours for signals", "Colours in the wire schedule", "Sheet 2, Section 5"],
                    ["3.3.1–3", "≤24 V nominal, fused, secured, fire-resistant bag, no damaged packs", "3S, F1, bag in the BOM", "Sheet 2"],
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
                    ["1", "PIN_BATT_ADC", "in, ADC1", "R21, R22, C21", "optional", "no"],
                    ["2", "PIN_MOTOR_BUS_ADC", "in, ADC1", "R23, R24, C22", "optional", "no"],
                    ["4", "PIN_LEFT_A", "out, PWM", "U4 PWM (BTS7960 RPWM)", "R1 to GND", "no"],
                    ["5", "PIN_LEFT_B", "out", "U4 DIR (BTS7960 LPWM)", "R2 to GND", "no"],
                    ["6", "PIN_RIGHT_A", "out, PWM", "U5 PWM (BTS7960 RPWM)", "R3 to GND", "no"],
                    ["7", "PIN_RIGHT_B", "out", "U5 DIR (BTS7960 LPWM)", "R4 to GND", "no"],
                    ["10", "PIN_SONAR_TRIG[0]", "out", "US1 Trig", "front centre", t_yes],
                    ["11", "PIN_SONAR_ECHO[0]", "in", "US1 Echo via R11, R12", "internal pull-down on", t_yes + ", 3.3 V"],
                    ["12", "PIN_SONAR_TRIG[1]", "out", "US2 Trig", "front left", "no"],
                    ["13", "PIN_SONAR_ECHO[1]", "in", "US2 Echo via R13, R14", "", "no"],
                    ["14", "PIN_SONAR_TRIG[2]", "out", "US3 Trig", "front right", "no"],
                    ["15", "PIN_DRIVER_EN", "out", "BTS7960 R_EN, L_EN", "BTS7960 only", "no"],
                    ["16", "PIN_SONAR_ECHO[2]", "in", "US3 Echo via R15, R16", "", "no"],
                    ["17", "PIN_SONAR_TRIG[3]", "out", "US4 Trig", "rear", "no"],
                    ["18", "PIN_SONAR_ECHO[3]", "in", "US4 Echo via R17, R18", "", "no"],
                    ["21", "PIN_ESTOP_SENSE", "in, pull-up", "SW1 pole B to GND", "LOW = switch ON", t_yes + ", jumper"],
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
    s.append(para("3 × HC-SR04 · rigid pan bracket · 2 × MD30C (or confirm owned drivers) · 4 × Pololu #4744 (or #4754 with encoders, or confirm the 25GA rating) · 2-pole ON-OFF E-stop switch, DC-rated above the K1 coil current · 12 V-coil contactor ≥40 A DC, ideally with an auxiliary NO contact · green NO push button · fuse holders and 25, 15, 15, 3, 3, 1, 1 A blade fuses · XT60 × 2 pairs, XT30 × 4 pairs · 2 × 5 V buck converters ≥3 A · 3S Li-ion pack with ≥25 A BMS, 12.6 V charger, fire-resistant bag · resistors: 1 k × 4, 1.8 k × 4, 10 k × 6, 100 k × 2, 22 k × 2 · capacitors: 470 µF × 1, 1000 µF × 1, 100 µF × 4, 100 nF × 7 · 1N4007 × 1 · wire · veroboard, headers, crimp connectors. Optional audio: INMP441, MAX98357A, 4 Ω 3 W speaker.", SMALL))
    s.append(callout(para("<b>Buy first:</b> the resistors and capacitors for the sonars. They are cheap, and the sonar work is waiting on them.", SMALL), lw))
    # ---- section 8
    s.append(NextPageTemplate("PortA4"))
    s.append(PageBreak())
    s.append(Heading("8. Calculations and build list", H1))
    s.append(para("5 V logic budget (U6, 3 A buck)", H2))
    s.append(table([["Load", "Peak at 5 V"], ["U1 ESP32-S3, Wi-Fi peak", "0.35 A"], ["U2 ESP32-CAM", "0.5 A"], ["US1–US4", "0.06 A"],
                    ["U3 audio board", "0.3 A"], ["U9 amplifier, full volume", "0.7 A"], ["<b>Total</b>", "<b>1.9 A, leaving about 1 A margin</b>"]], [260, a4w - 260]))
    s.append(para("Battery current, worst case", H2))
    s.append(table([["Branch", "Working", "At the battery"],
                    ["Both sides stalled", "4 motors × 5.5 A", "22 A"],
                    ["Logic, through U6", "1.9 A × 5 V / 0.85 efficiency (assumed) / 11.1 V", "1.0 A"],
                    ["Servo stalled, through U7", "2.5 A × 5 V / 0.85 efficiency (assumed) / 11.1 V", "1.3 A"],
                    ["<b>All at once</b>", "plus fan and K1 coil, both unknown", "<b>about 24.3 A + fan + coil</b>"]], [130, 240, a4w - 370]))
    s.append(Spacer(1, 4))
    s.append(para("That sits right at F1 (25 A), which is the job of F1: a full stall of everything should blow it rather than damage the wiring. Typical driving is about 2–6 A.", SMALL))
    s.append(para("Dividers", H2))
    s.append(table([["Divider", "Working", "Result"],
                    ["Sonar Echo, 1 k / 1.8 k", "5 V × 1.8 / (1 + 1.8)", "3.21 V"],
                    ["Sonar Echo, 1 k / 2 k", "5 V × 2 / (1 + 2)", "3.33 V"],
                    ["Battery, 100 k / 22 k, full", "12.6 V × 22 / 122", "2.27 V"],
                    ["Battery, 100 k / 22 k, empty", "9.6 V × 22 / 122", "1.73 V"]], [170, 200, a4w - 370]))
    s.append(para("Motor voltage", H2))
    s.append(para("A full 3S pack is 12.6 V, 5 % above a 12 V motor rating. The firmware caps PWM duty at 12.0 V / battery voltage once the battery divider is fitted and calibrated, and at a fixed 0.95 until then.", SMALL))
    s.append(para("Build list", H2))
    tick = ParagraphStyle("Tick", parent=SMALL, leftIndent=2, spaceAfter=2)
    s += [para(f"☐  {x}", tick) for x in ["Every module on soldered female headers on veroboard or PCB. No breadboard anywhere.",
                                     "Every power joint uses XT30, XT60 or Deans. No bare wire, clips or tape.",
                                     "Red = positive with the voltage labelled at each junction. Black = GND. Other colours for signals.",
                                     "Heat shrink on every joint, cable ties, strain relief at every connector.",
                                     "Motor wires routed away from sonar, servo and camera-link wires.",
                                     "E-stop rigidly mounted and reachable from outside without crossing moving parts.",
                                     "Battery fused at the battery, secured against vibration, in a fire-resistant bag when charging.",
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
                    ["E-stop sense: GPIO21", "Open: latches ESTOP at boot. Jumper to GND: released. No real switch fitted"],
                    ["MG996R on IO2, separate 5 V supply", "Moves and holds. Sweep 45–135° smooth, no buzzing (team observation)"],
                    ["HC-SR04 on GPIO10, 11, on 3.3 V", "Works, but 25 mm long at 100 mm, and strays at 1.8 m (Sheet 5)"],
                    ["AUTO QR mission", "COMPLETE 4 of 4 in 2.1–4.6 s, QR held in view, camera not on the servo"],
                    ["Heavy web traffic", "Crash found and fixed: ran 120 s under the same load, no restart"]], [210, a4w - 210]))
    s.append(para("Not tested at all", H2))
    s.append(para("Battery, BMS, fuses, XT connectors · contactor and E-stop circuit · both buck converters · S3 and camera fed from U6 · motor drivers and motors · sonars on 5 V with the divider and capacitors · sonars US2 to US4 · voltage dividers · microphone and speaker · camera mounted on the servo for the tracking task.", SMALL))
    s.append(para("Open problems", H2))
    s.append(table([["Problem", "Status", "Next step"],
                    ["S3 restarted once by itself (reset reason int_wdt), idle, on earlier firmware", "Cause unknown. Wiggling sonar wires did not bring it back. About 20 minutes of serial logging on the fixed firmware showed no restart", "Keep the serial log on during first motor tests"],
                    ["Sonar reads 25 mm long at 100 mm on 3.3 V", "Fails the ±20 mm window", "Build Sheet 5 on 5 V, then test T15"],
                    ["Sonar strays at long range", "Suspected supply noise, not proven", "Capacitors, 5 V, then a firmware filter if still needed"],
                    ["Possible K1 re-latch on quick release", "Design concern, not seen, not built", "Test T10, then the fix on Sheet 3"],
                    ["S3 5V pin as a supply input", "Not tested. It did not feed the camera as an output", "Test T5 with USB unplugged"],
                    ["E-stop is a switch, not the mushroom-head button rule 1.2.1 asks for", "Team decision, 6 Nov 2026", "Organisers' approval in writing before inspection"],
                    ["Dashboard access is not protected yet", "Software team", "Fix before motors are connected"]], [190, 190, a4w - 380]))
    s.append(para("Revision history", H2))
    s.append(table([["Rev", "Date", "Change"], ["A", "2 Nov 2026", "First issue. Adds J2, sonar capacitors C11–C18 and the K1 re-latch check to the design in docs/03_electrical.md."],
                    ["B", "6 Nov 2026", "E-stop SW1 changed from the red mushroom button to a 2-pole ON-OFF switch, by team decision. Rule 1.2.1 marked NOT MET."]], [34, 70, a4w - 104]))
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
