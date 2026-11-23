"""Pack the transparent ImageGen sheet as fixed, black-backed LVGL RGB565 frames."""

import argparse
import struct
from pathlib import Path

from PIL import Image

WIDTH, HEIGHT = 224, 188
COLUMNS, ROWS = 3, 2
FRAMES = ("resting", "blink", "attentive", "thoughtful", "talking", "wave")


def pack(source: Path) -> bytes:
    with Image.open(source) as sheet:
        if "A" not in sheet.getbands() or sheet.width % COLUMNS or sheet.height % ROWS:
            raise ValueError("Expected a 3 by 2 grid of sprites in an RGBA image")

        cell_width, cell_height = sheet.width // COLUMNS, sheet.height // ROWS
        output = bytearray()
        for index, name in enumerate(FRAMES):
            row, column = divmod(index, COLUMNS)
            left, top = column * cell_width, row * cell_height
            sprite = sheet.crop((left, top, left + cell_width, top + cell_height))
            # Ignore faint alpha noise when fitting each pose, then composite for native 565.
            bounds = sprite.getchannel("A").point(lambda alpha: 255 if alpha >= 30 else 0).getbbox()
            if not bounds:
                raise ValueError(f"Missing sprite in the {name} cell")

            sprite = sprite.crop(bounds)
            sprite.thumbnail((WIDTH - 12, HEIGHT - 8), Image.Resampling.LANCZOS)
            frame = Image.new("RGB", (WIDTH, HEIGHT))
            frame.paste(sprite, ((WIDTH - sprite.width) // 2, (HEIGHT - sprite.height) // 2), sprite)
            pixels = iter(frame.tobytes())
            for red, green, blue in zip(pixels, pixels, pixels):
                pixel = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
                output.extend(struct.pack("<H", pixel))

        return bytes(output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sheet", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.write_bytes(pack(args.sheet))
