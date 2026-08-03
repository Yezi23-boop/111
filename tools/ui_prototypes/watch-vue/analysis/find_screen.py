"""Find the exact watch screen (light background) bounds inside a Vue screenshot."""
import sys
from PIL import Image

def find_screen(path):
    im = Image.open(path).convert('RGB')
    px = im.load()
    w, h = im.size
    # Watch screen background is light #F6F5F0-ish. Search region x in [436,844]
    x0, x1 = 436, 844
    def is_light(x, y):
        r, g, b = px[x, y]
        return r > 220 and g > 220 and b > 210
    # find top-most light row inside x range (only count if majority light)
    def row_light(y):
        n = 0
        for x in range(x0, x1+1, 2):
            if is_light(x, y): n += 1
        return n
    ytop = None
    for y in range(0, 100):
        if row_light(y) > (x1-x0)//4:
            ytop = y
            break
    ybot = None
    for y in range(h-1, 400, -1):
        if row_light(y) > (x1-x0)//4:
            ybot = y
            break
    xleft = None
    for x in range(x0, x1+1):
        n = 0
        for y in range(0, h, 3):
            if is_light(x, y): n += 1
        if n > 30:
            xleft = x
            break
    xright = None
    for x in range(x1, x0-1, -1):
        n = 0
        for y in range(0, h, 3):
            if is_light(x, y): n += 1
        if n > 30:
            xright = x
            break
    print(f"{path}: screen bounds x=[{xleft},{xright}] w={xright-xleft+1} y=[{ytop},{ybot}] h={ybot-ytop+1}")
    return (xleft, ytop, xright, ybot)

if __name__ == '__main__':
    for p in sys.argv[1:]:
        find_screen(p)
