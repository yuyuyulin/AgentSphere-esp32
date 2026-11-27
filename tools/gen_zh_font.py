#!/usr/bin/env python3
"""Generate LVGL 9.x anti-aliased Simplified Chinese and ASCII fonts
from SourceHanSansSC-Normal.otf (SIL Open Font License 1.1).

Usage:
    python3 tools/gen_zh_font.py [--practice]
"""

import argparse
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

REPO_ROOT = Path(__file__).resolve().parent.parent
FONT_PATH = REPO_ROOT / "managed_components/lvgl__lvgl/scripts/generators/built_in_font/SourceHanSansSC-Normal.otf"
OUT_PATH = REPO_ROOT / "main/ui/fonts/font_zh_18.c"
FONT_SYMBOL = "font_zh_18"
FONT_SIZE = 18

# Full ASCII 0x20..0x7E plus Simplified Chinese UI vocabulary (current + upcoming voice/OTA UI)
ASCII_CHARS = "".join(chr(c) for c in range(0x20, 0x7F))
ZH_CHARS = (
    "（）：，。！？、·—…"
    "小安同学设备配对网升级固件启动中待机聆听思考说话已离线在线休眠"
    "就绪正在连接未联长按进入充电电池量传感器检测时间日期温度"
    "轻触屏幕或键热点模式请输入获取新过期请刷新剩余秒分时"
    "先在网页端解绑绑定接入语音检查状态录音并回放完成切换"
    "摇一静音息唤醒触摸服务繁忙后重试失败未知成功取消确认"
    "音量亮度高低温警告错误无法访问地址密码保存清空恢复出厂"
    "对话打断开始停止等待响应网络断开重连中扫描附近信号"
    "欢迎使用扫二维码或者手机浏览器打开输入位数字码"
    "下载校验安装重启版本最新无需进度条当前电量低请及时"
    "已静音点击任意处继续返回主界面设置关于本机"
    "供电姿态电源翻转抬起请求配对置未置关亮"
    "一二三四五六七八九十零闭麦英语练习退播报答再’译收"
)


def practice_chars() -> set[str]:
    # GB2312 levels 1 and 2 cover 6,763 commonly used Simplified Chinese characters.
    chars = set(ASCII_CHARS + ZH_CHARS + "“”‘《》")
    for high in range(0xB0, 0xF8):
        for low in range(0xA1, 0xFF):
            chars.update(bytes([high, low]).decode("gb2312", errors="ignore"))
    return chars


def render_glyph(font: ImageFont.FreeTypeFont, ch: str, ascent: int, bpp: int):
    # Measure tight bounding box using Pillow's anchor-free getbbox
    bbox = font.getbbox(ch)
    adv_px = int(round(font.getlength(ch)))
    if bbox is None:
        return {
            "cp": ord(ch),
            "bitmap": b"",
            "adv_w": max(adv_px, 4) * 16,
            "box_w": 0,
            "box_h": 0,
            "ofs_x": 0,
            "ofs_y": 0,
        }

    left, top, right, bottom = bbox
    w = max(right - left, 0)
    h = max(bottom - top, 0)
    if w == 0 or h == 0:
        return {
            "cp": ord(ch),
            "bitmap": b"",
            "adv_w": max(adv_px, 4) * 16,
            "box_w": 0,
            "box_h": 0,
            "ofs_x": 0,
            "ofs_y": 0,
        }

    img = Image.new("L", (w, h), 0)
    draw = ImageDraw.Draw(img)
    draw.text((-left, -top), ch, fill=255, font=font)

    pixels = img.tobytes()
    packed = bytearray((len(pixels) * bpp + 7) // 8)
    for i, pixel in enumerate(pixels):
        offset = i * bpp
        packed[offset // 8] |= (pixel >> (8 - bpp)) << (8 - bpp - offset % 8)

    # LVGL ofs_y is distance from baseline UP to the bottom of the glyph box
    ofs_y = ascent - bottom
    return {
        "cp": ord(ch),
        "bitmap": bytes(packed),
        "adv_w": max(adv_px, w + 1) * 16,
        "box_w": w,
        "box_h": h,
        "ofs_x": left,
        "ofs_y": ofs_y,
    }


def generate(chars, out_path, size, symbol, bpp, bitmap_path=None):
    if not FONT_PATH.exists():
        raise FileNotFoundError(f"Source font not found: {FONT_PATH}")

    font = ImageFont.truetype(str(FONT_PATH), size)
    ascent, descent = font.getmetrics()
    line_height = ascent + descent
    base_line = descent

    glyphs = [render_glyph(font, ch, ascent, bpp) for ch in sorted(chars)]

    # Build sparse cmaps grouped so (cp - range_start) fits in uint16_t
    # Group 0: ASCII contiguous range 0x20..0x7E
    ascii_glyphs = [g for g in glyphs if 0x20 <= g["cp"] <= 0x7E]
    non_ascii = [g for g in glyphs if g["cp"] > 0x7E]

    # Build bitmap blob and glyph descriptors (index 0 is reserved by LVGL)
    bitmap_bytes = bytearray()
    dsc_lines = ["    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0}, /* id=0 reserved */"]

    ordered = ascii_glyphs + non_ascii
    for g in ordered:
        b_idx = len(bitmap_bytes)
        bitmap_bytes.extend(g["bitmap"])
        dsc_lines.append(
            f"    {{.bitmap_index = {b_idx}, .adv_w = {g['adv_w']}, "
            f".box_w = {g['box_w']}, .box_h = {g['box_h']}, "
            f".ofs_x = {g['ofs_x']}, .ofs_y = {g['ofs_y']}}}, /* U+{g['cp']:04X} */"
        )

    # Non-ASCII unicode_list (all fit in uint16_t relative to first non-ASCII cp)
    non_ascii_start = non_ascii[0]["cp"]
    non_ascii_span = non_ascii[-1]["cp"] - non_ascii_start + 1
    assert non_ascii_span <= 65535
    unicode_offsets = [g["cp"] - non_ascii_start for g in non_ascii]

    assert len(bitmap_bytes) < (1 << 20), "LVGL glyph bitmap offsets must fit in 20 bits"
    if bitmap_path:
        bitmap_path.write_bytes(bitmap_bytes)
        binary_symbol = bitmap_path.name.replace(".", "_")
        bitmap_code = f'extern const uint8_t glyph_bitmap[] asm("_binary_{binary_symbol}_start");'
    else:
        bm_lines = []
        for i in range(0, len(bitmap_bytes), 16):
            chunk = ", ".join(f"0x{b:02x}" for b in bitmap_bytes[i:i + 16])
            bm_lines.append(f"    {chunk},")
        bitmap_code = "static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {\n"
        bitmap_code += "\n".join(bm_lines) + "\n};"

    ul_lines = []
    for i in range(0, len(unicode_offsets), 12):
        chunk = ", ".join(f"0x{o:04x}" for o in unicode_offsets[i:i + 12])
        ul_lines.append(f"    {chunk},")

    out_path.parent.mkdir(parents=True, exist_ok=True)
    code = f"""/* Auto-generated by tools/gen_zh_font.py from SourceHanSansSC-Normal.otf (SIL OFL 1.1)
 * Size: {size}px, {bpp}bpp anti-aliased, {len(ordered)} glyphs
 */
#include "lvgl.h"

#if LV_USE_FONT_COMPRESSED == 0 || 1

{bitmap_code}

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {{
{chr(10).join(dsc_lines)}
}};

static const uint16_t unicode_list_1[] = {{
{chr(10).join(ul_lines)}
}};

static const lv_font_fmt_txt_cmap_t cmaps[] = {{
    {{
        .range_start = 0x20,
        .range_length = {len(ascii_glyphs)},
        .glyph_id_start = 1,
        .unicode_list = NULL,
        .glyph_id_ofs_list = NULL,
        .list_length = 0,
        .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY,
    }},
    {{
        .range_start = 0x{non_ascii_start:04x},
        .range_length = {non_ascii_span},
        .glyph_id_start = {1 + len(ascii_glyphs)},
        .unicode_list = unicode_list_1,
        .glyph_id_ofs_list = NULL,
        .list_length = {len(non_ascii)},
        .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY,
    }},
}};

static const lv_font_fmt_txt_dsc_t font_dsc = {{
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 2,
    .bpp = {bpp},
    .kern_classes = 0,
    .bitmap_format = 0,
}};

const lv_font_t {symbol} = {{
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,
    .line_height = {line_height},
    .base_line = {base_line},
#if LV_VERSION_CHECK(9, 6, 0) || LVGL_VERSION_MAJOR >= 10
    .cap_height = {int(round(size * 0.72))},
    .x_height = {int(round(size * 0.52))},
#endif
    .subpx = LV_FONT_SUBPX_NONE,
    .underline_position = -2,
    .underline_thickness = 1,
#if LV_VERSION_CHECK(9, 3, 0)
    .static_bitmap = 1,
#endif
    .dsc = &font_dsc,
    .fallback = NULL,
    .user_data = NULL,
}};

#endif
"""
    out_path.write_text(code, encoding="utf-8")
    print(f"Generated {out_path} ({len(ordered)} glyphs, {len(bitmap_bytes)} bitmap bytes)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--practice", action="store_true", help="also generate the larger caption font")
    args = parser.parse_args()
    generate(set(ASCII_CHARS + ZH_CHARS), OUT_PATH, FONT_SIZE, FONT_SYMBOL, 4)
    if args.practice:
        generate(practice_chars(), OUT_PATH.with_name("font_practice_zh_16.c"), 16,
                 "font_practice_zh_16", 2, OUT_PATH.with_name("practice_zh_bitmap.a2"))


if __name__ == "__main__":
    main()
