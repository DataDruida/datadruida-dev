"""Genera fuentes LVGL compiladas (formato fmt_txt, 4 bpp) con acentos y ñ.

Las Montserrat que trae LVGL son sólo ASCII. En vez de dibujar la letra en el
equipo con TinyTTF (que hacía reiniciar la 4.3B al aparecer muchas letras
nuevas de golpe: lista de redes, teclado), se rasterizan acá con Pillow y
quedan en flash, igual que las de LVGL. Los íconos (LV_SYMBOL_*) salen de la
Montserrat de LVGL del mismo tamaño, que se usa como fallback.

  python idf/tools/gen_fonts.py <Montserrat-Medium.ttf> <carpeta_salida> 16 20 28
"""
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

EXTRA = "¡¿°·•…–—“”‘’áéíóúÁÉÍÓÚñÑüÜ"
CODES = sorted(set(range(0x20, 0x7F)) | {ord(c) for c in EXTRA})


def glyph(font, ch):
    adv = font.getlength(ch)
    x0, y0, x1, y1 = font.getbbox(ch, anchor="ls")
    w, h = max(0, x1 - x0), max(0, y1 - y0)
    px = []
    if w and h:
        img = Image.new("L", (w, h), 0)
        ImageDraw.Draw(img).text((-x0, -y0), ch, font=font, fill=255, anchor="ls")
        data = img.get_flattened_data() if hasattr(img, "get_flattened_data") else img.getdata()
        px = [v >> 4 for v in data]
        if not any(px):
            w = h = 0
            px = []
    # LVGL: ofs_y = del baseline al borde inferior de la caja, positivo hacia arriba
    return dict(adv=round(adv * 16), w=w, h=h, ox=x0, oy=-y1, px=px)


def pack4(px):
    out = bytearray()
    for i in range(0, len(px), 2):
        hi = px[i]
        lo = px[i + 1] if i + 1 < len(px) else 0
        out.append((hi << 4) | lo)
    return out


def gen(ttf, size, outdir):
    font = ImageFont.truetype(str(ttf), size)
    name = f"dd_font_{size}"
    gl = [glyph(font, chr(c)) for c in CODES]
    bitmap, dsc, idx = bytearray(), [], 0
    for g in gl:
        b = pack4(g["px"])
        dsc.append((idx, g))
        bitmap += b
        idx += len(b)
    tops = [g["oy"] + g["h"] for g in gl if g["h"]]
    bots = [g["oy"] for g in gl if g["h"]]
    line_h, base = max(tops) - min(bots), -min(bots)

    ascii_n = 0x7F - 0x20
    sparse = CODES[ascii_n:]
    start = sparse[0]
    L = []
    L.append(f"/* {name}: Montserrat Medium {size} px, 4 bpp. Generado por idf/tools/gen_fonts.py — no editar. */")
    L.append('#include "lvgl.h"\n')
    L.append("static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {")
    for i in range(0, len(bitmap), 20):
        L.append("    " + ", ".join(f"0x{v:02x}" for v in bitmap[i:i + 20]) + ",")
    L.append("    0x00\n};\n")
    L.append("static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {")
    L.append("    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0},")
    for (bi, g), c in zip(dsc, CODES):
        L.append(f"    {{.bitmap_index = {bi}, .adv_w = {g['adv']}, .box_w = {g['w']}, .box_h = {g['h']}, "
                 f".ofs_x = {g['ox']}, .ofs_y = {g['oy']}}}, /* U+{c:04X} */")
    L.append("};\n")
    L.append("static const uint16_t unicode_list_1[] = {")
    L.append("    " + ", ".join(f"0x{c - start:x}" for c in sparse))
    L.append("};\n")
    L.append("static const lv_font_fmt_txt_cmap_t cmaps[] = {")
    L.append(f"    {{.range_start = 32, .range_length = {ascii_n}, .glyph_id_start = 1, .unicode_list = NULL,"
             " .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY},")
    L.append(f"    {{.range_start = {start}, .range_length = {sparse[-1] - start + 1}, .glyph_id_start = {ascii_n + 1},"
             f" .unicode_list = unicode_list_1, .glyph_id_ofs_list = NULL, .list_length = {len(sparse)},"
             " .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY},")
    L.append("};\n")
    L.append("static const lv_font_fmt_txt_dsc_t font_dsc = {")
    L.append("    .glyph_bitmap = glyph_bitmap, .glyph_dsc = glyph_dsc, .cmaps = cmaps,")
    L.append("    .kern_dsc = NULL, .kern_scale = 0, .cmap_num = 2, .bpp = 4, .kern_classes = 0, .bitmap_format = 0,")
    L.append("};\n")
    L.append(f"const lv_font_t {name} = {{")
    L.append("    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,")
    L.append("    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,")
    L.append(f"    .line_height = {line_h},")
    L.append(f"    .base_line = {base},")
    L.append("    .subpx = LV_FONT_SUBPX_NONE,")
    L.append("    .underline_position = -1,")
    L.append("    .underline_thickness = 1,")
    L.append("    .dsc = &font_dsc,")
    L.append(f"    .fallback = &lv_font_montserrat_{size},  /* íconos LV_SYMBOL_* */")
    L.append("};")
    (Path(outdir) / f"{name}.c").write_text("\n".join(L) + "\n", encoding="utf-8")
    print(f"{name}: {len(CODES)} glifos, {len(bitmap)} bytes, line_height {line_h}, base_line {base}")


if __name__ == "__main__":
    ttf, outdir, *sizes = sys.argv[1:]
    Path(outdir).mkdir(parents=True, exist_ok=True)
    for s in sizes:
        gen(ttf, int(s), outdir)
