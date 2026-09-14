#!/usr/bin/env python3
import struct
import sys
from pathlib import Path

KIF_MAGIC = b"KIMG"
KIF_VERSION = 1
KIF_ENCODING_RAW = 0

BMP_FILE_HEADER_STRUCT = struct.Struct("<2sIHHI")
BMP_INFO_HEADER_SIZE_STRUCT = struct.Struct("<I")
BMP_INFO_HEADER_STRUCT = struct.Struct("<IiiHHIIiiII")
BMP_PALETTE_ENTRY_STRUCT = struct.Struct("<BBBB")  # B, G, R, reserved

KIF_HEADER_STRUCT = struct.Struct("<4sHHHB5x")
# magic[4], version(u16), width(u16), height(u16), encoding(u8), reserved[5]


class ConversionError(Exception):
    pass


def read_exact(f, n: int) -> bytes:
    data = f.read(n)
    if len(data) != n:
        raise ConversionError(f"Unexpected EOF while reading {n} bytes")
    return data


def rgb666_to_rgb888(v: int) -> int:
    return round(v * 255 / 63)


def palette666_to_888(palette666: list[tuple[int, int, int]]) -> list[tuple[int, int, int]]:
    return [
        (rgb666_to_rgb888(r), rgb666_to_rgb888(g), rgb666_to_rgb888(b))
        for r, g, b in palette666
    ]


# Alpha column from your C array is ignored.
ENGINE_PALETTE_666 = [
    (0, 0, 0), (0, 0, 42), (0, 42, 0), (0, 42, 42),
    (42, 0, 0), (42, 0, 42), (42, 21, 0), (42, 42, 42),
    (21, 21, 21), (21, 21, 63), (21, 63, 21), (21, 63, 63),
    (63, 21, 21), (63, 21, 63), (63, 63, 21), (63, 63, 63),
    (0, 0, 0), (5, 5, 5), (8, 8, 8), (11, 11, 11),
    (14, 14, 14), (17, 17, 17), (20, 20, 20), (24, 24, 24),
    (28, 28, 28), (32, 32, 32), (36, 36, 36), (40, 40, 40),
    (45, 45, 45), (50, 50, 50), (56, 56, 56), (63, 63, 63),
    (0, 0, 63), (16, 0, 63), (31, 0, 63), (47, 0, 63),
    (63, 0, 63), (63, 0, 47), (63, 0, 31), (63, 0, 16),
    (63, 0, 0), (63, 16, 0), (63, 31, 0), (63, 47, 0),
    (63, 63, 0), (47, 63, 0), (31, 63, 0), (16, 63, 0),
    (0, 63, 0), (0, 63, 16), (0, 63, 31), (0, 63, 47),
    (0, 63, 63), (0, 47, 63), (0, 31, 63), (0, 16, 63),
    (31, 31, 63), (39, 31, 63), (47, 31, 63), (55, 31, 63),
    (63, 31, 63), (63, 31, 55), (63, 31, 47), (63, 31, 39),
    (63, 31, 31), (63, 39, 31), (63, 47, 31), (63, 55, 31),
    (63, 63, 31), (55, 63, 31), (47, 63, 31), (39, 63, 31),
    (31, 63, 31), (31, 63, 39), (31, 63, 47), (31, 63, 55),
    (31, 63, 63), (31, 55, 63), (31, 47, 63), (31, 39, 63),
    (45, 45, 63), (49, 45, 63), (54, 45, 63), (58, 45, 63),
    (63, 45, 63), (63, 45, 58), (63, 45, 54), (63, 45, 49),
    (63, 45, 45), (63, 49, 45), (63, 54, 45), (63, 58, 45),
    (63, 63, 45), (58, 63, 45), (54, 63, 45), (49, 63, 45),
    (45, 63, 45), (45, 63, 49), (45, 63, 54), (45, 63, 58),
    (45, 63, 63), (45, 58, 63), (45, 54, 63), (45, 49, 63),
    (0, 0, 28), (7, 0, 28), (14, 0, 28), (21, 0, 28),
    (28, 0, 28), (28, 0, 21), (28, 0, 14), (28, 0, 7),
    (28, 0, 0), (28, 7, 0), (28, 14, 0), (28, 21, 0),
    (28, 28, 0), (21, 28, 0), (14, 28, 0), (7, 28, 0),
    (0, 28, 0), (0, 28, 7), (0, 28, 14), (0, 28, 21),
    (0, 28, 28), (0, 21, 28), (0, 14, 28), (0, 7, 28),
    (14, 14, 28), (17, 14, 28), (21, 14, 28), (24, 14, 28),
    (28, 14, 28), (28, 14, 24), (28, 14, 21), (28, 14, 17),
    (28, 14, 14), (28, 17, 14), (28, 21, 14), (28, 24, 14),
    (28, 28, 14), (24, 28, 14), (21, 28, 14), (17, 28, 14),
    (14, 28, 14), (14, 28, 17), (14, 28, 21), (14, 28, 24),
    (14, 28, 28), (14, 24, 28), (14, 21, 28), (14, 17, 28),
    (20, 20, 28), (22, 20, 28), (24, 20, 28), (26, 20, 28),
    (28, 20, 28), (28, 20, 26), (28, 20, 24), (28, 20, 22),
    (28, 20, 20), (28, 22, 20), (28, 24, 20), (28, 26, 20),
    (28, 28, 20), (26, 28, 20), (24, 28, 20), (22, 28, 20),
    (20, 28, 20), (20, 28, 22), (20, 28, 24), (20, 28, 26),
    (20, 28, 28), (20, 26, 28), (20, 24, 28), (20, 22, 28),
    (0, 0, 16), (4, 0, 16), (8, 0, 16), (12, 0, 16),
    (16, 0, 16), (16, 0, 12), (16, 0, 8), (16, 0, 4),
    (16, 0, 0), (16, 4, 0), (16, 8, 0), (16, 12, 0),
    (16, 16, 0), (12, 16, 0), (8, 16, 0), (4, 16, 0),
    (0, 16, 0), (0, 16, 4), (0, 16, 8), (0, 16, 12),
    (0, 16, 16), (0, 12, 16), (0, 8, 16), (0, 4, 16),
    (8, 8, 16), (10, 8, 16), (12, 8, 16), (14, 8, 16),
    (16, 8, 16), (16, 8, 14), (16, 8, 12), (16, 8, 10),
    (16, 8, 8), (16, 10, 8), (16, 12, 8), (16, 14, 8),
    (16, 16, 8), (14, 16, 8), (12, 16, 8), (10, 16, 8),
    (8, 16, 8), (8, 16, 10), (8, 16, 12), (8, 16, 14),
    (8, 16, 16), (8, 14, 16), (8, 12, 16), (8, 10, 16),
    (11, 11, 16), (12, 11, 16), (13, 11, 16), (15, 11, 16),
    (16, 11, 16), (16, 11, 15), (16, 11, 13), (16, 11, 12),
    (16, 11, 11), (16, 12, 11), (16, 13, 11), (16, 15, 11),
    (16, 16, 11), (15, 16, 11), (13, 16, 11), (12, 16, 11),
    (11, 16, 11), (11, 16, 12), (11, 16, 13), (11, 16, 15),
    (11, 16, 16), (11, 15, 16), (11, 13, 16), (11, 12, 16),
    (0, 0, 0), (0, 0, 0), (0, 0, 0), (0, 0, 0),
    (0, 0, 0), (0, 0, 0), (0, 0, 0), (0, 0, 0),
]

if len(ENGINE_PALETTE_666) != 256:
    raise RuntimeError(f"Expected 256 palette entries, got {len(ENGINE_PALETTE_666)}")

ENGINE_PALETTE_888 = palette666_to_888(ENGINE_PALETTE_666)


def build_exact_palette_lookup_first_match(
    palette888: list[tuple[int, int, int]]
) -> dict[tuple[int, int, int], int]:
    lookup: dict[tuple[int, int, int], int] = {}
    for i, rgb in enumerate(palette888):
        lookup.setdefault(rgb, i)
    return lookup


def nearest_palette_index(
    rgb: tuple[int, int, int],
    palette888: list[tuple[int, int, int]],
) -> int:
    r, g, b = rgb
    best_index = 0
    best_dist = None
    for i, (pr, pg, pb) in enumerate(palette888):
        dr = r - pr
        dg = g - pg
        db = b - pb
        dist = dr * dr + dg * dg + db * db
        if best_dist is None or dist < best_dist:
            best_dist = dist
            best_index = i
    return best_index


def print_unique_remap_info(
    bmp_palette: list[tuple[int, int, int]],
    remap_table: list[int],
    engine_palette_888: list[tuple[int, int, int]],
) -> None:
    groups: dict[tuple[tuple[int, int, int], int], list[int]] = {}

    for bmp_index, bmp_rgb in enumerate(bmp_palette):
        engine_index = remap_table[bmp_index]
        key = (bmp_rgb, engine_index)
        groups.setdefault(key, []).append(bmp_index)

    print("Unique palette remaps:")
    for (bmp_rgb, engine_index), bmp_indices in groups.items():
        engine_rgb = engine_palette_888[engine_index]
        print(
            f"  BMP {bmp_rgb} at indices {bmp_indices} -> "
            f"engine[{engine_index}] {engine_rgb}"
        )


def load_bmp_8bit_and_remap(
    path: Path,
    *,
    nearest: bool = False,
) -> tuple[int, int, bytearray]:
    exact_lookup = build_exact_palette_lookup_first_match(ENGINE_PALETTE_888)

    with path.open("rb") as f:
        file_header = read_exact(f, BMP_FILE_HEADER_STRUCT.size)
        bf_type, bf_size, bf_reserved1, bf_reserved2, bf_off_bits = BMP_FILE_HEADER_STRUCT.unpack(file_header)

        if bf_type != b"BM":
            raise ConversionError("Not a BMP file")

        dib_size_data = read_exact(f, BMP_INFO_HEADER_SIZE_STRUCT.size)
        (dib_size,) = BMP_INFO_HEADER_SIZE_STRUCT.unpack(dib_size_data)

        if dib_size != 40:
            raise ConversionError(
                f"Unsupported BMP DIB header size: {dib_size}. "
                "Only BITMAPINFOHEADER (40 bytes) is supported."
            )

        rest = read_exact(f, dib_size - 4)
        dib = dib_size_data + rest

        (
            bi_size,
            bi_width,
            bi_height,
            bi_planes,
            bi_bit_count,
            bi_compression,
            bi_size_image,
            bi_x_pels_per_meter,
            bi_y_pels_per_meter,
            bi_clr_used,
            bi_clr_important,
        ) = BMP_INFO_HEADER_STRUCT.unpack(dib)

        if bi_planes != 1:
            raise ConversionError(f"Unsupported BMP planes count: {bi_planes}")

        if bi_bit_count != 8:
            raise ConversionError(f"Unsupported BMP bit depth: {bi_bit_count}. Expected 8-bit BMP.")

        if bi_compression != 0:
            raise ConversionError(
                f"Unsupported BMP compression: {bi_compression}. "
                "Only uncompressed 8-bit BMP (BI_RGB) is supported."
            )

        width = bi_width
        height_signed = bi_height
        if width <= 0 or height_signed == 0:
            raise ConversionError(f"Invalid BMP dimensions: {width}x{height_signed}")

        top_down = height_signed < 0
        height = abs(height_signed)

        if width > 1024 or height > 1024:
            raise ConversionError(f"BMP dimensions {width}x{height} exceed KIF limit of 1024x1024")

        palette_entries = bi_clr_used if bi_clr_used != 0 else 256
        if palette_entries > 256:
            raise ConversionError(f"Invalid BMP palette size: {palette_entries}")

        bmp_palette: list[tuple[int, int, int]] = []
        for _ in range(palette_entries):
            b, g, r, _reserved = BMP_PALETTE_ENTRY_STRUCT.unpack(
                read_exact(f, BMP_PALETTE_ENTRY_STRUCT.size)
            )
            bmp_palette.append((r, g, b))

        if len(bmp_palette) < 256:
            bmp_palette.extend([(0, 0, 0)] * (256 - len(bmp_palette)))

        remap_table = [0] * 256
        for bmp_index, rgb in enumerate(bmp_palette):
            if rgb in exact_lookup:
                remap_table[bmp_index] = exact_lookup[rgb]
            else:
                if not nearest:
                    raise ConversionError(
                        f"BMP palette color at index {bmp_index} = {rgb} "
                        "does not exist in engine palette exactly. "
                        "Use --nearest to allow approximate remapping."
                    )
                remap_table[bmp_index] = nearest_palette_index(rgb, ENGINE_PALETTE_888)

        print_unique_remap_info(bmp_palette, remap_table, ENGINE_PALETTE_888)

        f.seek(bf_off_bits)

        row_stride = ((width + 3) // 4) * 4
        rows: list[bytearray] = []

        for _ in range(height):
            src_row = read_exact(f, row_stride)[:width]
            dst_row = bytearray(remap_table[p] for p in src_row)
            rows.append(dst_row)

        if not top_down:
            rows.reverse()

        pixels = bytearray(width * height)
        for y in range(height):
            start = y * width
            pixels[start:start + width] = rows[y]

        return width, height, pixels


def save_kif(path: Path, width: int, height: int, pixels: bytearray) -> None:
    if not (1 <= width <= 1024 and 1 <= height <= 1024):
        raise ConversionError(f"Invalid KIF size: {width}x{height}")

    if len(pixels) != width * height:
        raise ConversionError(
            f"Pixel buffer size mismatch: got {len(pixels)}, expected {width * height}"
        )

    header = KIF_HEADER_STRUCT.pack(
        KIF_MAGIC,
        KIF_VERSION,
        width,
        height,
        KIF_ENCODING_RAW,
    )

    with path.open("wb") as f:
        f.write(header)
        f.write(pixels)


def convert_bmp_to_kif(src_path: Path, dst_path: Path, *, nearest: bool = False) -> None:
    width, height, pixels = load_bmp_8bit_and_remap(src_path, nearest=nearest)
    save_kif(dst_path, width, height, pixels)


def main() -> int:
    args = sys.argv[1:]
    nearest = False

    if "--nearest" in args:
        nearest = True
        args.remove("--nearest")

    if len(args) not in (1, 2):
        print(
            f"Usage: {Path(sys.argv[0]).name} [--nearest] input.bmp [output.kif]",
            file=sys.stderr,
        )
        return 1

    src_path = Path(args[0])
    dst_path = Path(args[1]) if len(args) == 2 else src_path.with_suffix(".kif")

    try:
        convert_bmp_to_kif(src_path, dst_path, nearest=nearest)
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1

    print(f"Converted {src_path} -> {dst_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())