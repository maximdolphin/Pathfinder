"""How much a pair of ground frames disagrees with a single plane. T430.

Two captures of the same ground from two camera positions a metre apart.
A flat (normal-mapped) surface moves between them the way one plane does:
every patch of the frame shifts by an amount that varies smoothly across it.
A surface with depth moves by different amounts at different heights, so
after the smooth, planar part of the shift is taken out, what is left is the
parallax. This measures that residual.

    python tools/parallax_spread.py out/pom-march-0.png out/pom-march-1.png

Block matching on a grid of tiles, a least-squares plane-like (affine) fit to
the shifts, and the RMS of what the fit leaves. Pixels, at the capture's size.
"""
import sys

import numpy as np
from PIL import Image


def grey(path):
    return np.asarray(Image.open(path).convert('L'), dtype=np.float64)


def shift(a, b, y, x, size, reach):
    """The (dy, dx) that best moves tile (y, x) of a onto b, by exhaustive
    normalised cross-correlation within +-reach pixels."""
    tile = a[y:y + size, x:x + size]
    tile = tile - tile.mean()
    norm = np.sqrt((tile * tile).sum()) + 1e-9
    best, where = -2.0, (0, 0)
    for dy in range(-reach, reach + 1):
        for dx in range(-reach, reach + 1):
            yy, xx = y + dy, x + dx
            if yy < 0 or xx < 0 or yy + size > b.shape[0] or xx + size > b.shape[1]:
                continue
            other = b[yy:yy + size, xx:xx + size]
            other = other - other.mean()
            score = (tile * other).sum() / (norm * (np.sqrt((other * other).sum()) + 1e-9))
            if score > best:
                best, where = score, (dy, dx)
    return where, best


def spread(path_a, path_b, size=48, step=96, reach=40, scale=4):
    a, b = grey(path_a), grey(path_b)
    # Matched at a quarter of the size, then the shifts scaled back: a metre
    # of camera travel at two metres is a large shift at full resolution.
    small = lambda img: np.asarray(Image.fromarray(img.astype(np.uint8)).resize(
        (img.shape[1] // scale, img.shape[0] // scale), Image.BILINEAR), dtype=np.float64)
    sa, sb = small(a), small(b)
    s, st = size // scale, step // scale
    rows = []
    for y in range(st, sa.shape[0] - s - st, st):
        for x in range(st, sa.shape[1] - s - st, st):
            (dy, dx), score = shift(sa, sb, y, x, s, reach // scale)
            if score > 0.5:
                rows.append((x, y, dx * scale, dy * scale))
    if len(rows) < 6:
        return None
    m = np.array(rows, dtype=np.float64)
    design = np.column_stack([np.ones(len(m)), m[:, 0], m[:, 1]])
    residual = []
    for column in (2, 3):
        coef, *_ = np.linalg.lstsq(design, m[:, column], rcond=None)
        residual.append(m[:, column] - design @ coef)
    rms = float(np.sqrt(np.mean(np.square(residual[0]) + np.square(residual[1]))))
    return len(rows), rms


if __name__ == '__main__':
    result = spread(sys.argv[1], sys.argv[2])
    if result is None:
        print('too few matched tiles to say anything')
        sys.exit(1)
    tiles, rms = result
    print(f'{tiles} tiles matched; shift left after the planar fit: {rms:.2f} px RMS')
