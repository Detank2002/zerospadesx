"""Draws the VR controls cheat sheet: one slide per controller family, and
one for the body (holsters, wrist), into Resources/Gfx/VR/Controls.

The game shows them from the in-game menu's Controls button (VRControlsWindow);
VR-CONTROLS.md shows them too. Run again after a control changes:

    py tools/vr-controls/make_controls.py

Needs Pillow. The drawings are simple shapes, not pictures of the real
controllers: enough to find a button by where it sits. Right-handed (the
default); the slides say that vr_leftHanded swaps the hands.
"""

import math
import os
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OUT = os.path.join(ROOT, "Resources", "Gfx", "VR", "Controls")
FONT = os.path.join(ROOT, "Resources", "Gfx", "Fonts", "OpenSans.ttf")

W, H = 1600, 900  # the slide; drawn at SS times that and filtered down
SS = 2

BG = (21, 24, 30)
PANEL = (32, 37, 46)
BODY = (58, 64, 76)
BODY_EDGE = (120, 128, 142)
PART = (34, 38, 46)
PART_EDGE = (150, 158, 172)
TEXT = (236, 238, 242)
DIM = (160, 166, 178)

# What each kind of control is drawn in, on the controller and on its label.
COLOURS = {
    "trigger": (232, 84, 72),
    "grip": (88, 196, 112),
    "stick": (84, 160, 240),
    "button": (244, 178, 60),
    "menu": (196, 132, 236),
    "unused": (110, 116, 128),
}


def font(px):
    return ImageFont.truetype(FONT, px * SS)


def S(v):
    return v * SS


class Slide:
    def __init__(self, title, subtitle):
        self.img = Image.new("RGB", (S(W), S(H)), BG)
        self.d = ImageDraw.Draw(self.img)
        self.text((48, 30), title, 46, TEXT, bold=True)
        self.text((50, 92), subtitle, 24, DIM)

    def text(self, xy, s, px, fill, bold=False, anchor="la"):
        self.d.text((S(xy[0]), S(xy[1])), s, font=font(px), fill=fill, anchor=anchor,
                    stroke_width=(1 if bold else 0) * SS // 1, stroke_fill=fill)

    def text_width(self, s, px, bold=False):
        return self.d.textlength(s, font=font(px)) / SS + (2 if bold else 0)

    def wrap(self, s, px, width):
        """Words into lines no wider than `width`."""
        lines, line = [], ""
        for word in s.split(" "):
            t = word if not line else line + " " + word
            if self.text_width(t, px) <= width:
                line = t
            else:
                if line:
                    lines.append(line)
                line = word
        if line:
            lines.append(line)
        return lines

    def ellipse(self, cx, cy, rx, ry, fill=None, outline=None, width=0):
        self.d.ellipse([S(cx - rx), S(cy - ry), S(cx + rx), S(cy + ry)], fill=fill,
                       outline=outline, width=S(width))

    def rrect(self, x0, y0, x1, y1, r, fill=None, outline=None, width=0):
        self.d.rounded_rectangle([S(x0), S(y0), S(x1), S(y1)], radius=S(r), fill=fill,
                                 outline=outline, width=S(width))

    def poly(self, pts, fill=None, outline=None, width=0):
        self.d.polygon([(S(x), S(y)) for x, y in pts], fill=fill)
        if outline:
            self.d.line([(S(x), S(y)) for x, y in pts + [pts[0]]], fill=outline,
                        width=S(width), joint="curve")

    def line(self, pts, fill, width):
        self.d.line([(S(x), S(y)) for x, y in pts], fill=fill, width=S(width), joint="curve")

    def arc(self, cx, cy, rx, ry, a0, a1, fill, width):
        self.d.arc([S(cx - rx), S(cy - ry), S(cx + rx), S(cy + ry)], a0, a1, fill=fill,
                   width=S(width))

    def label_column(self, items, x, top, bottom, side, width):
        """Labels stacked down a column beside a controller, each joined to its
        control by a line ending in a ring round it. items: (point, kind, name,
        action). `side`: -1 the column is left of the controller, 1 right of it."""
        items = sorted(items, key=lambda it: it[0][1])
        bodies, heights = [], []
        for _, kind, name, action in items:
            body = self.wrap(action, 19, width - 26)
            bodies.append(body)
            heights.append(30 + 24 * len(body) + 12)
        gap = (bottom - top - sum(heights)) / max(1, len(items) - 1)
        gap = max(6, min(gap, 26))
        y = top + max(0, (bottom - top - sum(heights) - gap * (len(items) - 1)) / 2)
        for (pt, kind, name, action), body, h in zip(items, bodies, heights):
            c = COLOURS[kind]
            x0, x1 = x, x + width
            self.rrect(x0, y, x1, y + h - 4, 10, fill=PANEL)
            if side > 0:
                self.rrect(x0, y, x0 + 7, y + h - 4, 3, fill=c)
            else:
                self.rrect(x1 - 7, y, x1, y + h - 4, 3, fill=c)
            tx = x0 + 18 if side > 0 else x0 + 12
            self.text((tx, y + 4), name, 22, c, bold=True)
            for i, ln in enumerate(body):
                self.text((tx, y + 34 + 24 * i), ln, 19, TEXT)
            # The line, from the label's edge facing the controller, to a ring
            # round the control (a dot would hide a button's letter).
            ax = x0 if side > 0 else x1
            ay = y + 18
            mx = ax - 22 * side
            ring = 15
            dx, dy = pt[0] - mx, pt[1] - ay
            n = max(1e-3, math.hypot(dx, dy))
            end = (pt[0] - dx / n * ring, pt[1] - dy / n * ring)
            self.line([(ax, ay), (mx, ay), end], c, 3)
            self.ellipse(pt[0], pt[1], ring, ring, outline=c, width=3)
            y += h + gap

    def footer(self, lines):
        top = H - 30 * len(lines) - 24
        self.rrect(30, top - 10, W - 30, H - 18, 12, fill=PANEL)
        for i, (head, s) in enumerate(lines):
            x = 50
            if head:
                self.text((x, top + 30 * i), head, 20, COLOURS["button"], bold=True)
                x += self.text_width(head, 20, bold=True) + 10
            self.text((x, top + 30 * i), s, 20, TEXT)

    def save(self, name):
        os.makedirs(OUT, exist_ok=True)
        out = self.img.resize((W, H), Image.LANCZOS)
        # 256 colours: a third of the size (they ship in the paks, the Quest's
        # APK included), and nothing to see between the two.
        out = out.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
        out.save(os.path.join(OUT, name), optimize=True)
        print("wrote", os.path.relpath(os.path.join(OUT, name), ROOT))


# --- the controllers -------------------------------------------------------
# Each draws one controller face-on at (cx, cy) for `hand` (-1 left, 1 right)
# and returns the points its labels join to, by name.

def stick(s, x, y, r=30):
    s.ellipse(x, y, r + 6, r + 6, fill=PART, outline=PART_EDGE, width=2)
    s.ellipse(x, y, r - 6, r - 6, fill=(48, 54, 64), outline=PART_EDGE, width=2)


def button(s, x, y, r, letter):
    s.ellipse(x, y, r, r, fill=PART, outline=PART_EDGE, width=2)
    if letter:
        s.text((x, y + 1), letter, int(r * 1.1), TEXT, bold=True, anchor="mm")


def touch(s, cx, cy, hand, ring):
    """Meta Touch: Quest 2 and Rift S (`ring`: the tracking ring over the
    top), Quest 3 and 3S (Touch Plus: no ring)."""
    m = hand
    if ring:
        # The ring sweeps over the top, out past the thumb side.
        s.arc(cx - 18 * m, cy - 70, 150, 120, 150, 390, BODY_EDGE, 16)
        s.arc(cx - 18 * m, cy - 70, 150, 120, 150, 390, BODY, 10)
    # Handle, under the face, leaning in.
    s.poly([(cx - 52, cy + 40), (cx + 52, cy + 40), (cx + 40 + 14 * m, cy + 300),
            (cx - 40 + 14 * m, cy + 300)], fill=BODY, outline=BODY_EDGE, width=3)
    s.ellipse(cx + 14 * m, cy + 300, 40, 18, fill=BODY, outline=BODY_EDGE, width=3)
    # Face.
    rx, ry = (125, 92) if ring else (135, 108)
    s.ellipse(cx, cy, rx, ry, fill=BODY, outline=BODY_EDGE, width=3)
    # Trigger at the top front, under the index finger; grip on the handle's
    # inside, under the middle finger.
    s.rrect(cx - 30, cy - ry - 26, cx + 30, cy - ry + 4, 12, fill=COLOURS["trigger"])
    gx = cx - 46 * m
    s.poly([(gx - 10, cy + 70), (gx + 10, cy + 70), (gx + 10 + 6 * m, cy + 230),
            (gx - 10 + 6 * m, cy + 230)], fill=COLOURS["grip"])
    st = (cx - 52 * m, cy - 6)
    stick(s, *st)
    lo = (cx + 34 * m, cy + 30)
    hi = (cx + 74 * m, cy - 22)
    button(s, *lo, 20, "A" if m > 0 else "X")
    button(s, *hi, 20, "B" if m > 0 else "Y")
    sysb = (cx + 6 * m, cy + 64)
    s.ellipse(sysb[0], sysb[1], 11, 8, fill=PART, outline=PART_EDGE, width=2)
    if m < 0:
        for k in (-3, 0, 3):
            s.line([(sysb[0] - 5, sysb[1] + k), (sysb[0] + 5, sysb[1] + k)], TEXT, 1)
    return {"trigger": (cx, cy - ry - 12), "grip": (gx, cy + 150), "stick": st,
            "lo": lo, "hi": hi, "sys": sysb}


def index(s, cx, cy, hand):
    """Valve Index: a tall handle; the hand is strapped on, the grip is a
    squeeze of the whole handle."""
    m = hand
    s.rrect(cx - 50, cy - 170, cx + 50, cy + 270, 46, fill=BODY, outline=BODY_EDGE, width=3)
    # The strap, on the outside.
    s.rrect(cx + 52 * m - 7, cy - 120, cx + 52 * m + 7, cy + 220, 6, fill=(44, 48, 58))
    s.rrect(cx - 30, cy - 196, cx + 30, cy - 166, 12, fill=COLOURS["trigger"])
    s.rrect(cx - 54, cy + 40, cx + 54, cy + 230, 30, outline=COLOURS["grip"], width=6)
    st = (cx - 8 * m, cy - 118)
    stick(s, *st, r=24)
    a = (cx + 26 * m, cy - 52)
    b = (cx + 26 * m, cy - 84)
    button(s, *a, 13, "A")
    button(s, *b, 13, "B")
    s.rrect(cx - 24, cy - 40, cx + 14, cy + 20, 18, fill=PART, outline=COLOURS["unused"], width=2)
    sysb = (cx - 4, cy + 34)
    s.ellipse(sysb[0], sysb[1], 7, 7, fill=PART, outline=PART_EDGE, width=2)
    return {"trigger": (cx, cy - 182), "grip": (cx - 54 * m, cy + 150), "stick": st,
            "lo": a, "hi": b, "pad": (cx - 5, cy - 10)}


def vive(s, cx, cy, hand):
    """HTC Vive wand: a ring head over a long handle with a big trackpad."""
    m = hand
    s.ellipse(cx, cy - 175, 120, 100, outline=BODY_EDGE, width=18)
    s.ellipse(cx, cy - 175, 120, 100, outline=BODY, width=12)
    s.rrect(cx - 56, cy - 110, cx + 56, cy + 290, 50, fill=BODY, outline=BODY_EDGE, width=3)
    s.rrect(cx - 28, cy - 132, cx + 28, cy - 104, 12, fill=COLOURS["trigger"])
    pad = (cx, cy - 10)
    s.ellipse(pad[0], pad[1], 46, 46, fill=PART, outline=PART_EDGE, width=2)
    s.line([(cx - 30, cy - 10), (cx + 30, cy - 10)], (70, 76, 88), 2)
    s.line([(cx, cy - 40), (cx, cy + 20)], (70, 76, 88), 2)
    menu = (cx, cy - 76)
    s.rrect(menu[0] - 16, menu[1] - 7, menu[0] + 16, menu[1] + 7, 6, fill=PART,
            outline=PART_EDGE, width=2)
    sysb = (cx, cy + 58)
    s.ellipse(sysb[0], sysb[1], 9, 9, fill=PART, outline=PART_EDGE, width=2)
    for side in (-1, 1):
        s.rrect(cx + 56 * side - 8, cy + 90, cx + 56 * side + 8, cy + 150, 6,
                fill=COLOURS["grip"])
    return {"trigger": (cx, cy - 118), "grip": (cx - 62 * m, cy + 120), "pad": pad,
            "menu": menu}


def wmr(s, cx, cy, hand):
    """Windows Mixed Reality: a lit ring out past the thumb, a stick and a
    trackpad side by side."""
    m = hand
    s.ellipse(cx + 60 * m, cy - 120, 115, 105, outline=BODY_EDGE, width=16)
    s.ellipse(cx + 60 * m, cy - 120, 115, 105, outline=(80, 90, 110), width=10)
    s.poly([(cx - 70, cy - 70), (cx + 70, cy - 70), (cx + 46, cy + 290), (cx - 46, cy + 290)],
           fill=BODY, outline=BODY_EDGE, width=3)
    s.rrect(cx - 80, cy - 110, cx + 80, cy + 30, 40, fill=BODY, outline=BODY_EDGE, width=3)
    s.rrect(cx - 28, cy - 136, cx + 28, cy - 108, 12, fill=COLOURS["trigger"])
    st = (cx - 36 * m, cy - 50)
    stick(s, *st, r=22)
    pad = (cx + 30 * m, cy - 44)
    s.ellipse(pad[0], pad[1], 30, 30, fill=PART, outline=PART_EDGE, width=2)
    menu = (cx + 12 * m, cy + 10)
    s.rrect(menu[0] - 12, menu[1] - 6, menu[0] + 12, menu[1] + 6, 5, fill=PART,
            outline=PART_EDGE, width=2)
    sysb = (cx - 20 * m, cy + 10)
    s.ellipse(sysb[0], sysb[1], 8, 8, fill=PART, outline=PART_EDGE, width=2)
    gx = cx - 52 * m
    s.rrect(gx - 9, cy + 80, gx + 9, cy + 180, 6, fill=COLOURS["grip"])
    return {"trigger": (cx, cy - 122), "grip": (gx, cy + 130), "stick": st, "pad": pad,
            "menu": menu}


# --- what the controls do ---------------------------------------------------
# Kept with VR.md's Controls table and the client (Client::UpdateVRPlayerInput,
# OpenXRSystem::SuggestBindings).

L_TRIGGER = ("Holding the gun's barrel: brace it on your shoulder, while held. "
             "Otherwise: open or close the big map.")
L_GRIP = ("On the gun's barrel: hold it in both hands. Beside your head: headlamp. "
          "Anywhere else: scoreboard.")
R_TRIGGER = "Fire, hit with the spade, place a block. Grenade: pull the pin, let go to throw."
R_GRIP = ("At a holster: take that tool (below). Spade: dig a tunnel. "
          "Blocks: build a line.")
MENU_FOOT = ("Menu", "B and Y together (or the wrist menu's Menu).  In it: Controls (this "
             "sheet), Reset Height, Setup.")


def controller_slide(name, title, subtitle, draw, left, right, foot):
    s = Slide(title, subtitle + ".   Left-handed? Setup > VR > Left Handed swaps the hands.")
    lx, rx, cy = 620, 980, 400
    lp = draw(s, lx, cy, -1)
    rp = draw(s, rx, cy, 1)
    bottom = H - 30 * len(foot) - 50
    s.text((lx, bottom - 6), "LEFT HAND", 22, DIM, bold=True, anchor="mm")
    s.text((rx, bottom - 6), "RIGHT HAND (gun)", 22, DIM, bold=True, anchor="mm")
    s.label_column([(lp[k], kind, n, t) for k, kind, n, t in left], 30, 140, bottom, -1, 400)
    s.label_column([(rp[k], kind, n, t) for k, kind, n, t in right], W - 430, 140, bottom, 1,
                   400)
    s.footer(foot)
    s.save(name)


STANDARD_FOOT = [
    ("Switch weapon", "squeeze the right grip at a holster: right shoulder gun, left shoulder "
                      "spade, right hip blocks, left hip grenades."),
    ("Switch team", "look at your left wrist, bring your right hand to it: Team / weapon."),
    MENU_FOOT,
]


def touch_slide(name, title, subtitle, ring):
    left = [
        ("trigger", "trigger", "Trigger", L_TRIGGER),
        ("grip", "grip", "Grip", L_GRIP),
        ("stick", "stick", "Stick", "Walk, where you look. Click: sprint."),
        ("hi", "button", "Y", "Open or close the big map."),
        ("lo", "button", "X", "Copy the colour of the block you point at (holding blocks)."),
        ("sys", "menu", "Menu button", "The game's menu, where the headset lets it through."),
    ]
    right = [
        ("trigger", "trigger", "Trigger", R_TRIGGER),
        ("grip", "grip", "Grip", R_GRIP),
        ("stick", "stick", "Stick", "Left/right: turn. Click: crouch (or crouch for real)."),
        ("hi", "button", "B", "Reload."),
        ("lo", "button", "A", "Jump."),
    ]
    controller_slide(name, title, subtitle, lambda s, x, y, h: touch(s, x, y, h, ring), left,
                     right, STANDARD_FOOT)


def main():
    touch_slide("Quest2.png", "Meta Quest 2 controls", "Touch controllers (also Rift S)", True)
    touch_slide("Quest3.png", "Meta Quest 3 controls", "Touch Plus controllers (also Quest 3S)",
                False)

    controller_slide(
        "Index.png", "Valve Index controls", "Index controllers",
        index,
        [("trigger", "trigger", "Trigger", L_TRIGGER),
         ("grip", "grip", "Grip (squeeze)", L_GRIP),
         ("stick", "stick", "Stick", "Walk, where you look. Click: sprint."),
         ("hi", "button", "B", "Open or close the big map."),
         ("lo", "button", "A", "Copy the colour of the block you point at (holding blocks).")],
        [("trigger", "trigger", "Trigger", R_TRIGGER),
         ("grip", "grip", "Grip (squeeze)", R_GRIP),
         ("stick", "stick", "Stick", "Left/right: turn. Click: crouch (or crouch for real)."),
         ("hi", "button", "B", "Reload."),
         ("lo", "button", "A", "Jump."),
         ("pad", "unused", "Trackpad", "Not used.")],
        STANDARD_FOOT[:2] + [("Menu", "both B buttons together.  In it: Controls, Reset "
                                      "Height, Setup.")])

    controller_slide(
        "Vive.png", "HTC Vive controls", "Vive wands (also Vive Pro)", vive,
        [("trigger", "trigger", "Trigger", L_TRIGGER),
         ("grip", "grip", "Grip buttons", L_GRIP),
         ("pad", "stick", "Trackpad", "Touch to walk, where you look. Click: sprint."),
         ("menu", "menu", "Menu button", "The game's menu.")],
        [("trigger", "trigger", "Trigger", R_TRIGGER),
         ("grip", "grip", "Grip buttons", R_GRIP),
         ("pad", "stick", "Trackpad", "Touch left/right: turn. Click: jump."),
         ("menu", "button", "Menu button", "Reload.")],
        STANDARD_FOOT[:2] + [
            ("No crouch button", "crouch for real. No colour copy: colours are on the wrist "
                                 "menu."),
            ("Menu", "the left menu button.  In it: Controls, Reset Height, Setup.")])

    controller_slide(
        "WMR.png", "Windows Mixed Reality controls", "Motion controllers (HP Reverb, Samsung "
        "Odyssey and others)", wmr,
        [("trigger", "trigger", "Trigger", L_TRIGGER),
         ("grip", "grip", "Grip", L_GRIP),
         ("stick", "stick", "Stick", "Walk, where you look. Click: sprint."),
         ("menu", "menu", "Menu button", "The game's menu."),
         ("pad", "unused", "Trackpad", "Not used.")],
        [("trigger", "trigger", "Trigger", R_TRIGGER),
         ("grip", "grip", "Grip", R_GRIP),
         ("stick", "stick", "Stick", "Left/right: turn. Click: crouch (or crouch for real)."),
         ("pad", "button", "Trackpad click", "Jump."),
         ("menu", "button", "Menu button", "Reload.")],
        STANDARD_FOOT[:2] + [
            ("No colour copy", "colours are on the wrist menu."),
            ("Menu", "the left menu button.  In it: Controls, Reset Height, Setup.")])

    body_slide()


def body_slide():
    s = Slide("Your tools and the wrist menu", "Seen from behind, as you stand. Every headset.")
    cx, top = 800, 170
    # Body, from behind.
    s.ellipse(cx, top + 58, 52, 62, fill=BODY, outline=BODY_EDGE, width=3)
    s.rrect(cx - 34, top + 112, cx + 34, top + 150, 10, fill=BODY)
    s.poly([(cx - 150, top + 170), (cx + 150, top + 170), (cx + 110, top + 440),
            (cx - 110, top + 440)], fill=BODY, outline=BODY_EDGE, width=3)
    s.rrect(cx - 150, top + 140, cx + 150, top + 200, 30, fill=BODY, outline=BODY_EDGE, width=3)
    for side in (-1, 1):
        # Arms held out a little, clear of the hips; legs.
        s.poly([(cx + 140 * side, top + 160), (cx + 186 * side, top + 186),
                (cx + 330 * side, top + 350), (cx + 300 * side, top + 380)], fill=BODY,
               outline=BODY_EDGE, width=3)
        s.poly([(cx + 10 * side, top + 440), (cx + 108 * side, top + 440),
                (cx + 96 * side, top + 600), (cx + 28 * side, top + 600)], fill=BODY,
               outline=BODY_EDGE, width=3)
    # The watch on the left wrist (image left: seen from behind).
    watch = (cx - 300, top + 352)
    s.poly([(watch[0] - 30, watch[1] - 4), (watch[0] + 4, watch[1] - 24),
            (watch[0] + 26, watch[1] + 10), (watch[0] - 8, watch[1] + 30)],
           fill=COLOURS["menu"])
    holsters = {
        "gun": (cx + 130, top + 172), "spade": (cx - 130, top + 172),
        "blocks": (cx + 112, top + 420), "grenades": (cx - 112, top + 420),
    }
    for p in holsters.values():
        s.ellipse(p[0], p[1], 26, 26, outline=COLOURS["grip"], width=6)
    s.text((cx - 150, 150), "LEFT", 22, DIM, bold=True, anchor="mm")
    s.text((cx + 150, 150), "RIGHT", 22, DIM, bold=True, anchor="mm")

    bottom = H - 30 * 2 - 50
    s.label_column([
        (holsters["spade"], "grip", "Left shoulder: spade",
         "Reach over it with your right hand and squeeze the grip."),
        (holsters["grenades"], "grip", "Left hip: grenades",
         "Pull the trigger to pull the pin; throw, and let go."),
        (watch, "menu", "Left wrist: HUD and wrist menu",
         "Look at it like a watch: health, ammo, map. Bring your right hand to it for the "
         "menu: Chat, Light, Quick chat, Menu, Team / weapon, Block colours."),
    ], 30, 150, bottom, -1, 400)
    s.label_column([
        (holsters["gun"], "grip", "Right shoulder: gun",
         "Squeeze the right grip over your right shoulder."),
        (holsters["blocks"], "grip", "Right hip: blocks",
         "Pick their colour on the wrist menu."),
    ], W - 430, 150, bottom, 1, 400)
    s.footer([
        ("Crouched and can't stand?", "stand up straight, then Menu > Reset Height (or hold "
                                      "both sticks in for a second)."),
        ("Seated?", "Setup > VR > X And Y Switch Tools puts the tools on X and Y."),
    ])
    s.save("Body.png")


if __name__ == "__main__":
    sys.exit(main())
