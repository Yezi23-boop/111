"""Detect the watch-content bounding box inside a Vue 1280x720 screenshot (dark backdrop)."""
import sys
from PIL import Image

def find_bbox(path):
    im = Image.open(path).convert('RGB')
    w, h = im.size
    px = im.load()
    # background color is the corner color
    bg = px[3, 3]
    def far(c):
        return abs(c[0]-bg[0]) + abs(c[1]-bg[1]) + abs(c[2]-bg[2]) > 60
    xs = []
    ys = []
    for y in range(0, h, 2):
        for x in range(0, w, 2):
            if far(px[x, y]):
                xs.append(x); ys.append(y)
    if not xs:
        return None
    return (min(xs), min(ys), max(xs), max(ys))

if __name__ == '__main__':
    for p in sys.argv[1:]:
        bb = find_bbox(p)
        print(f"{p}: bbox={bb} -> w={bb[2]-bb[0]+1} h={bb[3]-bb[1]+1}")
