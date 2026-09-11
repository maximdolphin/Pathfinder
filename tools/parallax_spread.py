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

import cv2
import numpy as np
from PIL import Image


def grey(path, scale):
    img = Image.open(path).convert('L')
    img = img.resize((img.width // scale, img.height // scale), Image.BILINEAR)
    return np.asarray(img, dtype=np.float64)


def shift(a, b, y, x, size, reach, seed=(0, 0)):
    """The (dy, dx) that best moves tile (y, x) of a onto b, by normalised
    cross-correlation within +-reach pixels of seed, refined to a fraction of
    a pixel by a parabola through the peak."""
    tile = a[y:y + size, x:x + size].astype(np.float32)
    sy, sx = int(round(seed[0])), int(round(seed[1]))
    y0, x0 = max(0, y + sy - reach), max(0, x + sx - reach)
    y1 = min(b.shape[0], y + sy + reach + size)
    x1 = min(b.shape[1], x + sx + reach + size)
    if y1 - y0 < size + 2 or x1 - x0 < size + 2 or tile.std() < 1e-6:
        return (0.0, 0.0), -1.0
    scores = cv2.matchTemplate(b[y0:y1, x0:x1].astype(np.float32), tile, cv2.TM_CCOEFF_NORMED)
    iy, ix = np.unravel_index(np.argmax(scores), scores.shape)
    best = float(scores[iy, ix])

    def refine(m, c, p):
        denom = m - 2 * c + p
        return 0.0 if abs(denom) < 1e-12 else 0.5 * (m - p) / denom

    fy = refine(scores[iy - 1, ix], best, scores[iy + 1, ix]) if 0 < iy < scores.shape[0] - 1 else 0.0
    fx = refine(scores[iy, ix - 1], best, scores[iy, ix + 1]) if 0 < ix < scores.shape[1] - 1 else 0.0
    return (y0 + iy + fy - y, x0 + ix + fx - x), best


def field(path_a, path_b, size=48, step=64, reach=48, scale=2, coarse=8, far=720):
    """Per-tile shifts, coarse to fine. A metre's step two metres over the
    ground moves it half a frame, which no +-48 px search finds: a tile four
    times the side at 1/8 resolution finds where each one went within +-far
    pixels, and the fine search looks +-reach around that."""
    a, b = grey(path_a, scale), grey(path_b, scale)
    ca, cb = grey(path_a, coarse), grey(path_b, coarse)
    k = coarse // scale
    s, st, r = size // scale, step // scale, reach // scale
    cs = 4 * size // coarse
    out = {}
    for y in range(st, a.shape[0] - s - st, st):
        for x in range(st, a.shape[1] - s - st, st):
            cy, cx = (y + s // 2) // k - cs // 2, (x + s // 2) // k - cs // 2
            if cy < 0 or cx < 0 or cy + cs > ca.shape[0] or cx + cs > ca.shape[1]:
                continue
            (gy, gx), rough = shift(ca, cb, cy, cx, cs, far // coarse)
            if rough < 0.5:
                continue
            (dy, dx), score = shift(a, b, y, x, s, r, (gy * k, gx * k))
            if score > 0.6:
                out[(y, x)] = (dy * scale, dx * scale)
    return out


def selftest():
    """A noise texture moved (130, -310) px must come back as that."""
    import os
    import tempfile
    rng = np.random.default_rng(1)
    base = cv2.GaussianBlur(rng.random((1400, 2400)).astype(np.float32), (0, 0), 3)
    base = (255 * (base - base.min()) / (base.max() - base.min())).astype(np.uint8)
    d = tempfile.mkdtemp()
    pa, pb = os.path.join(d, 'a.png'), os.path.join(d, 'b.png')
    Image.fromarray(base[200:1280, 400:2320]).save(pa)
    Image.fromarray(base[200 - 130:1280 - 130, 400 + 310:2320 + 310]).save(pb)
    got = np.array(list(field(pa, pb).values()))
    assert len(got) > 50, len(got)
    med = np.median(got, axis=0)
    assert abs(med[0] - 130) < 1 and abs(med[1] + 310) < 1, med
    print(f'selftest: {len(got)} tiles, median shift {med[0]:.2f}, {med[1]:.2f} px')


if __name__ == '__main__':
    args = sys.argv[1:]
    if args == ['--selftest']:
        selftest()
        sys.exit(0)
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
