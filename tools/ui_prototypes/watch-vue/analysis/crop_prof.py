"""Analyze the cropped Vue watch region row/col profile to find the actual watch screen vs frame."""
import sys
from PIL import Image

def profile(path, x0, y0, x1, y1):
    im = Image.open(path).convert('RGB')
    px = im.load()
    w = x1-x0+1; h = y1-y0+1
    print(f"CROP {w}x{h}")
    print("ROW PROFILE:")
    for y in range(y0, y1, max(1,(y1-y0)//70)):
        tot=0;n=0
        for x in range(x0, x1+1, max(1,w//150)):
            r,g,b=px[x,y]; tot+=(r+g+b)//3; n+=1
        print(f"  y={y-y0:3d}  avg={tot//n}")
    print("COL PROFILE:")
    for x in range(x0, x1+1, max(1,w//110)):
        tot=0;n=0
        for y in range(y0, y1+1, max(1,h//90)):
            r,g,b=px[x,y]; tot+=(r+g+b)//3; n+=1
        print(f"  x={x-x0:3d}  avg={tot//n}")

if __name__=='__main__':
    path = sys.argv[1]
    profile(path, 436, 48, 844, 716)
