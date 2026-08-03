"""Pixel-level analysis of a PNG screenshot: size, palette, background, horizontal/vertical color band profile."""
import sys
from collections import Counter
from PIL import Image

def analyze(path):
    im = Image.open(path).convert('RGB')
    w, h = im.size
    px = im.load()
    print(f"FILE: {path}")
    print(f"SIZE: {w}x{h}")
    # Most common colors (background candidates)
    cnt = Counter()
    step = max(1, (w*h)//400000)
    for y in range(0, h, step):
        for x in range(0, w, step):
            cnt[px[x, y]] += 1
    print("TOP COLORS (sampled):")
    for col, c in cnt.most_common(14):
        print(f"  #{col[0]:02X}{col[1]:02X}{col[2]:02X}  count={c}")
    # Full-res top colors for exactness
    cntf = Counter()
    for y in range(0, h, max(1, (w*h)//2000000)):
        for x in range(0, w, max(1, (w*h)//2000000)):
            cntf[px[x, y]] += 1
    print("FULLRES TOP COLORS:")
    for col, c in cntf.most_common(10):
        print(f"  #{col[0]:02X}{col[1]:02X}{col[2]:02X}  count={c}")
    # Row brightness profile (to find bands/sections) and column profile
    def rowprof():
        out = []
        for y in range(0, h, max(1, h//64)):
            tot = 0; n = 0
            for x in range(0, w, max(1, w//128)):
                r, g, b = px[x, y]; tot += (r+g+b)//3; n += 1
            out.append((y, tot//n))
        return out
    def colprof():
        out = []
        for x in range(0, w, max(1, w//128)):
            tot = 0; n = 0
            for y in range(0, h, max(1, h//64)):
                r, g, b = px[x, y]; tot += (r+g+b)//3; n += 1
            out.append((x, tot//n))
        return out
    print("ROW PROFILE (y, brightness):")
    print(" ", rowprof())
    print("COL PROFILE (x, brightness):")
    print(" ", colprof())

if __name__ == '__main__':
    for p in sys.argv[1:]:
        analyze(p)
        print("="*60)
