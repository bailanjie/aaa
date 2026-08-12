"""Generate a minimalist 2D icon: blue rounded rectangle (terminal-window shape)
with a dark-blue thick border and two white parallel lines (one long, one short).
Pure stdlib — no dependencies beyond Python 3.x.
Output: res/app.ico with 16/24/32/48/256 sizes."""

import struct, math, os

# ── geometry (fractions of the square canvas) ─────────────────────────

RECT_HW = 0.42     # half-width  → total width 0.84
RECT_HH = 0.28     # half-height → total height 0.56 (3:2, terminal/screen ratio)
CORNER_R = 0.05    # small corner rounding
BORDER = 0.040     # border thickness (drawn inward from the outer edge)

# Two white lines, left-aligned, shorter than the rectangle width
LINE_H     = 0.040   # line thickness
LINE_LEFT  = 0.14    # left x of both lines
LINE1_LEN  = 0.46    # long line
LINE2_LEN  = 0.28    # short line
LINE1_CY   = 0.38    # vertical centre of long line
LINE2_CY   = 0.51    # vertical centre of short line

# ── colours ────────────────────────────────────────────────────────────

BLUE  = (37, 99, 235)     # body  #2563EB
DARK  = (30, 64, 175)     # border #1E40AF
WHITE = (255, 255, 255)   # lines


def _rounded_rect_sdf(px, py, cx, cy, hw, hh, r):
    """Signed distance to a rounded rectangle boundary. <0 inside, >0 outside."""
    qx = abs(px - cx) - (hw - r)
    qy = abs(py - cy) - (hh - r)
    ax, ay = max(qx, 0.0), max(qy, 0.0)
    return math.sqrt(ax * ax + ay * ay) + min(max(qx, qy), 0.0) - r


def make_bitmap(size):
    """Return size×size int bitmap: 0=transparent, 1=blue, 2=dark border, 3=white line."""
    bmp = [[0] * size for _ in range(size)]

    cx = cy = size / 2.0
    hw = RECT_HW * size
    hh = RECT_HH * size
    r = CORNER_R * size
    border = BORDER * size

    def _line_box(cy_norm, length_norm):
        x1 = LINE_LEFT * size
        x2 = (LINE_LEFT + length_norm) * size
        half = LINE_H * size / 2.0
        return x1, cy_norm * size - half, x2, cy_norm * size + half

    line1 = _line_box(LINE1_CY, LINE1_LEN)
    line2 = _line_box(LINE2_CY, LINE2_LEN)

    for y in range(size):
        for x in range(size):
            sdf = _rounded_rect_sdf(x, y, cx, cy, hw, hh, r)
            if sdf >= 0:
                continue
            if sdf > -border:
                bmp[y][x] = 2  # border
                continue
            # body
            if ((line1[0] <= x <= line1[2] and line1[1] <= y <= line1[3]) or
                (line2[0] <= x <= line2[2] and line2[1] <= y <= line2[3])):
                bmp[y][x] = 3  # white line
            else:
                bmp[y][x] = 1  # blue body
    return bmp


# ── ICO format ─────────────────────────────────────────────────────────

_COLORS = {1: BLUE, 2: DARK, 3: WHITE}


def bmp_to_ico_data(bmp, size):
    """Convert int bitmap to 32bpp BGRA BMP for .ico entry."""
    pixels = bytearray()
    for y in range(size - 1, -1, -1):  # BMP bottom-up
        for x in range(size):
            v = bmp[y][x]
            if v:
                c = _COLORS[v]
                pixels += bytes([c[2], c[1], c[0], 0xFF])
            else:
                pixels += b'\x00\x00\x00\x00'  # transparent
    bih = struct.pack('<IiiHHIIiiII',
        40, size, size * 2, 1, 32, 0, len(pixels), 0, 0, 0, 0)
    and_mask = b'\x00' * (size * ((size + 31) // 32) * 4)
    return bih + bytes(pixels) + and_mask


def write_ico(sizes, outpath):
    entries = []
    for sz in sizes:
        bmp = make_bitmap(sz)
        data = bmp_to_ico_data(bmp, sz)
        entries.append((sz, data))

    header = struct.pack('<HHH', 0, 1, len(entries))
    dir_entries = bytearray()
    img_data = bytearray()
    offset = 6 + 16 * len(entries)

    for sz, data in entries:
        img_size = len(data)
        dir_entries += struct.pack('<BBBBHHII',
            sz if sz < 256 else 0,
            sz if sz < 256 else 0,
            0, 0, 1, 32, img_size, offset)
        offset += img_size
        img_data += data

    os.makedirs(os.path.dirname(outpath), exist_ok=True)
    with open(outpath, 'wb') as f:
        f.write(header)
        f.write(bytes(dir_entries))
        f.write(bytes(img_data))
    print(f'Wrote {outpath} — sizes: {sizes}')


if __name__ == '__main__':
    write_ico([16, 24, 32, 48, 256], 'res/app.ico')
