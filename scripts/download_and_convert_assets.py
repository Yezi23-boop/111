import os
import urllib.request
from PIL import Image, ImageDraw

# 目标输出文件
TARGET_C_FILE = "main/ui/custom/mini_game_sprites.c"
TARGET_H_FILE = "main/ui/custom/mini_game_sprites.h"

# 资源列表 (CDN 镜像与原始 Github)
ASSETS = {
    "bird_mid": {
        "urls": [
            "https://fastly.jsdelivr.net/gh/samuelcust/flappy-bird-assets@master/sprites/yellowbird-midflap.png",
            "https://raw.githubusercontent.com/samuelcust/flappy-bird-assets/master/sprites/yellowbird-midflap.png"
        ],
        "size": (34, 24),
        "color": (250, 204, 21),  # 黄色
        "fallback_type": "bird_mid"
    },
    "bird_up": {
        "urls": [
            "https://fastly.jsdelivr.net/gh/samuelcust/flappy-bird-assets@master/sprites/yellowbird-upflap.png",
            "https://raw.githubusercontent.com/samuelcust/flappy-bird-assets/master/sprites/yellowbird-upflap.png"
        ],
        "size": (34, 24),
        "color": (250, 204, 21),
        "fallback_type": "bird_up"
    },
    "bird_down": {
        "urls": [
            "https://fastly.jsdelivr.net/gh/samuelcust/flappy-bird-assets@master/sprites/yellowbird-downflap.png",
            "https://raw.githubusercontent.com/samuelcust/flappy-bird-assets/master/sprites/yellowbird-downflap.png"
        ],
        "size": (34, 24),
        "color": (250, 204, 21),
        "fallback_type": "bird_down"
    },
    "dino_run1": {
        "urls": [
            "https://fastly.jsdelivr.net/gh/dhhruv/Chrome-Dino-Runner@master/assets/Dino/DinoRun1.png",
            "https://raw.githubusercontent.com/dhhruv/Chrome-Dino-Runner/master/assets/Dino/DinoRun1.png"
        ],
        "size": (22, 24),  # 调整小恐龙手表比例 (原始为 44x47 缩放一半)
        "color": (75, 85, 99),  # 灰色
        "fallback_type": "dino_run1"
    },
    "dino_run2": {
        "urls": [
            "https://fastly.jsdelivr.net/gh/dhhruv/Chrome-Dino-Runner@master/assets/Dino/DinoRun2.png",
            "https://raw.githubusercontent.com/dhhruv/Chrome-Dino-Runner/master/assets/Dino/DinoRun2.png"
        ],
        "size": (22, 24),
        "color": (75, 85, 99),
        "fallback_type": "dino_run2"
    },
    "dino_jump": {
        "urls": [
            "https://fastly.jsdelivr.net/gh/dhhruv/Chrome-Dino-Runner@master/assets/Dino/DinoJump.png",
            "https://raw.githubusercontent.com/dhhruv/Chrome-Dino-Runner/master/assets/Dino/DinoJump.png"
        ],
        "size": (22, 24),
        "color": (75, 85, 99),
        "fallback_type": "dino_jump"
    },
    "dino_dead": {
        "urls": [
            "https://fastly.jsdelivr.net/gh/dhhruv/Chrome-Dino-Runner@master/assets/Dino/DinoDead.png",
            "https://raw.githubusercontent.com/dhhruv/Chrome-Dino-Runner/master/assets/Dino/DinoDead.png"
        ],
        "size": (22, 24),
        "color": (75, 85, 99),
        "fallback_type": "dino_dead"
    },
    "cactus": {
        "urls": [
            "https://fastly.jsdelivr.net/gh/dhhruv/Chrome-Dino-Runner@master/assets/Cactus/SmallCactus1.png",
            "https://raw.githubusercontent.com/dhhruv/Chrome-Dino-Runner/master/assets/Cactus/SmallCactus1.png"
        ],
        "size": (16, 24),  # 仙人掌
        "color": (22, 163, 74),  # 绿色
        "fallback_type": "cactus"
    },
    "pterosaur1": {
        "urls": [
            "https://fastly.jsdelivr.net/gh/dhhruv/Chrome-Dino-Runner@master/assets/Bird/Bird1.png",
            "https://raw.githubusercontent.com/dhhruv/Chrome-Dino-Runner/master/assets/Bird/Bird1.png"
        ],
        "size": (24, 20),  # 翼龙1
        "color": (107, 114, 128),  # 深灰
        "fallback_type": "pterosaur1"
    },
    "pterosaur2": {
        "urls": [
            "https://fastly.jsdelivr.net/gh/dhhruv/Chrome-Dino-Runner@master/assets/Bird/Bird2.png",
            "https://raw.githubusercontent.com/dhhruv/Chrome-Dino-Runner/master/assets/Bird/Bird2.png"
        ],
        "size": (24, 20),  # 翼龙2
        "color": (107, 114, 128),
        "fallback_type": "pterosaur2"
    },
    "cloud": {
        "urls": [
            "https://fastly.jsdelivr.net/gh/dhhruv/Chrome-Dino-Runner@master/assets/Other/Cloud.png",
            "https://raw.githubusercontent.com/dhhruv/Chrome-Dino-Runner/master/assets/Other/Cloud.png"
        ],
        "size": (32, 16),  # 漂浮白云
        "color": (255, 255, 255),
        "fallback_type": "cloud"
    }
}

def download_asset(urls, name):
    """尝试下载资产，如果失败返回 None"""
    for url in urls:
        try:
            print(f"正在从 {url} 下载 {name}...")
            # 设置 user-agent 防拦截
            req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
            with urllib.request.urlopen(req, timeout=5) as response:
                data = response.read()
                temp_path = f"scripts/temp_{name}.png"
                os.makedirs("scripts", exist_ok=True)
                with open(temp_path, "wb") as f:
                    f.write(data)
                return temp_path
        except Exception as e:
            print(f"下载失败: {url}, 错误: {e}")
    return None

def generate_fallback(fallback_type, size, color):
    """如果没网，用 PIL 在本地画一套像素矢量 Fallback Art"""
    w, h = size
    # 创建透明底画布
    img = Image.new("RGBA", size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    
    if fallback_type.startswith("bird"):
        # 小鸟主体
        draw.ellipse([2, 2, w-6, h-2], fill=color, outline=(234, 179, 8))
        # 嘴巴
        draw.polygon([(w-8, h//2-3), (w-2, h//2), (w-8, h//2+3)], fill=(249, 115, 22))
        # 眼睛
        draw.ellipse([w-12, 5, w-8, 9], fill=(255, 255, 255))
        draw.rectangle([w-10, 6, w-9, 7], fill=(0, 0, 0))
        # 翅膀 (不同振翅动作)
        wing_y_offset = -3 if fallback_type == "bird_up" else (3 if fallback_type == "bird_down" else 0)
        draw.ellipse([4, h//2 - 4 + wing_y_offset, w-18, h//2 + 4 + wing_y_offset], fill=(255, 255, 255), outline=(229, 231, 235))
        
    elif fallback_type.startswith("dino"):
        # 简化版像素恐龙轮廓
        # 头
        draw.rectangle([8, 2, w-2, 10], fill=color)
        draw.rectangle([w-5, 4, w-3, 6], fill=(255, 255, 255)) # 眼
        # 嘴
        draw.rectangle([12, 7, w-2, 8], fill=(255, 255, 255, 0)) # 嘴空隙
        # 身体
        draw.rectangle([2, 10, 14, 18], fill=color)
        # 尾巴
        draw.rectangle([0, 12, 3, 16], fill=color)
        # 腿 (奔跑腿交替)
        if fallback_type == "dino_run1":
            draw.rectangle([4, 18, 6, 23], fill=color) # 左立
            draw.rectangle([10, 18, 13, 20], fill=color) # 右曲
        elif fallback_type == "dino_run2":
            draw.rectangle([4, 18, 7, 20], fill=color) # 左曲
            draw.rectangle([10, 18, 12, 23], fill=color) # 右立
        elif fallback_type == "dino_jump":
            # 缩回双腿
            draw.rectangle([4, 18, 6, 20], fill=color)
            draw.rectangle([10, 18, 12, 20], fill=color)
        else: # dino_dead
            # 叉叉眼
            draw.rectangle([w-5, 4, w-3, 6], fill=(255, 255, 255, 0))
            draw.line([(w-5, 4), (w-3, 6)], fill=(239, 68, 110), width=1)
            draw.line([(w-3, 4), (w-5, 6)], fill=(239, 68, 110), width=1)
            draw.rectangle([4, 18, 6, 23], fill=color)
            draw.rectangle([10, 18, 12, 23], fill=color)
            
    elif fallback_type == "cactus":
        # 仙人掌
        draw.rectangle([w//2-2, 2, w//2+2, h-2], fill=color) # 主干
        draw.rectangle([2, h//2-4, w//2-2, h//2-1], fill=color) # 左臂横
        draw.rectangle([2, h//2-8, 5, h//2-4], fill=color) # 左臂竖
        draw.rectangle([w//2+2, h//2, w-3, h//2+3], fill=color) # 右臂横
        draw.rectangle([w-5, h//2-4, w-2, h//2], fill=color) # 右臂竖
        
    elif fallback_type.startswith("pterosaur"):
        # 翼龙
        draw.rectangle([6, h//2-4, 18, h//2+2], fill=color) # 身体
        draw.polygon([(18, h//2-2), (24, h//2), (18, h//2+2)], fill=(249, 115, 22)) # 喙
        draw.rectangle([2, h//2, 6, h//2+2], fill=color) # 尾巴
        # 拍打的翅膀
        if fallback_type == "pterosaur1":
            draw.polygon([(10, h//2-4), (14, 2), (18, h//2-4)], fill=color) # 翅膀向上
        else:
            draw.polygon([(10, h//2+2), (14, h-2), (18, h//2+2)], fill=color) # 翅膀向下
            
    elif fallback_type == "cloud":
        # 白云
        draw.ellipse([4, 4, 16, 12], fill=(255, 255, 255, 180))
        draw.ellipse([12, 2, w-4, 14], fill=(255, 255, 255, 180))
        draw.rectangle([10, 6, w-10, 12], fill=(255, 255, 255, 180))
        
    return img

def convert_to_rgb565a8(img, size):
    """转换为 LVGL RGB565A8 格式字节流"""
    # 缩放到指定大小
    img = img.resize(size, Image.Resampling.LANCZOS)
    img = img.convert("RGBA")
    w, h = size
    
    rgb565_bytes = bytearray()
    alpha_bytes = bytearray()
    
    for y in range(h):
        for x in range(w):
            r, g, b, a = img.getpixel((x, y))
            # RGB565 转换
            r5 = (r >> 3) & 0x1F
            g6 = (g >> 2) & 0x3F
            b5 = (b >> 3) & 0x1F
            rgb16 = (r5 << 11) | (g6 << 5) | b5
            # Little Endian 字节序
            rgb565_bytes.append(rgb16 & 0xFF)
            rgb565_bytes.append((rgb16 >> 8) & 0xFF)
            # Alpha 字节
            alpha_bytes.append(a)
            
    return rgb565_bytes + alpha_bytes

def main():
    print("===== 开始获取小游戏像素资源 =====")
    converted_images = {}
    
    for name, info in ASSETS.items():
        size = info["size"]
        temp_path = download_asset(info["urls"], name)
        
        if temp_path and os.path.exists(temp_path):
            try:
                img = Image.open(temp_path)
                print(f"下载成功！已成功打开图片 {name}。尺寸：{img.size}")
            except Exception as e:
                print(f"打开图片失败: {e}，将使用 Fallback。")
                img = generate_fallback(info["fallback_type"], size, info["color"])
        else:
            print(f"无法下载 {name}，正使用 Fallback 矢量绘制像素。")
            img = generate_fallback(info["fallback_type"], size, info["color"])
            
        # 转换为 RGB565A8
        img_bytes = convert_to_rgb565a8(img, size)
        converted_images[name] = {
            "bytes": img_bytes,
            "w": size[0],
            "h": size[1]
        }
        
        # 清理临时文件
        if temp_path and os.path.exists(temp_path):
            try:
                os.remove(temp_path)
            except:
                pass

    # 写入 C 文件
    print(f"正在写入 {TARGET_C_FILE}...")
    os.makedirs(os.path.dirname(TARGET_C_FILE), exist_ok=True)
    with open(TARGET_C_FILE, "w", encoding="utf-8") as fc:
        fc.write('#include "mini_game_sprites.h"\n\n')
        
        # 逐个写入字节流数组
        for name, data in converted_images.items():
            fc.write(f"const uint8_t img_{name}_map[] = {{\n  ")
            bytes_str = ", ".join(f"0x{b:02x}" for b in data["bytes"])
            # 按 16 个字节一行换行，保持 C 文件整洁
            bytes_list = bytes_str.split(", ")
            formatted_lines = []
            for i in range(0, len(bytes_list), 16):
                formatted_lines.append(", ".join(bytes_list[i:i+16]))
            fc.write(",\n  ".join(formatted_lines))
            fc.write("\n};\n\n")
            
            # 写入图片描述符结构体 (LVGL 9 风格)
            fc.write(f"const lv_image_dsc_t img_{name} = {{\n")
            fc.write("  .header.magic = LV_IMAGE_HEADER_MAGIC,\n")
            fc.write("  .header.cf = LV_COLOR_FORMAT_RGB565A8,\n")
            fc.write(f'  .header.stride = {data["w"] * 2},\n')
            fc.write(f'  .header.w = {data["w"]},\n')
            fc.write(f'  .header.h = {data["h"]},\n')
            fc.write(f'  .data_size = sizeof(img_{name}_map),\n')
            fc.write(f'  .data = img_{name}_map,\n')
            fc.write("};\n\n")

    # 写入 H 文件
    print(f"正在写入 {TARGET_H_FILE}...")
    with open(TARGET_H_FILE, "w", encoding="utf-8") as fh:
        fh.write('#ifndef MINI_GAME_SPRITES_H\n')
        fh.write('#define MINI_GAME_SPRITES_H\n\n')
        fh.write('#ifdef __has_include\n')
        fh.write('    #if __has_include("lvgl.h")\n')
        fh.write('        #ifndef LV_LVGL_H_INCLUDE_SIMPLE\n')
        fh.write('            #define LV_LVGL_H_INCLUDE_SIMPLE\n')
        fh.write('        #endif\n')
        fh.write('    #endif\n')
        fh.write('#endif\n\n')
        fh.write('#if defined(LV_LVGL_H_INCLUDE_SIMPLE)\n')
        fh.write('    #include "lvgl.h"\n')
        fh.write('#else\n')
        fh.write('    #include "lvgl/lvgl.h"\n')
        fh.write('#endif\n\n')
        fh.write('#include <stdint.h>\n\n')
        
        # 外部声明图片描述符
        for name in converted_images.keys():
            fh.write(f"extern const lv_image_dsc_t img_{name};\n")
            
        fh.write('\n#endif /* MINI_GAME_SPRITES_H */\n')

    print("===== 获取与转换完成 =====")

if __name__ == "__main__":
    main()
