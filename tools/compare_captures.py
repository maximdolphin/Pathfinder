# -*- coding: utf-8 -*-
"""Compares the scripted flight's captures against committed references.

**Why this exists.** Three rendering regressions have survived review on this
project because the check was a person looking at one image. The sea rendered as
grey default material for a whole task. The town went unphotographed for two
milestones because the capture that was inspected was the one that happened to
be right. A frame time of 19 fps went unnoticed for twenty-six tasks because a
screenshot does not contain one.

A perceptual diff will not catch a frame time, but it catches the other two, and
it catches them on the commit that caused them rather than three weeks later.

Deliberately dependency-free: PNG decoding is done here rather than pulling in
Pillow, because a CI check that needs an install is a CI check somebody turns
off. It only handles the non-interlaced truecolour PNGs the engine writes.

    python tools/compare_captures.py            compare out/ against reference/
    python tools/compare_captures.py --accept   adopt out/ as the new reference
"""

import argparse
import io
import os
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CAPTURES = os.path.join(ROOT, "out")
REFERENCE = os.path.join(ROOT, "docs", "reference-captures")

# Structural difference, after a global brightness offset has been removed.
# This is the number that catches a material change, a geometry change or a
# missing object.
RESIDUAL_TOLERANCE = 2.5

# The brightness offset itself, tolerated separately and generously.
#
# Auto-exposure adapts over frames, and two runs reach the same *game* time
# after different numbers of frames, so the adapted exposure differs slightly
# and every pixel shifts by the same small amount. That is not a regression, and
# judging it as one made the check cry wolf on a run where nothing had changed.
# Comparing the residual after removing the offset separates "the image is
# different" from "the image is the same, dimmer".
OFFSET_TOLERANCE = 14.0

CHANGED_FRACTION = 0.06  # fraction of pixels allowed to differ by more than 24


def read_png(path, max_rows=None):
    """Returns (width, height, rows of RGB bytes, channels) for a truecolour PNG.

    `max_rows` stops after that many rows. Undoing PNG's filters is a byte at
    a time in Python, so a full 4K texture is fifty million iterations; a
    caller that only wants channel statistics can ask for a strip instead.
    Rows must still be decoded from the top, because each filter references
    the row above it."""
    data = io.open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("%s is not a PNG" % path)

    position = 8
    header = None
    # Decompressed incrementally rather than all at once: a caller that only
    # wants the top strip of a 30 MB texture should not pay to inflate all of
    # it, and stopping early is the difference between six seconds and one.
    inflater = zlib.decompressobj()
    raw = b""
    enough = None
    channels = stride = None

    while position < len(data):
        length = struct.unpack(">I", data[position:position + 4])[0]
        kind = data[position + 4:position + 8]
        body = data[position + 8:position + 8 + length]
        position += 12 + length

        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
            width, height, depth, colour, _compression, _filt, interlace = header
            if depth != 8 or interlace != 0 or colour not in (2, 6):
                raise ValueError(
                    "%s: unsupported PNG (depth %d, colour %d, interlace %d)"
                    % (path, depth, colour, interlace))
            channels = 3 if colour == 2 else 4
            stride = width * channels
            if max_rows is not None:
                enough = min(height, max_rows) * (stride + 1)
        elif kind == b"IDAT":
            raw += inflater.decompress(body)
            if enough is not None and len(raw) >= enough:
                break
        elif kind == b"IEND":
            break

    if header is None:
        raise ValueError("%s: no IHDR" % path)
    width, height = header[0], header[1]

    rows = []
    previous = bytearray(stride)
    offset = 0
    wanted = height if max_rows is None else min(height, max_rows)
    for _ in range(wanted):
        method = raw[offset]
        line = bytearray(raw[offset + 1:offset + 1 + stride])
        offset += 1 + stride

        # PNG's per-row filters, undone. Byte at a time because the filters
        # reference bytes decoded earlier in the same row.
        for i in range(stride):
            a = line[i - channels] if i >= channels else 0
            b = previous[i]
            c = previous[i - channels] if i >= channels else 0
            if method == 1:
                line[i] = (line[i] + a) & 0xFF
            elif method == 2:
                line[i] = (line[i] + b) & 0xFF
            elif method == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 0xFF
            elif method == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                predictor = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + predictor) & 0xFF
        rows.append(bytes(line))
        previous = line

    return width, height, rows, channels


def compare(reference_path, capture_path):
    rw, rh, rrows, rc = read_png(reference_path)
    cw, ch, crows, cc = read_png(capture_path)

    if (rw, rh) != (cw, ch):
        return "size changed: reference %dx%d, capture %dx%d" % (rw, rh, cw, ch), 0.0, 0.0

    # Two passes: the first finds the global brightness offset, the second
    # measures what is left once that is taken out.
    signed = 0
    samples = 0
    for y in range(rh):
        a, b = rrows[y], crows[y]
        for x in range(0, rw * rc, rc):
            for channel in range(3):
                signed += b[x + channel] - a[x + channel]
                samples += 1

    offset = signed / float(samples)

    residual = 0.0
    changed = 0
    for y in range(rh):
        a, b = rrows[y], crows[y]
        for x in range(0, rw * rc, rc):
            for channel in range(3):
                difference = (b[x + channel] - a[x + channel]) - offset
                residual += abs(difference)
                if abs(difference) > 24:
                    changed += 1

    residual /= float(samples)
    fraction = changed / float(samples)

    if residual > RESIDUAL_TOLERANCE:
        return ("structural difference %.2f after removing a %.2f brightness "
                "offset (limit %.2f)" % (residual, offset, RESIDUAL_TOLERANCE)), residual, fraction
    if fraction > CHANGED_FRACTION:
        return ("%.1f%% of samples changed by more than 24 (limit %.1f%%)"
                % (fraction * 100.0, CHANGED_FRACTION * 100.0)), residual, fraction
    if abs(offset) > OFFSET_TOLERANCE:
        return ("brightness shifted by %.2f with no structural change (limit %.2f) "
                "— check exposure or lighting" % (offset, OFFSET_TOLERANCE)), residual, fraction
    return None, residual, fraction


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--accept", action="store_true",
                        help="adopt the current captures as the reference")
    arguments = parser.parse_args()

    if not os.path.isdir(CAPTURES):
        sys.stderr.write("no captures in out/ — run the scripted flight first\n")
        return 1

    captures = sorted(f for f in os.listdir(CAPTURES) if f.endswith(".png"))
    if not captures:
        sys.stderr.write("no captures in out/ — run the scripted flight first\n")
        return 1

    if arguments.accept:
        os.makedirs(REFERENCE, exist_ok=True)
        for name in captures:
            body = io.open(os.path.join(CAPTURES, name), "rb").read()
            io.open(os.path.join(REFERENCE, name), "wb").write(body)
        print("adopted %d captures as the reference" % len(captures))
        print("commit them, and say in the message what changed and why")
        return 0

    if not os.path.isdir(REFERENCE):
        sys.stderr.write("no reference captures. Run with --accept to create them.\n")
        return 1

    failures = []
    for name in sorted(os.listdir(REFERENCE)):
        if not name.endswith(".png"):
            continue
        capture = os.path.join(CAPTURES, name)
        if not os.path.isfile(capture):
            failures.append("%s: not captured. The flight did not reach that step."
                            % name)
            continue
        problem, mean, fraction = compare(os.path.join(REFERENCE, name), capture)
        if problem:
            failures.append("%s: %s" % (name, problem))
        else:
            print("  %-26s residual %.2f, %.2f%% changed" % (name, mean, fraction * 100.0))

    for name in captures:
        if not os.path.isfile(os.path.join(REFERENCE, name)):
            print("  %-26s new, no reference yet" % name)

    if failures:
        sys.stderr.write("\nCaptures differ from the reference:\n\n")
        for failure in failures:
            sys.stderr.write("  %s\n" % failure)
        sys.stderr.write(
            "\nIf the change is intended, look at the images, then run\n"
            "  python tools/compare_captures.py --accept\n"
            "and say in the commit message what changed and why.\n")
        return 1

    print("captures match the reference")
    return 0


if __name__ == "__main__":
    sys.exit(main())
