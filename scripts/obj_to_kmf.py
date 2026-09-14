#!/usr/bin/env python3
import argparse
import shlex
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

from bmp_to_kif import ConversionError as TextureConversionError
from bmp_to_kif import ENGINE_PALETTE_888
from bmp_to_kif import build_exact_palette_lookup_first_match
from bmp_to_kif import convert_bmp_to_kif
from bmp_to_kif import nearest_palette_index

try:
    from PIL import Image
except ImportError:
    Image = None

KMDL_MAGIC = b"KMDL"
KMDL_VERSION = 1
KMDL_INDEX_NONE = 0xFFFF

KMDL_FLAG_HAS_FACE_COLORS = 1 << 0
KMDL_FLAG_HAS_TEXTURE = 1 << 1

KMDL_HEADER_STRUCT = struct.Struct("<4sHHHHHHHBx")
KMDL_VERTEX_STRUCT = struct.Struct("<fff")
KMDL_UV_STRUCT = struct.Struct("<ff")
KMDL_FACE_VERTEX_STRUCT = struct.Struct("<HHH")


@dataclass(frozen=True)
class ObjFaceVertex:
    vertex: int
    uv: int | None
    normal: int | None


@dataclass(frozen=True)
class ObjTriangle:
    vertices: tuple[ObjFaceVertex, ObjFaceVertex, ObjFaceVertex]
    material: str | None


class ObjConversionError(Exception):
    pass


def compact_entries(entries: list[tuple]) -> tuple[list[tuple], dict[int, int]]:
    unique_entries: list[tuple] = []
    remap: dict[int, int] = {}
    first_index_by_value: dict[tuple, int] = {}

    for index, entry in enumerate(entries):
        mapped_index = first_index_by_value.get(entry)
        if mapped_index is None:
            mapped_index = len(unique_entries)
            unique_entries.append(entry)
            first_index_by_value[entry] = mapped_index
        remap[index] = mapped_index

    return unique_entries, remap


def remap_triangle_indices(
    triangles: list[ObjTriangle],
    vertex_remap: dict[int, int],
    uv_remap: dict[int, int],
    normal_remap: dict[int, int],
) -> list[ObjTriangle]:
    remapped: list[ObjTriangle] = []

    for triangle in triangles:
        remapped_vertices: list[ObjFaceVertex] = []
        for vertex in triangle.vertices:
            remapped_vertices.append(
                ObjFaceVertex(
                    vertex=vertex_remap[vertex.vertex],
                    uv=None if vertex.uv is None else uv_remap[vertex.uv],
                    normal=None if vertex.normal is None else normal_remap[vertex.normal],
                )
            )
        remapped.append(
            ObjTriangle(
                vertices=(
                    remapped_vertices[0],
                    remapped_vertices[1],
                    remapped_vertices[2],
                ),
                material=triangle.material,
            )
        )

    return remapped


def strip_uvs_for_non_primary_texture_faces(
    triangles: list[ObjTriangle],
    textured_materials: set[str] | None,
) -> list[ObjTriangle]:
    if textured_materials is None:
        return triangles

    filtered: list[ObjTriangle] = []
    for triangle in triangles:
        if triangle.material in textured_materials:
            filtered.append(triangle)
            continue

        filtered.append(
            ObjTriangle(
                vertices=tuple(
                    ObjFaceVertex(vertex=v.vertex, uv=None, normal=v.normal)
                    for v in triangle.vertices
                ),
                material=triangle.material,
            )
        )

    return filtered


def parse_obj_index(token: str, count: int) -> int:
    value = int(token)
    if value == 0:
        raise ObjConversionError("OBJ indices are 1-based; got 0")
    if value < 0:
        value = count + value + 1
    if value < 1 or value > count:
        raise ObjConversionError(f"OBJ index {token} is out of range for count {count}")
    return value - 1


def parse_face_vertex(token: str, v_count: int, vt_count: int, vn_count: int) -> ObjFaceVertex:
    parts = token.split("/")
    if len(parts) > 3:
        raise ObjConversionError(f"Unsupported face vertex token: {token}")

    vertex = parse_obj_index(parts[0], v_count)

    uv = None
    normal = None

    if len(parts) >= 2 and parts[1] != "":
        uv = parse_obj_index(parts[1], vt_count)
    if len(parts) >= 3 and parts[2] != "":
        normal = parse_obj_index(parts[2], vn_count)

    return ObjFaceVertex(vertex=vertex, uv=uv, normal=normal)


def triangulate(face_vertices: list[ObjFaceVertex], material: str | None) -> list[ObjTriangle]:
    if len(face_vertices) < 3:
        raise ObjConversionError("Face must have at least 3 vertices")

    triangles: list[ObjTriangle] = []
    for i in range(1, len(face_vertices) - 1):
        triangles.append(
            ObjTriangle(
                vertices=(face_vertices[0], face_vertices[i], face_vertices[i + 1]),
                material=material,
            )
        )
    return triangles


def parse_obj(path: Path) -> tuple[
    list[tuple[float, float, float]],
    list[tuple[float, float]],
    list[tuple[float, float, float]],
    list[ObjTriangle],
    list[Path],
]:
    vertices: list[tuple[float, float, float]] = []
    uvs: list[tuple[float, float]] = []
    normals: list[tuple[float, float, float]] = []
    triangles: list[ObjTriangle] = []
    mtllibs: list[Path] = []
    current_material: str | None = None

    with path.open("r", encoding="utf-8") as f:
        for lineno, raw_line in enumerate(f, start=1):
            line = raw_line.strip()
            if not line or line.startswith("#"):
                continue

            try:
                parts = shlex.split(line, comments=False, posix=True)
            except ValueError as exc:
                raise ObjConversionError(f"{path}:{lineno}: {exc}") from exc

            if not parts:
                continue

            head, *tail = parts

            if head == "v":
                if len(tail) < 3:
                    raise ObjConversionError(f"{path}:{lineno}: vertex needs 3 components")
                vertices.append((float(tail[0]), float(tail[1]), float(tail[2])))
            elif head == "vt":
                if len(tail) < 2:
                    raise ObjConversionError(f"{path}:{lineno}: uv needs 2 components")
                uvs.append((float(tail[0]), float(tail[1])))
            elif head == "vn":
                if len(tail) < 3:
                    raise ObjConversionError(f"{path}:{lineno}: normal needs 3 components")
                normals.append((float(tail[0]), float(tail[1]), float(tail[2])))
            elif head == "f":
                if len(tail) < 3:
                    raise ObjConversionError(f"{path}:{lineno}: face needs at least 3 vertices")
                face_vertices = [
                    parse_face_vertex(token, len(vertices), len(uvs), len(normals))
                    for token in tail
                ]
                triangles.extend(triangulate(face_vertices, current_material))
            elif head == "usemtl":
                current_material = tail[0] if tail else None
            elif head == "mtllib":
                for item in tail:
                    mtllibs.append((path.parent / item).resolve())

    if not vertices:
        raise ObjConversionError(f"{path}: no vertices found")
    if not triangles:
        raise ObjConversionError(f"{path}: no faces found")

    return vertices, uvs, normals, triangles, mtllibs


def transform_uvs(
    uvs: list[tuple[float, float]],
    *,
    keep_uvs: bool,
) -> list[tuple[float, float]]:
    if keep_uvs:
        return uvs

    return [(u, 1.0 - v) for u, v in uvs]


def parse_mtl_texture_map(path: Path) -> dict[str, Path]:
    result: dict[str, Path] = {}
    current_material: str | None = None

    with path.open("r", encoding="utf-8") as f:
        for lineno, raw_line in enumerate(f, start=1):
            line = raw_line.strip()
            if not line or line.startswith("#"):
                continue

            try:
                parts = shlex.split(line, comments=False, posix=True)
            except ValueError as exc:
                raise ObjConversionError(f"{path}:{lineno}: {exc}") from exc

            if not parts:
                continue

            head, *tail = parts
            if head == "newmtl":
                current_material = tail[0] if tail else None
            elif head == "map_Kd" and current_material and tail:
                result[current_material] = (path.parent / tail[-1]).resolve()

    return result


def parse_mtl_diffuse_map(path: Path) -> dict[str, tuple[float, float, float]]:
    result: dict[str, tuple[float, float, float]] = {}
    current_material: str | None = None

    with path.open("r", encoding="utf-8") as f:
        for lineno, raw_line in enumerate(f, start=1):
            line = raw_line.strip()
            if not line or line.startswith("#"):
                continue

            try:
                parts = shlex.split(line, comments=False, posix=True)
            except ValueError as exc:
                raise ObjConversionError(f"{path}:{lineno}: {exc}") from exc

            if not parts:
                continue

            head, *tail = parts
            if head == "newmtl":
                current_material = tail[0] if tail else None
            elif head == "Kd" and current_material:
                if len(tail) < 3:
                    raise ObjConversionError(f"{path}:{lineno}: Kd needs 3 components")
                result[current_material] = (float(tail[0]), float(tail[1]), float(tail[2]))

    return result


def resolve_material_colors(
    mtllibs: list[Path],
) -> dict[str, tuple[float, float, float]]:
    diffuse_colors: dict[str, tuple[float, float, float]] = {}

    for mtllib in mtllibs:
        if not mtllib.exists():
            raise ObjConversionError(f"Referenced MTL file does not exist: {mtllib}")
        diffuse_colors.update(parse_mtl_diffuse_map(mtllib))

    return diffuse_colors


def diffuse_rgb_to_palette_index(
    rgb: tuple[float, float, float],
    *,
    nearest: bool,
) -> int:
    rgb_888 = (
        max(0, min(255, round(rgb[0] * 255))),
        max(0, min(255, round(rgb[1] * 255))),
        max(0, min(255, round(rgb[2] * 255))),
    )
    exact_lookup = build_exact_palette_lookup_first_match(ENGINE_PALETTE_888)

    if rgb_888 in exact_lookup:
        return exact_lookup[rgb_888]
    if nearest:
        return nearest_palette_index(rgb_888, ENGINE_PALETTE_888)

    raise ObjConversionError(
        f"Material diffuse color {rgb_888} does not exist in engine palette exactly. "
        "Use --nearest to allow approximate palette remapping."
    )


def emit_indexed_bmp_with_engine_palette(
    src_path: Path,
    dst_path: Path,
    *,
    nearest: bool,
) -> None:
    if Image is None:
        raise ObjConversionError(
            "Pillow is required to convert non-BMP textures. Install it with `pip install pillow`."
        )

    exact_lookup = build_exact_palette_lookup_first_match(ENGINE_PALETTE_888)

    with Image.open(src_path) as img:
        rgba = img.convert("RGBA")
        width, height = rgba.size

        if width < 1 or height < 1 or width > 1024 or height > 1024:
            raise ObjConversionError(
                f"Texture dimensions {width}x{height} are outside the supported 1..1024 range"
            )

        remapped = bytearray(width * height)
        pixels = list(rgba.getdata())
        for i, (r, g, b, _a) in enumerate(pixels):
            rgb = (r, g, b)
            if rgb in exact_lookup:
                remapped[i] = exact_lookup[rgb]
            elif nearest:
                remapped[i] = nearest_palette_index(rgb, ENGINE_PALETTE_888)
            else:
                raise ObjConversionError(
                    f"Texture color {rgb} does not exist in engine palette exactly. "
                    "Use --nearest to allow approximate palette remapping."
                )

    palette_flat: list[int] = []
    for r, g, b in ENGINE_PALETTE_888:
        palette_flat.extend((r, g, b))

    indexed = Image.frombytes("P", (width, height), bytes(remapped))
    indexed.putpalette(palette_flat)
    indexed.save(dst_path, format="BMP", bits=8)


def build_face_colors_from_materials(
    triangles: list[ObjTriangle],
    mtllibs: list[Path],
    *,
    nearest: bool,
) -> bytes:
    material_colors = resolve_material_colors(mtllibs)
    face_colors = bytearray()
    found_any = False

    for triangle in triangles:
        if triangle.material is None:
            face_colors.append(15)
            continue

        rgb = material_colors.get(triangle.material)
        if rgb is None:
            face_colors.append(15)
            continue

        face_colors.append(diffuse_rgb_to_palette_index(rgb, nearest=nearest))
        found_any = True

    return bytes(face_colors) if found_any else b""


def resolve_texture_source(
    obj_path: Path,
    triangles: list[ObjTriangle],
    mtllibs: list[Path],
    texture_override: Path | None,
) -> tuple[Path | None, set[str] | None]:
    if texture_override is not None:
        return texture_override.resolve(), None

    used_materials = [triangle.material for triangle in triangles if triangle.material]
    if not used_materials:
        return None, None

    material_maps: dict[str, Path] = {}
    for mtllib in mtllibs:
        if not mtllib.exists():
            raise ObjConversionError(f"Referenced MTL file does not exist: {mtllib}")
        material_maps.update(parse_mtl_texture_map(mtllib))

    texture_path: Path | None = None
    textured_materials: set[str] = set()
    for material in used_materials:
        candidate = material_maps.get(material)
        if candidate is None:
            continue
        if texture_path is None:
            texture_path = candidate
            textured_materials.add(material)
        elif texture_path != candidate:
            print(
                f"Warning: OBJ uses multiple diffuse textures; using the first one ({texture_path.name}) and relying on face colors for the rest.",
                file=sys.stderr,
            )
            return texture_path, textured_materials
        else:
            textured_materials.add(material)

    return texture_path, textured_materials if texture_path is not None else None


def make_texture_name(
    texture_source: Path | None,
    texture_name_override: str | None,
) -> tuple[str | None, bool]:
    if texture_name_override:
        return texture_name_override, True
    if texture_source is None:
        return None, False
    return texture_source.with_suffix(".kif").name, True


def maybe_convert_texture(
    texture_source: Path | None,
    texture_name: str | None,
    output_dir: Path,
    nearest: bool,
    convert_texture: bool,
) -> tuple[Path | None, Path | None]:
    if texture_source is None or texture_name is None or not convert_texture:
        return None, None

    bmp_source = texture_source
    emitted_bmp_path: Path | None = None

    if texture_source.suffix.lower() != ".bmp":
        emitted_bmp_path = output_dir / texture_source.with_suffix(".bmp").name
        emit_indexed_bmp_with_engine_palette(texture_source, emitted_bmp_path, nearest=nearest)
        bmp_source = emitted_bmp_path

    output_path = output_dir / texture_name
    try:
        convert_bmp_to_kif(bmp_source, output_path, nearest=nearest)
    except TextureConversionError as exc:
        raise ObjConversionError(f"Texture conversion failed: {exc}") from exc

    return emitted_bmp_path, output_path


def encode_faces(triangles: list[ObjTriangle]) -> bytes:
    chunks: list[bytes] = []
    for triangle in triangles:
        face_parts = []
        for vertex in triangle.vertices:
            face_parts.append(
                KMDL_FACE_VERTEX_STRUCT.pack(
                    vertex.vertex,
                    vertex.normal if vertex.normal is not None else KMDL_INDEX_NONE,
                    vertex.uv if vertex.uv is not None else KMDL_INDEX_NONE,
                )
            )
        chunks.append(b"".join(face_parts))
    return b"".join(chunks)


def convert_obj_to_kmdl(
    src_path: Path,
    dst_path: Path,
    *,
    texture_override: Path | None,
    texture_name_override: str | None,
    face_color: int | None,
    convert_texture: bool,
    nearest: bool,
    keep_uvs: bool,
) -> tuple[Path | None, str | None]:
    vertices, uvs, normals, triangles, mtllibs = parse_obj(src_path)
    uvs = transform_uvs(uvs, keep_uvs=keep_uvs)
    vertices, vertex_remap = compact_entries(vertices)
    uvs, uv_remap = compact_entries(uvs)
    normals, normal_remap = compact_entries(normals)
    triangles = remap_triangle_indices(triangles, vertex_remap, uv_remap, normal_remap)
    texture_source, textured_materials = resolve_texture_source(src_path, triangles, mtllibs, texture_override)
    triangles = strip_uvs_for_non_primary_texture_faces(triangles, textured_materials)
    texture_name, has_texture = make_texture_name(texture_source, texture_name_override)

    if len(vertices) > 0xFFFF:
        raise ObjConversionError(f"Too many vertices: {len(vertices)} > 65535")
    if len(triangles) > 0xFFFF:
        raise ObjConversionError(f"Too many faces: {len(triangles)} > 65535")
    if len(normals) > 0xFFFF:
        raise ObjConversionError(f"Too many normals: {len(normals)} > 65535")
    if len(uvs) > 0xFFFF:
        raise ObjConversionError(f"Too many UVs: {len(uvs)} > 65535")
    if has_texture and len(uvs) == 0:
        raise ObjConversionError("Texture was requested but the OBJ has no UV coordinates")

    flags = 0
    face_colors = b""
    if face_color is not None:
        if face_color < 0 or face_color > 255:
            raise ObjConversionError(f"Face color must be 0..255, got {face_color}")
        flags |= KMDL_FLAG_HAS_FACE_COLORS
        face_colors = bytes([face_color]) * len(triangles)
    else:
        face_colors = build_face_colors_from_materials(triangles, mtllibs, nearest=nearest)
        if face_colors:
            flags |= KMDL_FLAG_HAS_FACE_COLORS
    if has_texture:
        flags |= KMDL_FLAG_HAS_TEXTURE

    texture_name_bytes = b""
    if texture_name is not None:
        texture_name_bytes = texture_name.encode("utf-8") + b"\0"
        if len(texture_name_bytes) > 255:
            raise ObjConversionError("Texture name is too long for KMDL header")

    emitted_bmp_path, converted_texture_path = maybe_convert_texture(
        texture_source,
        texture_name,
        dst_path.parent,
        nearest,
        convert_texture,
    )

    header = KMDL_HEADER_STRUCT.pack(
        KMDL_MAGIC,
        KMDL_VERSION,
        flags,
        len(vertices),
        len(triangles),
        len(normals),
        len(uvs),
        0,
        len(texture_name_bytes),
    )

    vertex_blob = b"".join(KMDL_VERTEX_STRUCT.pack(*vertex) for vertex in vertices)
    normal_blob = b"".join(KMDL_VERTEX_STRUCT.pack(*normal) for normal in normals)
    uv_blob = b"".join(KMDL_UV_STRUCT.pack(*uv) for uv in uvs)
    face_blob = encode_faces(triangles)

    with dst_path.open("wb") as f:
        f.write(header)
        f.write(texture_name_bytes)
        f.write(vertex_blob)
        f.write(normal_blob)
        f.write(uv_blob)
        f.write(face_blob)
        f.write(face_colors)

    if emitted_bmp_path is not None:
        print(f"Emitted indexed BMP -> {emitted_bmp_path}")
    if converted_texture_path is not None:
        print(f"Converted texture -> {converted_texture_path}")

    return texture_source, texture_name


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Convert Wavefront OBJ into the engine KMDL/KMF model format."
    )
    parser.add_argument("input", type=Path, help="Input .obj file")
    parser.add_argument("output", type=Path, nargs="?", help="Output .kmf file")
    parser.add_argument(
        "--texture",
        type=Path,
        help="Override diffuse texture source path instead of reading OBJ materials",
    )
    parser.add_argument(
        "--texture-name",
        help="Texture asset name stored in KMDL (defaults to texture basename with .kif suffix)",
    )
    parser.add_argument(
        "--convert-texture",
        action="store_true",
        help="Convert the diffuse texture to KIF next to the output KMDL, emitting an indexed BMP first if needed",
    )
    parser.add_argument(
        "--nearest",
        action="store_true",
        help="Allow approximate palette remapping when converting BMP to KIF",
    )
    parser.add_argument(
        "--face-color",
        type=int,
        help="Optional fallback face color index (0..255) written for all faces",
    )
    parser.add_argument(
        "--keep-uvs",
        action="store_true",
        help="Keep OBJ UVs unchanged instead of flipping the V coordinate during export",
    )
    return parser


def main() -> int:
    parser = build_arg_parser()
    args = parser.parse_args()

    src_path = args.input.resolve()
    dst_path = args.output.resolve() if args.output else src_path.with_suffix(".kmf")

    try:
        texture_source, texture_name = convert_obj_to_kmdl(
            src_path,
            dst_path,
            texture_override=args.texture,
            texture_name_override=args.texture_name,
            face_color=args.face_color,
            convert_texture=args.convert_texture,
            nearest=args.nearest,
            keep_uvs=args.keep_uvs,
        )
    except Exception as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1

    print(f"Converted {src_path} -> {dst_path}")
    if texture_source and texture_name:
        print(f"Texture source: {texture_source}")
        print(f"KMF texture name: {texture_name}")
        if args.convert_texture:
            print(f"Converted texture -> {dst_path.parent / texture_name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
