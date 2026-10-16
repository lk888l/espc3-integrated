#!/usr/bin/env python3
"""Export labeled pet sheets and animation from the real C++ OLED renderer (Pillow)."""
import argparse
from pathlib import Path
import subprocess

from PIL import Image, ImageDraw, ImageFont


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--renderer", default="build/host/render_previews")
    parser.add_argument("--raw", default="build/previews")
    parser.add_argument("--output", default="docs/previews")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    raw, output = root / args.raw, root / args.output
    subprocess.run([str(root / args.renderer), str(raw)], check=True)
    output.mkdir(parents=True, exist_ok=True)
    font = ImageFont.truetype("DejaVuSans.ttf", 18)
    small = ImageFont.truetype("DejaVuSans.ttf", 15)
    styles = ["Eyes", "Pixel Cat", "Moon Bunny", "Mochi Slime"]
    actions = ["Idle", "Blink", "Happy", "Eat", "Sleep", "Surprised", "Sad", "Dance"]
    tile_w, tile_h, label_w, gap = 256, 128, 112, 10
    sheet = Image.new("RGB", (label_w + 4 * (tile_w + gap), 42 + 8 * (tile_h + gap)), "#171c26")
    draw = ImageDraw.Draw(sheet)
    for style, name in enumerate(styles):
        draw.text((label_w + style * (tile_w + gap) + 8, 12), name, font=font, fill="#c8d2e4")
    for action, name in enumerate(actions):
        y = 42 + action * (tile_h + gap)
        draw.text((8, y + 54), name, font=small, fill="#a7b7d0")
        for style in range(4):
            image = Image.open(raw / f"pet-{style}-{name.lower()}.pgm").convert("RGB")
            sheet.paste(image.resize((tile_w, tile_h), Image.Resampling.NEAREST),
                        (label_w + style * (tile_w + gap), y))
    sheet.save(output / "pets.png")
    idle = sheet.crop((label_w, 0, sheet.width, 42 + tile_h))
    idle.save(output / "pets-idle.png")
    frames = []
    for path in sorted(raw.glob("pet-animation-*.pgm")):
        frames.append(Image.open(path).convert("P").resize((384, 192), Image.Resampling.NEAREST))
    frames[0].save(output / "pets.gif", save_all=True, append_images=frames[1:],
                   duration=100, loop=0, optimize=True)
    print(f"Pet previews: {output / 'pets.png'} and {output / 'pets.gif'}")


if __name__ == "__main__":
    main()
