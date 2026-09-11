"""Per-pixel occlusion between two captures a step apart (T430).

Offset mapping shifts texels; occlusion hides some and reveals others. Dense
optical flow both ways shows the difference: where flow is a smooth shift,
forward then backward returns to the start; where one pebble uncovers the
ground behind it, it does not. The fraction of pixels that fail that round
trip, march against one-step against flat, is the measure that tile averages
could not give.

    python tools/occlusion_fb.py --selftest
    python tools/occlusion_fb.py out/pom29        (reads -flat/-onestep/-march -0/-1.png)
"""
import sys
import cv2
import numpy as np


def load(path):
    g = cv2.imread(path, cv2.IMREAD_GRAYSCALE)
    if g is None:
        raise SystemExit(f'cannot read {path}')
    return g[int(g.shape[0] * 0.3):]  # the ground; the top of a 40-degree view is distance


def roundtrip(a, b):
    dis = cv2.DISOpticalFlow_create(cv2.DISOPTICAL_FLOW_PRESET_MEDIUM)
    ahead = dis.calc(a, b, None)
    back = dis.calc(b, a, None)
    h, w = a.shape
    ys, xs = np.mgrid[0:h, 0:w].astype(np.float32)
    mx, my = xs + ahead[..., 0], ys + ahead[..., 1]
    returned = cv2.remap(back, mx, my, cv2.INTER_LINEAR)
    error = np.linalg.norm(ahead + returned, axis=2)
    inside = (mx >= 1) & (mx < w - 2) & (my >= 1) & (my < h - 2)
    detail = np.linalg.norm(ahead - cv2.GaussianBlur(ahead, (0, 0), 12), axis=2)
    return error[inside], np.linalg.norm(ahead, axis=2)[inside], detail[inside]


def report(name, a, b):
    error, flow, detail = roundtrip(load(a), load(b))
    print(f'{name:8s} flow {np.median(flow):6.2f} px, detail {np.median(detail):5.2f} px '
          f'(p99 {np.percentile(detail, 99):5.2f}), round trip off >1 px {100 * np.mean(error > 1):5.2f}%, '
          f'>2 px {100 * np.mean(error > 2):5.2f}%')


def selftest():
    rng = np.random.default_rng(1)
    texture = lambda: cv2.GaussianBlur((rng.random((200, 300)) * 255).astype(np.uint8), (0, 0), 1.5)
    ground, pebble = texture(), texture()
    a, b = ground.copy(), np.roll(ground, 2, 1)          # the ground slides 2 px
    shift, _, _ = roundtrip(a, b)
    a2, b2 = a.copy(), b.copy()                          # and a pebble slides 10 over it
    a2[60:140, 100:180] = pebble[60:140, 100:180]
    b2[60:140, 110:190] = pebble[60:140, 100:180]
    hidden, _, _ = roundtrip(a2, b2)
    plain, occluded = np.mean(shift > 1), np.mean(hidden > 1)
    assert plain < 0.02 and occluded > plain + 0.01, (plain, occluded)
    print(f'selftest: a plain shift {100 * plain:.2f}% off the round trip, with an occluder {100 * occluded:.2f}%')


if __name__ == '__main__':
    if sys.argv[1:] == ['--selftest']:
        selftest()
    else:
        for stem in sys.argv[1:]:
            for arm in ('flat', 'onestep', 'march'):
                try:
                    report(f'{arm}', f'{stem}-{arm}-0.png', f'{stem}-{arm}-1.png')
                except SystemExit as missing:
                    print(f'{arm:8s} {missing}')
