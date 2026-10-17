"""Build maps/randomizer.pack from the campaign caches in maps/.

Each map on the disc is a plain header plus a zlib stream. The game used to
inflate that at level load. This writes one file the game can read instead:
the inflated tag section of each campaign map, and the bitmap pixels and
sound samples those tags point at. Identical pixel and sample blobs are
stored once. The maps themselves are local and are not part of the pack's
sources in git.

A map whose zlib stream does not finish is refused, so a half-copied cache
is not baked in.
"""

import argparse
import hashlib
import os
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WINDOW = 0x803A6000
HEADER = 0x800
CAMPAIGN = ("a10", "a30", "a50", "b30", "b40", "c10", "c20", "c40", "d20", "d40")
TAG_LIMIT = 0x1600000
PACK_LIMIT = 0x7F000000

BITM = struct.unpack(">I", b"bitm")[0]
SND = struct.unpack(">I", b"snd!")[0]
HEAD = struct.unpack(">I", b"head")[0]
TAGS = struct.unpack(">I", b"tags")[0]

# bitmap_group.pixel_data is a tag_data at 0x30. On these caches its size is 0
# and its file_offset is not a pixel base. Each bitmap_data in the bitmaps
# block at 0x60 names its own pixels, and the cache reads that pixels_offset.
BITMAPS_COUNT = 0x60
BITMAPS_ADDRESS = 0x64
BITMAP_DATA = 0x30
# bitmaps.c bitmap_format_bits_per_pixel_table, through p8-bump
BITMAP_BITS = (8, 8, 8, 16, 0, 0, 16, 0, 16, 16, 32, 32, 0, 0, 4, 8, 8, 8)
# sound_definition.pitch_ranges is a tag_block at 0x98.
PITCH_COUNT = 0x98
PITCH_ADDRESS = 0x9C
PITCH_SIZE = 0x48
# sound_pitch_range.permutations is a tag_block at 0x3C.
PERM_COUNT = 0x3C
PERM_ADDRESS = 0x40
PERM_SIZE = 0x7C
# sound_permutation.samples is a tag_data at 0x40.
SAMPLE_SIZE = 0x40
SAMPLE_OFFSET = 0x48


def fourcc_ok(value, expected):
    return value == expected


def u32(buf, offset):
    return struct.unpack_from("<I", buf, offset)[0]


def i32(buf, offset):
    return struct.unpack_from("<i", buf, offset)[0]


def locate(blob, address):
    if address < WINDOW:
        return None
    offset = address - WINDOW
    if offset < 0 or offset >= len(blob):
        return None
    return offset


def inflate_map(source, destination, file_length):
    remaining = file_length - HEADER
    decoder = zlib.decompressobj()
    with open(source, "rb") as src, open(destination, "wb") as out:
        header = src.read(HEADER)
        if len(header) != HEADER:
            raise RuntimeError("short header")
        out.write(header)
        while remaining > 0:
            chunk = src.read(1 << 20)
            if not chunk and decoder.unconsumed_tail == b"":
                break
            piece = decoder.decompress(chunk) if chunk else b""
            if len(piece) > remaining:
                piece = piece[:remaining]
            if piece:
                out.write(piece)
                remaining -= len(piece)
            if not chunk:
                break
        tail = decoder.flush()
        if tail and remaining > 0:
            if len(tail) > remaining:
                tail = tail[:remaining]
            out.write(tail)
            remaining -= len(tail)
    if remaining != 0:
        raise RuntimeError("zlib ended %d bytes early" % remaining)


def read_header(path):
    with open(path, "rb") as handle:
        raw = handle.read(0x18)
    if len(raw) < 0x18:
        raise RuntimeError("short file")
    signature, version, file_length, _pad, tag_offset, tag_size = struct.unpack("<Iiiiii", raw)
    if not fourcc_ok(signature, HEAD) or version != 5:
        raise RuntimeError("not an Xbox map (signature %08x, version %d)" % (signature, version))
    if file_length < HEADER or tag_offset < HEADER or tag_size < 36:
        raise RuntimeError("header offsets are not a campaign cache")
    if tag_offset > file_length - tag_size or tag_size > TAG_LIMIT:
        raise RuntimeError("tag section is %d bytes at %d, past the map or the port's limit" % (tag_size, tag_offset))
    return file_length, tag_offset, tag_size


def bitmap_bytes(width, height, depth, kind, fmt, flags, mipmaps):
    if fmt < 0 or fmt >= len(BITMAP_BITS) or BITMAP_BITS[fmt] == 0:
        return 0
    if mipmaps < 0 or mipmaps > 16 or width <= 0 or height <= 0 or depth <= 0:
        return 0
    if width > 4096 or height > 4096 or depth > 4096:
        return 0
    compressed = (flags & 2) != 0
    pixels = 0
    for level in range(mipmaps + 1):
        wide = max(width >> level, 1)
        high = max(height >> level, 1)
        deep = max(depth >> level, 1)
        if compressed:
            wide += (-wide) & 3
            high += (-high) & 3
        pixels += wide * high * deep * (6 if kind == 2 else 1)
        if pixels > 64 * 1024 * 1024:
            return 0
    return pixels * BITMAP_BITS[fmt] // 8


def tag_name(blob, instance):
    pointer = u32(blob, instance + 0x10)
    offset = locate(blob, pointer)
    if offset is None:
        return ""
    end = blob.find(b"\0", offset, offset + 256)
    if end < 0:
        return ""
    return blob[offset:end].decode("latin1")


def keep_bytes(group, name):
    """Level geometry, menus, music and ambience are not actors or weapons,
    and the same character tags would otherwise carry them in every map."""
    if group == BITM:
        return not name.startswith((
            "levels\\", "sky\\", "ui\\", "rasterizer\\", "scenery\\", "cinematics\\"))
    if group == SND:
        return not name.startswith((
            "sound\\music\\", "sound\\sfx\\ambience\\"))
    return True


def add_range(ranges, file_length, offset, size):
    if size <= 0 or size > 64 * 1024 * 1024 or offset < 0:
        return
    if offset > file_length - size:
        return
    ranges.append((offset, size))


def collect(blob, file_length):
    if len(blob) < 36 or u32(blob, 0x20) != TAGS:
        raise RuntimeError("tag section signature is not 'tags'")
    count = i32(blob, 0x0C)
    if count <= 0 or count > 100000:
        raise RuntimeError("tag count %d" % count)
    instances = locate(blob, u32(blob, 0))
    if instances is None or instances + count * 0x20 > len(blob):
        raise RuntimeError("tag table is outside the tag section")
    ranges = []
    bitmaps = 0
    sounds = 0
    for index in range(count):
        inst = instances + index * 0x20
        group = u32(blob, inst)
        base = locate(blob, u32(blob, inst + 0x14))
        if base is None:
            continue
        if not keep_bytes(group, tag_name(blob, inst)):
            continue
        if group == BITM:
            if base + BITMAPS_ADDRESS + 4 > len(blob):
                continue
            bitmaps += 1
            bitmap_count = i32(blob, base + BITMAPS_COUNT)
            bitmap_at = locate(blob, u32(blob, base + BITMAPS_ADDRESS))
            if bitmap_at is None or bitmap_count <= 0 or bitmap_count > 512:
                continue
            if bitmap_at + bitmap_count * BITMAP_DATA > len(blob):
                continue
            for bitmap in range(bitmap_count):
                rec = bitmap_at + bitmap * BITMAP_DATA
                width, height, depth = struct.unpack_from("<hhh", blob, rec + 4)
                kind, fmt, flags = struct.unpack_from("<hhH", blob, rec + 0x0A)
                mipmaps = struct.unpack_from("<h", blob, rec + 0x14)[0]
                pixels_offset = i32(blob, rec + 0x18)
                pixels_size = i32(blob, rec + 0x1C)
                if pixels_offset < 0:
                    continue
                need = pixels_size
                read = bitmap_bytes(width, height, depth, kind, fmt, flags, mipmaps)
                if read > need:
                    need = read
                add_range(ranges, file_length, pixels_offset, need)
        elif group == SND:
            if base + PITCH_ADDRESS + 4 > len(blob):
                continue
            sounds += 1
            pitch_count = i32(blob, base + PITCH_COUNT)
            pitch_at = locate(blob, u32(blob, base + PITCH_ADDRESS))
            if pitch_at is None or pitch_count <= 0 or pitch_count > 32:
                continue
            if pitch_at + pitch_count * PITCH_SIZE > len(blob):
                continue
            for pitch in range(pitch_count):
                at = pitch_at + pitch * PITCH_SIZE
                perm_count = i32(blob, at + PERM_COUNT)
                perm_at = locate(blob, u32(blob, at + PERM_ADDRESS))
                if perm_at is None or perm_count <= 0 or perm_count > 256:
                    continue
                if perm_at + perm_count * PERM_SIZE > len(blob):
                    continue
                for perm in range(perm_count):
                    sample = perm_at + perm * PERM_SIZE
                    add_range(ranges, file_length, i32(blob, sample + SAMPLE_OFFSET), i32(blob, sample + SAMPLE_SIZE))
    if bitmaps < 20 or sounds < 20:
        raise RuntimeError("only %d bitmaps and %d sounds; the tag layout was not read" % (bitmaps, sounds))
    ranges.sort()
    merged = []
    for offset, size in ranges:
        if merged and offset <= merged[-1][0] + merged[-1][1]:
            end = max(merged[-1][0] + merged[-1][1], offset + size)
            merged[-1] = (merged[-1][0], end - merged[-1][0])
        else:
            merged.append((offset, size))
    return count, bitmaps, sounds, merged


def hash_span(handle, offset, size):
    digest = hashlib.sha256()
    handle.seek(offset)
    left = size
    while left:
        chunk = handle.read(min(left, 1 << 20))
        if len(chunk) != min(left, 1 << 20) and len(chunk) != left:
            raise RuntimeError("short read of resource bytes")
        digest.update(chunk)
        left -= len(chunk)
    return digest.digest()


def build(maps_dir, names, output):
    work = maps_dir / ".randomizer_pack_work"
    work.mkdir(exist_ok=True)
    resources = open(work / "resources.bin", "wb")
    known = {}
    prepared = []
    try:
        for name in names:
            source = maps_dir / (name + ".map")
            if not source.is_file():
                raise RuntimeError("missing %s" % source)
            file_length, tag_offset, tag_size = read_header(source)
            print("inflating %s (%d byte map)" % (name, file_length), flush=True)
            raw_path = work / (name + ".raw")
            inflate_map(source, raw_path, file_length)
            with open(raw_path, "rb") as raw:
                raw.seek(tag_offset)
                blob = raw.read(tag_size)
                count, bitmaps, sounds, spans = collect(blob, file_length)
                listed = []
                total = 0
                for offset, size in spans:
                    total += size
                    digest = hash_span(raw, offset, size)
                    place = known.get(digest)
                    if place is None:
                        place = resources.tell()
                        raw.seek(offset)
                        left = size
                        while left:
                            chunk = raw.read(min(left, 1 << 20))
                            if not chunk:
                                raise RuntimeError("short resource copy")
                            resources.write(chunk)
                            left -= len(chunk)
                        known[digest] = place
                    listed.append((offset, size, place))
            raw_path.unlink()
            tag_path = work / (name + ".tags")
            tag_path.write_bytes(blob)
            prepared.append({
                "name": name,
                "file_length": file_length,
                "tag_offset": tag_offset,
                "tag_size": tag_size,
                "count": count,
                "bitmaps": bitmaps,
                "sounds": sounds,
                "spans": listed,
                "span_bytes": total,
                "tags": tag_path,
            })
            print("  %s: %d tags, %d bitmaps, %d sounds, %d spans, %d resource bytes" % (
                name, count, bitmaps, sounds, len(listed), total), flush=True)
        resources.close()
        resources = None
        directory = 12 + len(prepared) * 32
        body = 0
        for item in prepared:
            body += item["tag_size"] + len(item["spans"]) * 12
        resource_size = (work / "resources.bin").stat().st_size
        pack_size = directory + body + resource_size
        if pack_size > PACK_LIMIT or pack_size < 0:
            raise RuntimeError("pack would be %d bytes, past what a 32-bit file offset can name" % pack_size)
        resource_at = directory + body
        cursor = directory
        temporary = Path(str(output) + ".tmp")
        with open(temporary, "wb") as out:
            out.write(struct.pack("<III", struct.unpack(">I", b"rpak")[0], 1, len(prepared)))
            for item in prepared:
                tags_at = cursor
                resources_at = cursor + item["tag_size"]
                name = item["name"].encode("ascii")
                if len(name) >= 8:
                    raise RuntimeError(item["name"])
                out.write(name + b"\0" * (8 - len(name)))
                out.write(struct.pack("<IIIIII", item["file_length"], item["tag_offset"], item["tag_size"],
                    tags_at, len(item["spans"]), resources_at))
                item["tags_at"] = tags_at
                item["resources_at"] = resources_at
                cursor = resources_at + len(item["spans"]) * 12
            if cursor != resource_at:
                raise RuntimeError("directory does not meet the resource bytes")
            for item in prepared:
                out.write(item["tags"].read_bytes())
                for offset, size, place in item["spans"]:
                    out.write(struct.pack("<III", offset, size, resource_at + place))
            with open(work / "resources.bin", "rb") as blob:
                while True:
                    chunk = blob.read(1 << 20)
                    if not chunk:
                        break
                    out.write(chunk)
        temporary.replace(output)
        print("wrote %s (%d bytes, %d unique resource bytes)" % (output, pack_size, resource_size), flush=True)
    finally:
        if resources is not None:
            resources.close()
        if work.is_dir():
            for path in work.glob("*"):
                path.unlink()
            work.rmdir()


def main():
    parser = argparse.ArgumentParser(description="Build the campaign randomizer pack.")
    parser.add_argument("--maps", nargs="*", default=list(CAMPAIGN))
    parser.add_argument("--maps-dir", type=Path, default=ROOT / "maps")
    parser.add_argument("--output", type=Path, default=ROOT / "maps" / "randomizer.pack")
    args = parser.parse_args()
    try:
        build(args.maps_dir, args.maps, args.output)
    except (OSError, RuntimeError, zlib.error) as error:
        print("randomizer pack: %s" % error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
