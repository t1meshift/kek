# On-disk formats

Every format here is little-endian, with no alignment padding beyond what is
written out explicitly as `reserved`. The engine reads headers with a single
`read(sizeof(hdr))` straight into a C struct, so the struct layout *is* the
format; the static asserts next to each struct are what keep a compiler from
quietly changing it. The Python writers under [`scripts/`](../scripts) carry the
same layout as a `struct.Struct` format string, and the two have to be edited
together — there is nothing that checks them against each other.

Floats are IEEE-754 binary32. Nothing converts byte order or float
representation on load; a big-endian target needs a conversion pass, not a
wider assert.

Colours are never stored as RGB. Every colour in every format is an index into
the engine's 256-entry palette (`KEK_palette_item`, three bytes of 0..63 VGA DAC
values — see [`kek_palette.h`](../kek/include/kek_palette.h)).

| Format | Extension | Magic | Version | Reader | Writer |
| --- | --- | --- | --- | --- | --- |
| Model | `.kmf` | `KMDL` | 1 | [`kek_file_model.c`](../kek/kek_file_model.c) | [`obj_to_kmf.py`](../scripts/obj_to_kmf.py) |
| Image | `.kif` | `KIMG` | 1 | [`kek_file_image.c`](../kek/kek_file_image.c) | [`bmp_to_kif.py`](../scripts/bmp_to_kif.py) |
| Level | `.klf` | `KLVL` | — | not written yet | not written yet |

The extension and the magic disagree on purpose: the magic is four bytes of a
readable word, the extension is short. A reader checks the magic and ignores the
name it was opened under.

---

## KMF — model

### Header, 20 bytes

| Offset | Size | Field | Notes |
| --- | --- | --- | --- |
| 0 | 4 | `magic` | `"KMDL"`, not NUL-terminated |
| 4 | 2 | `version` | 1. Anything else is refused |
| 6 | 2 | `flags` | see below |
| 8 | 2 | `vertices_count` | 1..65535, must be non-zero |
| 10 | 2 | `faces_count` | 1..65535, must be non-zero |
| 12 | 2 | `normals_count` | 0 means "derive them" |
| 14 | 2 | `uv_count` | 0 when the model is untextured |
| 16 | 2 | `reserved` | written as 0 |
| 18 | 1 | `texture_name_size` | bytes of the name that follows, *including* its NUL. 0 when there is none |
| 19 | 1 | — | padding, written as 0 |

`flags`:

| Bit | Name | Meaning |
| --- | --- | --- |
| 0 | `HAS_FACE_COLORS` | a palette index per face is appended at the end |
| 1 | `HAS_TEXTURE` | the model has UVs and names a `.kif` to load |

### Body, in this order

1. **Texture name** — `texture_name_size` bytes, the last of which must be
   `\0`. Absent when the size is 0. It is a path resolved by the same
   `KEK_AssetProvider` the model came from, not a path relative to the model.
2. **Vertices** — `vertices_count` × 12 bytes, each `float x, y, z`.
3. **Normals** — `normals_count` × 12 bytes, same shape. Unit length is
   expected but re-normalised on load anyway.
4. **UVs** — `uv_count` × 8 bytes, each `float u, v`.
5. **Faces** — `faces_count` × 18 bytes. A face is three vertices, each of
   which is three `uint16` indices in the order `vertex`, `normal`, `uv`.
   `0xFFFF` means "no index" for `normal` and `uv`; there is no such thing as a
   face vertex without a position.
6. **Face colours** — `faces_count` × 1 byte, only when `HAS_FACE_COLORS` is
   set. One palette index per face.

Faces are triangles. The exporter triangulates; the format has no other
primitive.

### What the loader refuses

Beyond the magic and the version: a zero vertex or face count; counts past the
pool limits (`KEK_POOL_MODEL_VERTS_MAX` for vertices *and* normals,
`KEK_POOL_MODEL_FACES_MAX`, `KEK_POOL_MODEL_UVS_MAX` for `uv_count` and, when
`HAS_TEXTURE` is set, for `faces_count` too — `face_textures` holds one UV
triple per face, not per UV — and `KEK_POOL_MODEL_COLORS_MAX` for the colour
block); a texture name whose last
byte is not `\0` — its length needs no check, since a one-byte field cannot
describe a name the 256-byte buffer will not hold; a file that ends early at
any point; a face vertex index at or past `vertices_count`; a normal index that is
neither `0xFFFF` nor below `normals_count`; a UV index that is neither `0xFFFF`
nor below `uv_count`, when `HAS_TEXTURE` is set; `HAS_TEXTURE` with no texture
name; and a texture name that fails to load as a KIF.

Note the consequence of the normal rule: with `normals_count == 0`, every face
vertex has to say `0xFFFF`, because no index is below zero.

### What the loader does, not what the file says

- **Normals are expanded to three per face.** In memory a face carries a normal
  per corner (`KEK_model_face_normal`), whatever the file stored. With
  `normals_count == 0` the loader computes one flat normal per face from the
  cross product of its edges and writes it into all three; with indexed
  normals, a corner whose index is `0xFFFF` falls back to that same computed
  normal. This is the single largest array in the engine and Tier 2 of the
  backlog is about undoing it.
- **Missing UVs become NaN.** Under `HAS_TEXTURE`, a corner with no UV index
  gets `{NAN, NAN}` rather than `{0, 0}`, so it is visibly wrong rather than
  quietly sampling the corner of the texture.
- **The model owns the texture it loaded.** `KEK_model.owns_texture` is set, and
  `kek_model_destroy` releases the texture handle with it. A clone shares the
  handle and never the ownership.

---

## KIF — image

### Header, 16 bytes

| Offset | Size | Field | Notes |
| --- | --- | --- | --- |
| 0 | 4 | `magic` | `"KIMG"`, not NUL-terminated |
| 4 | 2 | `version` | 1 |
| 6 | 2 | `width` | non-zero |
| 8 | 2 | `height` | non-zero |
| 10 | 1 | `encoding` | 0 = raw, 1 = RLE |
| 11 | 5 | `reserved` | written as 0 |

The loader bounds the image by `width * height` against
`KEK_POOL_TEXTURE_PIXELS_MAX` rather than by either dimension alone. The writer
additionally caps each dimension at 1024.

### Body

**Encoding 0, raw.** `width * height` bytes, one palette index per pixel, row
by row from the top. The loader also checks the file is at least
`16 + width * height` bytes before reading.

**Encoding 1, RLE.** Pairs of bytes, `(count, value)`, read until
`width * height` pixels have been produced. `count` is 1..255 — a zero count is
refused, since it would encode nothing and could loop forever — and a run that
would carry the total past `width * height` is refused rather than truncated.
Runs are not bounded by row: a run may cross from the end of one row into the
next.

Nothing writes encoding 1 today. `bmp_to_kif.py` always emits raw; the decoder
is there because the format is meant for a machine where the texture pool is
the expensive thing.

---

## KLF — level

Not specified yet. Tier 4 of [the backlog](../BACKLOG.md): magic `KLVL`, a model
table of paths, an instance table of transforms plus a model index, and a camera
spawn — under the same discipline as KMF, meaning fixed-size records,
bounds-checked, and no allocation. It gets written here when it exists.
