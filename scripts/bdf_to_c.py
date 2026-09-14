#!/usr/bin/env python3

from dataclasses import dataclass
import argparse
import os


@dataclass
class Glyph:
    codepoint: int
    width: int
    height: int
    xoff: int
    yoff: int
    rows: list[int]


def parse_bdf(path: str) -> list[Glyph]:
    glyphs = []

    with open(path, "r", encoding="utf-8", errors="replace") as f:
        lines = [line.rstrip("\n") for line in f]

    i = 0
    while i < len(lines):
        if lines[i].startswith("STARTCHAR"):
            codepoint = None
            width = height = xoff = yoff = None
            bitmap_rows = []

            i += 1
            while i < len(lines) and lines[i] != "ENDCHAR":
                line = lines[i]

                if line.startswith("ENCODING "):
                    parts = line.split()
                    if len(parts) >= 2:
                        codepoint = int(parts[1])

                elif line.startswith("BBX "):
                    parts = line.split()
                    if len(parts) >= 5:
                        width = int(parts[1])
                        height = int(parts[2])
                        xoff = int(parts[3])
                        yoff = int(parts[4])

                elif line == "BITMAP":
                    i += 1
                    while i < len(lines) and lines[i] != "ENDCHAR":
                        row = lines[i].strip()
                        if row:
                            bitmap_rows.append(int(row, 16))
                        i += 1
                    continue

                i += 1

            if None not in (codepoint, width, height, xoff, yoff):
                glyphs.append(Glyph(
                    codepoint=codepoint,
                    width=width,
                    height=height,
                    xoff=xoff,
                    yoff=yoff,
                    rows=bitmap_rows,
                ))
        else:
            i += 1

    return glyphs


def glyph_to_5x8_rows(g: Glyph) -> list[int] | None:
    if g.codepoint <= 0:
        return None

    if g.width <= 0 or g.height <= 0:
        return None

    if g.height > 8:
        return None

    if g.width > 5:
        return None

    start_y = 8 - g.height
    if start_y < 0:
        return None

    out = [0] * 8

    # BDF BITMAP rows are padded to whole bytes.
    row_storage_bits = ((g.width + 7) // 8) * 8

    for src_y in range(min(g.height, len(g.rows))):
        dst_y = start_y + src_y
        if not (0 <= dst_y < 8):
            continue

        src_row = g.rows[src_y]
        dst_row = 0

        for src_x in range(g.width):
            dst_x = g.xoff + src_x
            if not (0 <= dst_x < 5):
                continue

            # Read from the padded bitmap width, not the logical glyph width.
            src_bit = row_storage_bits - 1 - src_x
            pixel = (src_row >> src_bit) & 1

            if pixel:
                out_bit = 4 - dst_x
                dst_row |= (1 << out_bit)

        out[dst_y] = dst_row

    return out


def make_fallback_box() -> list[int]:
    return [
        0x1F,
        0x11,
        0x11,
        0x11,
        0x11,
        0x11,
        0x11,
        0x1F,
    ]


def emit_c(font_name: str, glyphs: list[Glyph], header_path: str, source_font_filename: str) -> str:
    usable = []
    seen = set()

    for g in glyphs:
        if g.codepoint in seen:
            continue

        rows = glyph_to_5x8_rows(g)
        if rows is None:
            continue

        usable.append((g.codepoint, rows))
        seen.add(g.codepoint)

    usable.sort(key=lambda item: item[0])

    glyph_map = [0] * 256
    for index, (cp, _rows) in enumerate(usable, start=1):
        if cp <= 0xFF:
            glyph_map[cp] = index

    fallback = make_fallback_box()

    lines = []
    lines.append("/*")
    lines.append(f" {source_font_filename}")
    lines.append(" This file is generated from BDF font.")
    lines.append("")
    lines.append(" INSERT FONT LICENSE HERE")
    lines.append("*/")
    lines.append("")
    lines.append(f'#include "{header_path}"')
    lines.append("")

    lines.append(f"static KEK_glyph_5x8 {font_name}_glyphs[] = {{")
    lines.append(
        f"    {{ {{ {', '.join(f'0x{x:02X}' for x in fallback)} }}, 0x00000000 }},"
    )
    for cp, rows in usable:
        rows_text = ", ".join(f"0x{row:02X}" for row in rows)
        lines.append(f"    {{ {{ {rows_text} }}, 0x{cp:08X} }},")
    lines.append("};")
    lines.append("")

    lines.append(f"KEK_font_5x8 {font_name} = {{")
    lines.append("    .glyph_map = {")
    for i in range(0, 256, 8):
        chunk = glyph_map[i:i + 8]
        chunk_text = ", ".join(f"0x{x:08X}" for x in chunk)
        lines.append(f"        {chunk_text},")
    lines.append("    },")
    lines.append(f"    .glyphs = {font_name}_glyphs,")
    lines.append("    .fallback_glyph = {")
    lines.append(f"        .data = {{ {', '.join(f'0x{x:02X}' for x in fallback)} }},")
    lines.append("        .codepoint = 0x00000000,")
    lines.append("    },")
    lines.append(
        f"    .glyph_count = (uint32_t)(sizeof({font_name}_glyphs) / sizeof({font_name}_glyphs[0])),"
    )
    lines.append("};")

    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("input_bdf")
    parser.add_argument("output_c")
    parser.add_argument("--font-name", default="kek_font")
    parser.add_argument("--header", default="kek_font.h")
    args = parser.parse_args()

    glyphs = parse_bdf(args.input_bdf)
    source_font_filename = os.path.basename(args.input_bdf)
    c_code = emit_c(args.font_name, glyphs, args.header, source_font_filename)

    with open(args.output_c, "w", encoding="utf-8") as f:
        f.write(c_code)
        f.write("\n")

    print(f"Wrote {args.output_c}")


if __name__ == "__main__":
    main()