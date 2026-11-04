#!/usr/bin/env python3
"""Converts the MODIS global land cover map into Orbis' land cover format.

Source: MODIS/Terra+Aqua Land Cover Type Yearly L3 Global 0.05 Deg CMG (MCD12C1, collection 6,
year 2019), majority IGBP class per 0.05 degree cell (~5.6 km). NASA data, no restrictions on use
(Friedl, M., Sulla-Menashe, D. (2015), doi:10.5067/MODIS/MCD12C1.006). The GeoTIFF is taken
from the MCD12C1_2019_v006 package on PyPI (JPL ECOSTRESS tooling), pinned by SHA-256.

Usage:
    python tools/prepare_landcover.py                 # downloads the source package
    python tools/prepare_landcover.py MCD12C1_2019_v006-1.0.1-py3-none-any.whl

Output: data/earth_landcover.olc - see src/core/terrain/LandCover.hpp for the format:
    char[4]  magic "OLC1"
    uint32   width, height       (CELLS, longitude fastest; cell-registered: cell (0, 0) spans
                                  90..90-dLat north to south and -180..-180+dLon west to east)
    uint32   classCount          (17: IGBP 0..16, 0 = water)
    runs     row by row, west to east: uint8 class, LEB128 varint run length (>= 1);
             a run never crosses the end of a row
    uint32   FNV-1a hash of the decoded cells (row-major), detects corruption
All little-endian. Only the Python standard library is required.
"""
import hashlib
import io
import struct
import sys
import urllib.request
import zipfile
import zlib
from pathlib import Path

SOURCE_URL = ("https://files.pythonhosted.org/packages/67/27/"
              "680755a23b08f7d36ec218da56bf6801901d3cedfb6551d6c7a8e3075f95/"
              "MCD12C1_2019_v006-1.0.1-py3-none-any.whl")
SOURCE_SHA256 = "0899c746e2f5060e3b94a9c220311103be1984999af4fc1b33c3d7b3244b97bc"
TIFF_NAME = "MCD12C1_2019_v006/MCD12C1.A2019001.006.2020220162300.tif"
OUTPUT = Path(__file__).resolve().parent.parent / "data" / "earth_landcover.olc"
CLASS_COUNT = 17


def read_tiff_u8(data: bytes):
    """Minimal baseline TIFF reader: one 8-bit band, deflate- or un-compressed strips, no
    predictor. Returns (width, height, bytes, geo) where geo = (pixel scale, tie point)."""
    order = {b"II": "<", b"MM": ">"}[data[:2]]
    if struct.unpack(order + "H", data[2:4])[0] != 42:
        raise ValueError("not a classic TIFF")
    ifd = struct.unpack(order + "I", data[4:8])[0]
    count = struct.unpack(order + "H", data[ifd:ifd + 2])[0]
    sizes = {1: 1, 2: 1, 3: 2, 4: 4, 12: 8}
    formats = {1: "B", 2: "c", 3: "H", 4: "I", 12: "d"}
    tags = {}
    for i in range(count):
        entry = data[ifd + 2 + 12 * i: ifd + 14 + 12 * i]
        tag, kind, n = struct.unpack(order + "HHI", entry[:8])
        size = sizes[kind] * n
        raw = entry[8:12] if size <= 4 else data[struct.unpack(order + "I", entry[8:12])[0]:][:size]
        tags[tag] = struct.unpack(order + formats[kind] * n, raw[:size])

    width, height = tags[256][0], tags[257][0]
    if tags[258][0] != 8 or tags.get(277, (1,))[0] != 1 or tags.get(317, (1,))[0] != 1:
        raise ValueError("expected one 8-bit band without predictor")
    compression = tags.get(259, (1,))[0]
    pixels = bytearray()
    for offset, length in zip(tags[273], tags[279]):
        strip = data[offset:offset + length]
        pixels += zlib.decompress(strip) if compression in (8, 32946) else strip
    if len(pixels) != width * height:
        raise ValueError("strip data does not match the image size")
    return width, height, bytes(pixels), (tags.get(33550), tags.get(33922))


def varint(n: int) -> bytes:
    out = bytearray()
    while True:
        byte = n & 0x7F
        n >>= 7
        out.append(byte | (0x80 if n else 0))
        if not n:
            return bytes(out)


def fnv1a(data: bytes) -> int:
    h = 0x811C9DC5
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def main() -> None:
    if len(sys.argv) > 1:
        raw = Path(sys.argv[1]).read_bytes()
    else:
        print(f"Downloading {SOURCE_URL} ...")
        raw = urllib.request.urlopen(SOURCE_URL).read()
    digest = hashlib.sha256(raw).hexdigest()
    if digest != SOURCE_SHA256:
        sys.exit(f"Unexpected SHA-256 {digest}; the source file changed - refusing to convert.")

    tiff = zipfile.ZipFile(io.BytesIO(raw)).read(TIFF_NAME)
    width, height, cells, (scale, tie) = read_tiff_u8(tiff)
    # Check the georeferencing instead of assuming it: a global, cell-registered grid starting
    # at the north-west corner (-180, 90) with square cells.
    if not (scale and tie and abs(scale[0] * width - 360.0) < 1e-6 and
            abs(scale[1] * height - 180.0) < 1e-6 and tie[3:5] == (-180.0, 90.0)):
        sys.exit(f"Unexpected georeferencing: scale {scale}, tie point {tie}")
    if max(cells) >= CLASS_COUNT:
        sys.exit(f"Unexpected class value {max(cells)}")

    out = bytearray(b"OLC1")
    out += struct.pack("<III", width, height, CLASS_COUNT)
    runs = 0
    for r in range(height):
        row = cells[r * width:(r + 1) * width]
        start = 0
        for c in range(1, width + 1):
            if c == width or row[c] != row[start]:
                out.append(row[start])
                out += varint(c - start)
                runs += 1
                start = c
    out += struct.pack("<I", fnv1a(cells))

    counts = [cells.count(k) for k in range(CLASS_COUNT)]
    print(f"grid {width} x {height} cells ({scale[0]} deg), {runs} runs, "
          f"water {100.0 * counts[0] / len(cells):.1f} %")
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_bytes(bytes(out))
    print(f"wrote {OUTPUT} ({OUTPUT.stat().st_size / 1e6:.1f} MB)")


if __name__ == "__main__":
    main()
