"""Regenerate the SVG and multi-resolution Windows icon (requires Pillow)."""
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / 'assets'
ASSETS.mkdir(exist_ok=True)
SCALE = 4
canvas = Image.new('RGBA', (256 * SCALE, 256 * SCALE))
draw = ImageDraw.Draw(canvas)
elements = []


def rounded_box(x, y, width, height, radius, color):
    elements.append(f'<rect x="{x}" y="{y}" width="{width}" height="{height}" rx="{radius}" fill="{color}"/>')
    draw.rounded_rectangle((x*SCALE, y*SCALE, (x+width)*SCALE, (y+height)*SCALE), radius*SCALE, fill=color)


def circle(x, y, radius, color):
    elements.append(f'<circle cx="{x}" cy="{y}" r="{radius}" fill="{color}"/>')
    draw.ellipse(((x-radius)*SCALE, (y-radius)*SCALE, (x+radius)*SCALE, (y+radius)*SCALE), fill=color)


def polygon(points, color):
    elements.append('<polygon points="'+' '.join(f'{x},{y}' for x,y in points)+f'" fill="{color}"/>')
    draw.polygon([(x*SCALE,y*SCALE) for x,y in points], fill=color)


rounded_box(8, 8, 240, 240, 55, '#13796e')
rounded_box(84, 57, 88, 45, 13, '#ffffff')
rounded_box(42, 78, 172, 116, 23, '#ffffff')
circle(128, 136, 42, '#13796e')
polygon([(117,110),(124,110),(109,163),(100,163)], '#ffffff')
polygon([(132,110),(139,110),(156,163),(147,163)], '#ffffff')
rounded_box(125, 121, 6, 13, 2, '#ffffff')
rounded_box(124, 144, 8, 15, 2, '#ffffff')
circle(189, 102, 6, '#ef745f')
(ASSETS / 'teslacam.svg').write_text(
    '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256">\n'
    + '\n'.join(elements) + '\n</svg>\n', encoding='utf-8')
canvas = canvas.resize((256,256), Image.Resampling.LANCZOS)
canvas.save(ASSETS / 'teslacam.ico', sizes=[(s,s) for s in (16,20,24,32,40,48,64,96,128,256)])
preview = ROOT / '.verification/teslacam-icon.png'
preview.parent.mkdir(exist_ok=True)
canvas.save(preview)
print('Generated assets/teslacam.svg and assets/teslacam.ico')
