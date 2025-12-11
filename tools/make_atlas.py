# tools/make_atlas.py
# Generates a 32x32-per-tile atlas (8x8 = 64 tiles) and a CSV legend.
from PIL import Image, ImageDraw, ImageFilter
import random, math, os

TILE = 32
GRID = 8
W = H = TILE * GRID

def hashnoise(x, y, seed=0):
    n = (x*374761393 + y*668265263 + seed*700001) & 0xFFFFFFFF
    n = (n ^ (n >> 13)) * 1274126177 & 0xFFFFFFFF
    return ((n ^ (n >> 16)) & 0xFFFFFFFF) / 0xFFFFFFFF

def make_noise_tile(color_base, color_var, seed=0, rough=2):
    img = Image.new('RGB', (TILE, TILE), color_base)
    px = img.load()
    for j in range(TILE):
        for i in range(TILE):
            n = 0.0; freq = 1.0; amp = 1.0; total = 0.0
            for o in range(3 + rough):
                n += hashnoise(int(i*freq), int(j*freq), seed + o*97) * amp
                total += amp; freq *= 2.1; amp *= 0.5
            t = (n / max(total, 1e-6)) * 0.8
            r = int(color_base[0]*(1-t) + color_var[0]*t)
            g = int(color_base[1]*(1-t) + color_var[1]*t)
            b = int(color_base[2]*(1-t) + color_var[2]*t)
            px[i,j] = (r,g,b)
    return img

def make_checker(a, b, cells=4):
    img = Image.new('RGB', (TILE, TILE), a); d = ImageDraw.Draw(img)
    step = max(1, TILE//cells)
    for y in range(cells):
        for x in range(cells):
            if (x + y) % 2 == 0:
                d.rectangle([x*step, y*step, (x+1)*step-1, (y+1)*step-1], fill=b)
    return img

def make_stripes(a, b, vertical=True, bands=6):
    img = Image.new('RGB', (TILE, TILE), a); d = ImageDraw.Draw(img)
    for i in range(bands):
        if vertical:
            x0 = int(i*TILE/bands); x1 = int((i+1)*TILE/bands)-1
            d.rectangle([x0,0,x1,TILE-1], fill=a if i%2==0 else b)
        else:
            y0 = int(i*TILE/bands); y1 = int((i+1)*TILE/bands)-1
            d.rectangle([0,y0,TILE-1,y1], fill=a if i%2==0 else b)
    return img

def make_planks(base=(130,95,60), var=(90,65,45), planks=4, seed=0):
    img = make_noise_tile(base, var, seed, rough=1); d = ImageDraw.Draw(img)
    h = max(1, TILE//planks)
    for i in range(1, planks): d.line([(0,i*h),(TILE-1,i*h)], fill=(60,40,25))
    for i in range(planks):
        y = int(i*h + h*0.2)
        for x in [int(TILE*0.2), int(TILE*0.8)]:
            if random.random() < 0.6: d.ellipse([x-1,y-1,x+1,y+1], fill=(50,50,50))
    return img

def make_bricks(base=(140,60,50), mortar=(190,190,190), rows=6, seed=0):
    img = Image.new('RGB', (TILE, TILE), mortar); d = ImageDraw.Draw(img)
    row_h = TILE/max(1,rows)
    for r in range(rows):
        y0 = int(r*row_h); y1 = int((r+1)*row_h)
        offset = (r%2)*(TILE//4); c = 4; bw = TILE//c
        for i in range(-1, c+1):
            x0 = i*bw + offset; x1 = x0 + bw - 1
            mul = 0.9+0.2*hashnoise(i,r,seed + r*31 + i*17)
            bcol = tuple(int(min(255, max(0, v * mul))) for v in base)
            d.rectangle([x0,y0,x1,y1], fill=bcol)
    return img

def make_leaves():
    img = make_noise_tile((40,100,35),(20,160,20), seed=5, rough=2); d = ImageDraw.Draw(img)
    for _ in range(25): d.point((random.randrange(TILE), random.randrange(TILE)), fill=(70,170,60))
    return img

def make_runes(bg=(20,20,22), rune=(80,180,255)):
    img = Image.new('RGB',(TILE,TILE), bg); d = ImageDraw.Draw(img)
    cx, cy = TILE//2, TILE//2
    for r in [5,9,13]: d.ellipse([cx-r,cy-r,cx+r,cy+r], outline=rune)
    for a in range(0,360,45):
        rad = math.radians(a)
        x = cx + int(math.cos(rad)*13); y = cy + int(math.sin(rad)*13)
        d.line([cx,cy,x,y], fill=rune)
    for _ in range(35): d.point((random.randrange(3,TILE-3), random.randrange(3,TILE-3)), fill=rune)
    return img

def make_metal():             return make_stripes((155,165,175),(220,230,240), vertical=False, bands=8)
def make_sand():              return make_noise_tile((194,178,128),(210,200,150), seed=11, rough=1)
def make_snow():              return make_noise_tile((235,240,250),(255,255,255), seed=21, rough=0)
def make_grass_top():         return make_noise_tile((36,120,36),(60,180,60), seed=7, rough=2)
def make_dirt():              return make_noise_tile((90,62,40),(120,85,55), seed=8, rough=2)
def make_grass_side():
    img = make_dirt(); d = ImageDraw.Draw(img)
    for y in range(TILE//3):
        t = y/(TILE/3)
        col = (int(36*(1-t)+60*t), int(120*(1-t)+180*t), int(36*(1-t)+60*t))
        d.line([(0,y),(TILE-1,y)], fill=col)
    return img
def make_cobble():
    img = Image.new('RGB',(TILE,TILE),(110,110,110)); d = ImageDraw.Draw(img)
    for _ in range(40):
        x,y = random.randrange(TILE), random.randrange(TILE)
        r = random.randint(2,5); g = random.randint(90,140)
        d.ellipse([x-r,y-r,x+r,y+r], fill=(g,g,g))
    return img.filter(ImageFilter.SMOOTH_MORE)
def make_ore(base, speck):
    img = make_noise_tile(base, tuple(min(255,int(v*1.1)) for v in base), seed=33, rough=2); d = ImageDraw.Draw(img)
    for _ in range(45):
        d.point((random.randrange(TILE), random.randrange(TILE)), fill=speck)
    return img
def make_glass(tint=(130,200,235)):
    img = Image.new('RGB',(TILE,TILE),(0,0,0)); d = ImageDraw.Draw(img)
    d.rectangle([0,0,TILE-1,TILE-1], outline=(200,200,200))
    d.line([(0,TILE//2),(TILE-1,TILE//2)], fill=tint)
    d.line([(TILE//2,0),(TILE//2,TILE-1)], fill=tint)
    for j in range(TILE):
        for i in range(TILE):
            if (i+j)%3==0: d.point((i,j), fill=tint)
    return img
def make_concrete():          return make_noise_tile((130,130,130),(180,180,180), seed=77, rough=1)
def make_brushed_copper():    return make_stripes((184,115,51),(196,138,76), vertical=True, bands=10)
def make_obsidian():          return make_noise_tile((10,10,20),(30,20,60), seed=101, rough=2)
def make_glow_runes():        return make_runes(bg=(15,15,20), rune=(40,220,255))
def make_lamp():
    img = Image.new('RGB',(TILE,TILE),(30,30,30)); d = ImageDraw.Draw(img)
    d.rectangle([6,6,TILE-7,TILE-7], outline=(80,80,80))
    d.rectangle([9,9,TILE-10,TILE-10], fill=(245,220,120))
    return img
def make_books():
    img = Image.new('RGB',(TILE,TILE),(70,40,20)); d = ImageDraw.Draw(img); bands = 6
    cols = [(150,40,40),(40,90,160),(50,120,60),(160,120,40)]
    for i in range(bands):
        y0 = int(i*TILE/bands); y1 = int((i+1)*TILE/bands)-1
        d.rectangle([0,y0,TILE-1,y1], fill=cols[i%4]); d.line([4,y0,4,y1], fill=(230,220,180))
    return img

materials = [
    ("Stone",           lambda: make_noise_tile((110,110,110),(150,150,150), seed=1, rough=2)),
    ("Cobblestone",     make_cobble),
    ("Dirt",            make_dirt),
    ("Grass Top",       make_grass_top),
    ("Grass Side",      make_grass_side),
    ("Sand",            make_sand),
    ("Snow",            make_snow),
    ("Gravel",          lambda: make_noise_tile((120,120,120),(100,100,100), seed=13, rough=1)),

    ("Oak Planks",      lambda: make_planks(seed=2)),
    ("Birch Planks",    lambda: make_planks(base=(170,150,110), var=(140,120,90), seed=3)),
    ("Brick",           make_bricks),
    ("Stone Bricks",    lambda: make_bricks(base=(120,120,120), mortar=(170,170,170), rows=7)),
    ("Glass",           make_glass),
    ("Concrete",        make_concrete),
    ("Steel",           make_metal),
    ("Copper",          make_brushed_copper),

    ("Leaves",          make_leaves),
    ("Wood Log",        lambda: make_stripes((120,90,60),(100,70,50), vertical=False, bands=12)),
    ("Clay",            lambda: make_noise_tile((165,180,195),(180,190,200), seed=15, rough=1)),
    ("Terracotta",      lambda: make_noise_tile((180,100,70),(200,120,90), seed=16, rough=1)),
    ("Obsidian",        make_obsidian),
    ("Lamp",            make_lamp),
    ("Glow Runes",      make_glow_runes),
    ("Arcane Runes",    make_runes),

    ("Coal Ore",        lambda: make_ore((110,110,110),(25,25,25))),
    ("Iron Ore",        lambda: make_ore((130,110,90),(210,190,140))),
    ("Gold Ore",        lambda: make_ore((130,110,90),(245,220,120))),
    ("Redstone Ore",    lambda: make_ore((110,110,110),(200,40,40))),
    ("Lapis Ore",       lambda: make_ore((110,110,110),(50,70,200))),
    ("Emerald Ore",     lambda: make_ore((110,110,110),(40,200,100))),
    ("Diamond Ore",     lambda: make_ore((110,110,110),(120,200,240))),
    ("Quartz Ore",      lambda: make_ore((200,200,200),(245,245,245))),

    ("Rune Brick",      lambda: make_bricks(base=(60,60,70), mortar=(90,90,100), rows=6)),
    ("Chiseled Stone",  lambda: make_checker((120,120,120),(140,140,140), cells=8)),
    ("Tiled Slate",     lambda: make_checker((50,60,70),(70,80,90), cells=6)),
    ("Mosaic",          lambda: make_checker((120,90,60),(170,130,80), cells=4)),
    ("Fabric",          lambda: make_stripes((120,60,60),(150,90,90), vertical=False, bands=10)),
    ("Carpet",          lambda: make_checker((160,40,40),(180,60,60), cells=8)),
    ("Circuit",         lambda: make_checker((25,25,28),(35,35,40), cells=4)),
    ("Panel",           lambda: make_stripes((60,60,70),(80,80,90), vertical=True, bands=6)),

    ("Dark Planks",     lambda: make_planks(base=(80,60,40), var=(60,45,35), seed=4)),
    ("Sandstone",       lambda: make_noise_tile((210,200,160),(230,220,180), seed=20, rough=1)),
    ("Ice",             lambda: make_glass((170,220,255))),
    ("Marble",          lambda: make_noise_tile((220,220,225),(255,255,255), seed=30, rough=0)),
    ("Basalt",          lambda: make_noise_tile((50,50,60),(30,30,40), seed=31, rough=2)),
    ("Chalk",           lambda: make_noise_tile((235,235,225),(250,250,250), seed=32, rough=0)),
    ("Roof Tiles",      lambda: make_stripes((120,50,50),(150,70,70), vertical=False, bands=12)),
    ("Shingles",        lambda: make_checker((110,80,60),(95,70,55), cells=6)),

    ("Rune Copper",     lambda: make_runes(bg=(60,40,25), rune=(230,170,60))),
    ("Rune Emerald",    lambda: make_runes(bg=(15,25,20), rune=(60,220,140))),
    ("Rune Amethyst",   lambda: make_runes(bg=(25,20,35), rune=(190,110,255))),
    ("Rune Sapphire",   lambda: make_runes(bg=(15,20,35), rune=(90,150,255))),
    ("Rune Ruby",       lambda: make_runes(bg=(35,15,20), rune=(255,100,120))),
    ("Gilded Panel",    lambda: make_checker((100,85,50),(155,130,70), cells=5)),
    ("Engraved Gold",   lambda: make_checker((180,150,60),(220,200,120), cells=6)),
    ("Runed Obsidian",  lambda: make_runes(bg=(8,8,12), rune=(140,120,255))),
]

atlas = Image.new('RGB', (W, H), (0,0,0))
legend_lines = ["index,tile_x,tile_y,name"]
idx = 0
for ty in range(GRID):
    for tx in range(GRID):
        name, gen = materials[idx] if idx < len(materials) else (f"Slot {idx}", lambda: make_checker((80,80,80),(100,100,100), cells=8))
        atlas.paste(gen(), (tx*TILE, ty*TILE))
        legend_lines.append(f"{idx},{tx},{ty},{name}")
        idx += 1

os.makedirs("build_assets", exist_ok=True)
png_path = os.path.join("build_assets", "atlas_32px_8x8.png")
csv_path = os.path.join("build_assets", "atlas_32px_8x8_legend.csv")
atlas.save(png_path, "PNG")
with open(csv_path, "w", encoding="utf-8") as f:
    f.write("\n".join(legend_lines))
print("Wrote:", png_path, "and", csv_path)