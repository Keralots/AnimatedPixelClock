#!/usr/bin/env python3
"""Convert an animated GIF into a PixelClock .pca animation.

The device plays custom animations from its flash filesystem as the
"Custom animation" ambient style. This tool does the heavy lifting on the
PC so the firmware stays trivial: frames are fitted to the 128x64 panel,
quantized to one global palette of up to 16 colors and packed at 4 bits
per pixel.

PCA1 format (little-endian):
    offset      size    field
    0           4       magic "PCA1"
    4           2       frameCount N (1..360)
    6           2       defaultFrameMs (metadata, 20..5000)
    8           1       paletteLen P (2..16)
    9           1       flags (bit0: loop)
    10          2       reserved (0)
    12          P*2     palette, RGB565
    12+P*2      N*2     per-frame delay in ms (clamped 34..5000)
    12+P*2+N*2  N*4096  4bpp frames, 2 px/byte, high nibble = left pixel

Usage:
    python tools/gif2pca.py input.gif
    python tools/gif2pca.py input.gif -o fine.pca --anchor bottom --preview check.gif
    python tools/gif2pca.py input.gif --upload http://pixelclock.local

Requires Pillow (pip install pillow). Upload uses the standard library.
"""

import argparse
import io
import os
import struct
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow is required: pip install pillow")

PANEL_W, PANEL_H = 128, 64


def pixel_data(img):
    """Flat pixel sequence; Pillow 14 renames getdata()."""
    if hasattr(img, "get_flattened_data"):
        return list(img.get_flattened_data())
    return list(img.getdata())
MAX_FRAMES = 360
MIN_DELAY_MS = 34  # the device renders ambient at 30 Hz
MAX_DELAY_MS = 5000
MAX_PCA_BYTES = 1536 * 1024  # device hard cap


def load_frames(path):
    """Return ([RGB frames], [delays ms]) with GIF disposal handled."""
    im = Image.open(path)
    frames, delays = [], []
    index = 0
    try:
        while True:
            im.seek(index)
            frames.append(im.convert("RGB"))
            delays.append(int(im.info.get("duration", 100)) or 100)
            index += 1
    except EOFError:
        pass
    if not frames:
        sys.exit("no frames found in %s" % path)
    return frames, delays


def fit_frame(frame, mode, anchor):
    """Fit one RGB frame to 128x64 via crop, pad or stretch."""
    w, h = frame.size
    if mode == "stretch":
        return frame.resize((PANEL_W, PANEL_H), Image.BOX)

    target = PANEL_W / PANEL_H  # 2:1
    if mode == "crop":
        # Cut the excess dimension, keep the anchored edge.
        if w / h > target:  # too wide: crop x
            cw = int(round(h * target))
            if anchor == "start":
                x0 = 0
            elif anchor == "end":
                x0 = w - cw
            else:
                x0 = (w - cw) // 2
            frame = frame.crop((x0, 0, x0 + cw, h))
        else:  # too tall: crop y
            ch = int(round(w / target))
            if anchor == "start":
                y0 = 0
            elif anchor == "end":
                y0 = h - ch
            else:
                y0 = (h - ch) // 2
            frame = frame.crop((0, y0, w, y0 + ch))
        return frame.resize((PANEL_W, PANEL_H), Image.BOX)

    # pad: letterbox on black
    scale = min(PANEL_W / w, PANEL_H / h)
    sw, sh = max(1, int(round(w * scale))), max(1, int(round(h * scale)))
    scaled = frame.resize((sw, sh), Image.BOX)
    canvas = Image.new("RGB", (PANEL_W, PANEL_H), (0, 0, 0))
    canvas.paste(scaled, ((PANEL_W - sw) // 2, (PANEL_H - sh) // 2))
    return canvas


def quantize_frames(frames, colors):
    """One global palette across all frames; returns (index frames, palette565)."""
    stack = Image.new("RGB", (PANEL_W, PANEL_H * len(frames)))
    for k, f in enumerate(frames):
        stack.paste(f, (0, PANEL_H * k))
    pal_img = stack.quantize(colors=colors, method=Image.FASTOCTREE)
    quantized = [f.quantize(palette=pal_img, dither=Image.Dither.NONE) for f in frames]

    used = max(max(pixel_data(f)) for f in quantized) + 1
    rgb = pal_img.getpalette()[: used * 3]
    palette565 = []
    for c in range(used):
        r, g, b = rgb[c * 3], rgb[c * 3 + 1], rgb[c * 3 + 2]
        palette565.append(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
    return quantized, palette565


def pack_frame(indexed):
    data = pixel_data(indexed)
    packed = bytearray(PANEL_W * PANEL_H // 2)
    for p in range(0, len(data), 2):
        packed[p // 2] = (data[p] << 4) | data[p + 1]
    return bytes(packed)


def build_pca(quantized, palette565, delays):
    n = len(quantized)
    p = max(2, len(palette565))
    default_ms = max(MIN_DELAY_MS, min(MAX_DELAY_MS, round(sum(delays) / n)))
    out = io.BytesIO()
    out.write(b"PCA1")
    out.write(struct.pack("<HHBBH", n, default_ms, p, 1, 0))
    for c in range(p):
        out.write(struct.pack("<H", palette565[c] if c < len(palette565) else 0))
    for d in delays:
        out.write(struct.pack("<H", d))
    for f in quantized:
        out.write(pack_frame(f))
    return out.getvalue()


def write_preview(quantized, delays, path):
    frames = [f.convert("RGB").resize((PANEL_W * 4, PANEL_H * 4), Image.NEAREST)
              for f in quantized]
    frames[0].save(path, save_all=True, append_images=frames[1:],
                   duration=delays, loop=0)


def upload(blob, name, base_url):
    import urllib.request
    boundary = "----gif2pca"
    body = io.BytesIO()
    body.write(("--%s\r\nContent-Disposition: form-data; name=\"anim\"; "
                "filename=\"%s.pca\"\r\n"
                "Content-Type: application/octet-stream\r\n\r\n"
                % (boundary, name)).encode())
    body.write(blob)
    body.write(("\r\n--%s--\r\n" % boundary).encode())
    req = urllib.request.Request(
        base_url.rstrip("/") + "/api/anim/upload?name=" + name,
        data=body.getvalue(),
        headers={"Content-Type": "multipart/form-data; boundary=" + boundary})
    with urllib.request.urlopen(req, timeout=60) as resp:
        print("device:", resp.read().decode(errors="replace"))


def sanitize_name(raw):
    name = "".join(c for c in raw if c.isalnum() or c in "_-")[:24]
    return name or "anim"


def main():
    ap = argparse.ArgumentParser(description="Convert a GIF to a PixelClock .pca animation")
    ap.add_argument("gif", help="input GIF file")
    ap.add_argument("-o", "--output", help="output .pca path (default: <gif>.pca)")
    ap.add_argument("--fit", choices=["crop", "pad", "stretch"], default="crop",
                    help="how to fit to 128x64 (default crop)")
    ap.add_argument("--anchor", choices=["center", "start", "end"], default="center",
                    help="crop anchor: start=top/left edge kept, end=bottom/right "
                         "(e.g. --anchor end keeps a bottom caption)")
    ap.add_argument("--colors", type=int, default=16, choices=range(2, 17),
                    metavar="2-16", help="palette size (default 16)")
    ap.add_argument("--frame-skip", type=int, default=1, metavar="N",
                    help="keep every Nth frame, delays merged (default 1 = all)")
    ap.add_argument("--preview", metavar="OUT.GIF",
                    help="also write an upscaled preview GIF of the converted result")
    ap.add_argument("--upload", metavar="URL",
                    help="POST the result to the device, e.g. http://pixelclock.local")
    args = ap.parse_args()

    frames, delays = load_frames(args.gif)
    if args.frame_skip > 1:
        merged = []
        for i in range(0, len(frames), args.frame_skip):
            merged.append(sum(delays[i:i + args.frame_skip]))
        frames = frames[::args.frame_skip]
        delays = merged
    if len(frames) > MAX_FRAMES:
        sys.exit("too many frames (%d > %d); use --frame-skip" % (len(frames), MAX_FRAMES))
    delays = [max(MIN_DELAY_MS, min(MAX_DELAY_MS, d)) for d in delays]

    fitted = [fit_frame(f, args.fit, args.anchor) for f in frames]
    quantized, palette565 = quantize_frames(fitted, args.colors)
    blob = build_pca(quantized, palette565, delays)
    if len(blob) > MAX_PCA_BYTES:
        sys.exit("result is %d KiB, over the device cap of %d KiB; use --frame-skip"
                 % (len(blob) // 1024, MAX_PCA_BYTES // 1024))

    out_path = args.output or os.path.splitext(args.gif)[0] + ".pca"
    with open(out_path, "wb") as fh:
        fh.write(blob)
    total_ms = sum(delays)
    print("%s: %d frames, %d colors, %.1fs loop, %d KiB -> %s"
          % (os.path.basename(args.gif), len(quantized), len(palette565),
             total_ms / 1000.0, len(blob) // 1024, out_path))

    if args.preview:
        write_preview(quantized, delays, args.preview)
        print("preview:", args.preview)

    if args.upload:
        name = sanitize_name(os.path.splitext(os.path.basename(out_path))[0])
        upload(blob, name, args.upload)
    else:
        print("upload with: curl -F \"anim=@%s\" \"http://pixelclock.local/api/anim/upload\""
              % out_path)


if __name__ == "__main__":
    main()
