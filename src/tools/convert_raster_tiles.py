#!/usr/bin/env python3
"""Convert slippy-map raster tiles to raw RGB565 tiles.

The output preserves the input hierarchy. For an input tile like:

    <input-root>/37474/23718.png

the output is:

    <output-root>/37474/23718.rgb565

The firmware reads these files as little-endian RGB565, row-major,
256x256 pixels by default.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import sys

from PIL import Image

try:
    import numpy as np
except ImportError:  # pragma: no cover - fallback for lean Python installs
    np = None


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert PNG/JPG slippy tiles to raw RGB565 files."
    )
    parser.add_argument("input_root", type=Path, help="Root of the raster tile tree")
    parser.add_argument(
        "format",
        help="Input file format/extension, for example png, jpg, or jpeg",
    )
    parser.add_argument("output_root", type=Path, help="Output tile tree root")
    parser.add_argument(
        "--tile-size",
        type=int,
        default=256,
        help="Expected square tile size in pixels, default: 256",
    )
    parser.add_argument(
        "--overwrite",
        action="store_true",
        help="Overwrite existing .rgb565 files",
    )
    return parser.parse_args()


def rgb888_to_rgb565_bytes(image: Image.Image) -> bytes:
    rgb = image.convert("RGB")

    if np is not None:
        arr = np.asarray(rgb, dtype=np.uint8)
        packed = (
            ((arr[:, :, 0].astype(np.uint16) & 0xF8) << 8)
            | ((arr[:, :, 1].astype(np.uint16) & 0xFC) << 3)
            | (arr[:, :, 2].astype(np.uint16) >> 3)
        )
        return packed.astype("<u2", copy=False).tobytes()

    data = rgb.tobytes()
    out = bytearray((len(data) // 3) * 2)
    out_i = 0
    for i in range(0, len(data), 3):
        r = data[i]
        g = data[i + 1]
        b = data[i + 2]
        value = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        out[out_i] = value & 0xFF
        out[out_i + 1] = value >> 8
        out_i += 2
    return bytes(out)


def convert_tile(src: Path, dst: Path, tile_size: int, overwrite: bool) -> None:
    if dst.exists() and not overwrite:
        return

    with Image.open(src) as image:
        if image.size != (tile_size, tile_size):
            raise ValueError(
                f"{src} is {image.size[0]}x{image.size[1]}, expected "
                f"{tile_size}x{tile_size}"
            )
        converted = rgb888_to_rgb565_bytes(image)

    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(converted)


def main() -> int:
    args = parse_args()
    input_root = args.input_root.resolve()
    output_root = args.output_root.resolve()
    suffix = args.format.lower().lstrip(".")
    if suffix == "jpg":
        suffixes = {".jpg", ".jpeg"}
    else:
        suffixes = {f".{suffix}"}

    if not input_root.is_dir():
        print(f"Input root does not exist or is not a directory: {input_root}", file=sys.stderr)
        return 2

    tiles = sorted(
        path
        for path in input_root.rglob("*")
        if path.is_file() and path.suffix.lower() in suffixes
    )
    if not tiles:
        print(f"No {args.format} tiles found under {input_root}", file=sys.stderr)
        return 1

    converted = 0
    skipped = 0
    for src in tiles:
        rel = src.relative_to(input_root).with_suffix(".rgb565")
        dst = output_root / rel
        existed = dst.exists()
        convert_tile(src, dst, args.tile_size, args.overwrite)
        if existed and not args.overwrite:
            skipped += 1
        else:
            converted += 1

        done = converted + skipped
        if done % 250 == 0 or done == len(tiles):
            print(f"{done}/{len(tiles)} tiles processed", flush=True)

    print(f"Converted {converted} tile(s), skipped {skipped} existing tile(s).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
