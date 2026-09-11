"""How much a ground material moves under a moving camera, against a control. T430.

Two captures of the same ground from two camera positions a metre apart, and
the same two captures of a control build (parallax off). The geometry, the
cameras and the lighting are identical between the runs, so the shift of
every tile from the first frame to the second is the same in both -- except
where the material itself displaces the surface it draws. The difference
between the two shift fields is therefore what the material adds, and nothing
else: perspective, relief and camera motion cancel.

    python tools/parallax_spread.py out/pom-march-0.png out/pom-march-1.png \
        --control out/pom-flat-0.png out/pom-flat-1.png

Without --control it reports the raw shift field's spread after an affine fit,
which is dominated by perspective and relief and says little on its own (the
first run of this measured 36.4, 36.7 and 36.7 px for the march, the one-step
offset and flat ground: all perspective).
"""
import sys

import numpy as np
from PIL import Image


def grey(path, scale):
    img = Image.open(path).convert('L')
    img = img.resize((img.width // scale, img.height // scale), Image.BILINEAR)
    return np.asarray(img, dtype=np.float64)


def shift(a, b, y, x, size, reach):
    """The (dy, dx) that best moves tile (y, x) of a onto b, by exhaustive
    normalised cross-correlation within +-reach pixels, refined to a fraction
    of a pixel by a parabola through the peak."""
    tile = a[y:y + size, x:x + size]
    tile = tile - tile.mean()
    norm = np.sqrt((tile * tile).sum()) + 1e-9
    scores = np.full((2 * reach + 1, 2 * reach + 1), -2.0)
    for dy in range(-reach, reach + 1):
        for dx in range(-reach, reach + 1):
            yy, xx = y + dy, x + dx
            if yy < 0 or xx < 0 or yy + size > b.shape[0] or xx + size > b.shape[1]:
                continue
            other = b[yy:yy + size, xx:xx + size]
            other = other - other.mean()
            scores[dy + reach, dx + reach] = (tile * other).sum() / (
                norm * (np.sqrt((other * other).sum()) + 1e-9))
    iy, ix = np.unravel_index(np.argmax(scores), scores.shape)
    best = scores[iy, ix]

    def refine(m, c, p):
        denom = m - 2 * c + p
        return 0.0 if abs(denom) < 1e-12 else 0.5 * (m - p) / denom

    fy = refine(scores[iy - 1, ix], best, scores[iy + 1, ix]) if 0 < iy < 2 * reach else 0.0
    fx = refine(scores[iy, ix - 1], best, scores[iy, ix + 1]) if 0 < ix < 2 * reach else 0.0
    return (iy - reach + fy, ix - reach + fx), best


def field(path_a, path_b, size=48, step=64, reach=48, scale=2):
    a, b = grey(path_a, scale), grey(path_b, scale)
    s, st, r = size // scale, step // scale, reach // scale
    out = {}
    for y in range(st, a.shape[0] - s - st, st):
        for x in range(st, a.shape[1] - s - st, st):
            (dy, dx), score = shift(a, b, y, x, s, r)
            if score > 0.6:
                out[(y, x)] = (dy * scale, dx * scale)
    return out


if __name__ == '__main__':
    args = sys.argv[1:]
    control = None
    if '--control' in args:
        i = args.index('--control')
        control = args[i + 1:i + 3]
        args = args[:i]
    test = field(args[0], args[1])
    if control is None:
        m = np.array([(x, y, dx, dy) for (y, x), (dy, dx) in test.items()], dtype=np.float64)
        design = np.column_stack([np.ones(len(m)), m[:, 0], m[:, 1]])
        res = [m[:, c] - design @ np.linalg.lstsq(design, m[:, c], rcond=None)[0] for c in (2, 3)]
        print(f'{len(m)} tiles; shift left after an affine fit: '
              f'{float(np.sqrt(np.mean(res[0] ** 2 + res[1] ** 2))):.2f} px RMS (perspective dominates)')
        sys.exit(0)
    ref = field(control[0], control[1])
    common = sorted(set(test) & set(ref))
    if len(common) < 6:
        print('too few tiles matched in both pairs to say anything')
        sys.exit(1)
    d = np.array([[test[k][0] - ref[k][0], test[k][1] - ref[k][1]] for k in common])
    rms = float(np.sqrt(np.mean((d ** 2).sum(axis=1))))
    worst = float(np.sqrt((d ** 2).sum(axis=1)).max())
    print(f'{len(common)} tiles in both; the material moves the ground by {rms:.2f} px RMS '
          f'more than the control does (worst tile {worst:.2f} px)')
