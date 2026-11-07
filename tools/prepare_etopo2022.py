#!/usr/bin/env python3
"""Converts an ETOPO 2022 global GeoTIFF into Orbis' elevation grid format.

Source: NOAA NCEI ETOPO 2022 Global Relief Model, 60 arc-second surface elevation
(doi:10.25921/fd45-gt74, public domain). Download it in a browser (NOAA blocks many automated
clients), then:

    python tools/prepare_etopo2022.py ETOPO_2022_v1_60s_N90W180_surface.tif

https://www.ngdc.noaa.gov/thredds/fileServer/global/ETOPO2022/60s/60s_surface_elev_gtif/
ETOPO_2022_v1_60s_N90W180_surface.tif

Output: data/earth_etopo2022_60s.oeg (~470 MB, not committed), the format of
tools/prepare_etopo.py. ETOPO 2022 is cell-registered; Orbis grids are node-registered with
nodes on both poles and on both +-180 meridians, so the cells are resampled half a cell
diagonally (see to_nodes).

Needs numpy. Deflate is decoded with zlib; LZW uses imagecodecs when installed and a slow
pure-Python decoder otherwise.
"""
import argparse
import hashlib
import struct
import sys
import zlib
from pathlib import Path

import numpy as np

KNOWN_SOURCES = {
    "9d27d4b8ea8e76977e2988bca667d7c8fa68b927355feffcddd6b4875a7fd08e":
        "ETOPO_2022_v1_60s_N90W180_surface.tif",
}
OUTPUT = Path(__file__).resolve().parent.parent / "data" / "earth_etopo2022_60s.oeg"

TIFF_TYPES = {1: "B", 2: "c", 3: "H", 4: "I", 5: "II", 6: "b", 8: "h", 9: "i", 11: "f",
              12: "d", 16: "Q", 17: "q"}
SAMPLE_TYPES = {(1, 8): "u1", (1, 16): "u2", (1, 32): "u4", (2, 8): "i1", (2, 16): "i2",
                (2, 32): "i4", (3, 32): "f4", (3, 64): "f8"}


class Tiff:
    """The first image of a classic or BigTIFF file: tags and decoded pixels."""

    def __init__(self, data: memoryview):
        self.data = data
        self.order = {b"II": "<", b"MM": ">"}[bytes(data[:2])]
        magic = self.unpack("H", 2)[0]
        if magic == 42:
            self.big, ifd = False, self.unpack("I", 4)[0]
        elif magic == 43:
            self.big, ifd = True, self.unpack("Q", 8)[0]
        else:
            raise ValueError("not a TIFF file")
        self.tags = self.read_ifd(ifd)

    def unpack(self, fmt: str, offset: int):
        return struct.unpack_from(self.order + fmt, self.data, offset)

    def read_ifd(self, offset: int) -> dict:
        count_fmt, entry_size, value_size = ("Q", 20, 8) if self.big else ("H", 12, 4)
        count = self.unpack(count_fmt, offset)[0]
        offset += struct.calcsize(count_fmt)
        tags = {}
        for i in range(count):
            entry = offset + i * entry_size
            tag, kind = self.unpack("HH", entry)
            n = self.unpack("Q" if self.big else "I", entry + 4)[0]
            fmt = TIFF_TYPES[kind]
            size = struct.calcsize(self.order + fmt) * n
            where = entry + (12 if self.big else 8)
            if size > value_size:
                where = self.unpack("Q" if self.big else "I", where)[0]
            if kind == 2:
                tags[tag] = bytes(self.data[where:where + n]).rstrip(b"\0").decode("latin-1")
            else:
                tags[tag] = struct.unpack_from(self.order + fmt * n, self.data, where)
        return tags

    def tag(self, number: int, default=None):
        value = self.tags.get(number)
        return default if value is None else value[0] if isinstance(value, tuple) else value

    def read(self) -> np.ndarray:
        width, height = self.tag(256), self.tag(257)
        if self.tag(277, 1) != 1 or self.tag(284, 1) != 1:
            raise ValueError("expected one band")
        dtype = np.dtype(SAMPLE_TYPES[(self.tag(339, 1), self.tag(258))]).newbyteorder(self.order)
        compression, predictor = self.tag(259, 1), self.tag(317, 1)
        if 322 in self.tags:
            block_w, block_h = self.tag(322), self.tag(323)
            offsets, counts = self.tags[324], self.tags[325]
            across = -(-width // block_w)
        else:
            block_w, block_h = width, self.tag(278, height)
            offsets, counts = self.tags[273], self.tags[279]
            across = 1

        image = np.empty((height, width), dtype=dtype.newbyteorder("="))
        for index, (offset, count) in enumerate(zip(offsets, counts)):
            raw = decompress(bytes(self.data[offset:offset + count]), compression)
            rows = block_h if 322 in self.tags else min(block_h, height - index * block_h)
            block = unpredict(raw, predictor, dtype, rows, block_w)
            y, x = (index // across) * block_h, (index % across) * block_w
            h, w = min(rows, height - y), min(block_w, width - x)
            image[y:y + h, x:x + w] = block[:h, :w]
        return image


def decompress(raw: bytes, compression: int) -> bytes:
    if compression == 1:
        return raw
    if compression in (8, 32946):
        return zlib.decompress(raw)
    if compression == 5:
        try:
            import imagecodecs
            return imagecodecs.lzw_decode(raw)
        except ImportError:
            return lzw_decode(raw)
    raise ValueError(f"unsupported TIFF compression {compression}")


def lzw_decode(raw: bytes) -> bytes:
    """TIFF LZW: MSB-first codes of 9..12 bits, early change, 256 = clear, 257 = end."""
    out = bytearray()
    table = [bytes([i]) for i in range(256)] + [b"", b""]
    bits, value, nbits, width, previous = 0, 0, 0, 9, None
    for byte in raw:
        value = (value << 8) | byte
        nbits += 8
        while nbits >= width:
            nbits -= width
            code = (value >> nbits) & ((1 << width) - 1)
            value &= (1 << nbits) - 1
            if code == 256:
                table, width, previous = table[:258], 9, None
                continue
            if code == 257:
                return bytes(out)
            if previous is None:
                entry = table[code]
            else:
                entry = table[code] if code < len(table) else previous + previous[:1]
                table.append(previous + entry[:1])
                if len(table) + 1 >= (1 << width) and width < 12:
                    width += 1
            out += entry
            previous = entry
    return bytes(out)


def unpredict(raw: bytes, predictor: int, dtype: np.dtype, rows: int, columns: int) -> np.ndarray:
    count = rows * columns
    if predictor == 1:
        return np.frombuffer(raw, dtype=dtype, count=count).reshape(rows, columns)
    if predictor == 2:
        values = np.frombuffer(raw, dtype=dtype, count=count).reshape(rows, columns)
        return np.cumsum(values, axis=1, dtype=dtype)
    if predictor == 3:
        # Byte-wise differencing over each row, whose bytes are grouped by significance, most
        # significant first, whatever the file's byte order (TIFF Technote 3).
        size = dtype.itemsize
        planes = np.frombuffer(raw, dtype=np.uint8, count=count * size)
        planes = planes.reshape(rows, size * columns)
        planes = np.cumsum(planes, axis=1, dtype=np.uint8).reshape(rows, size, columns)
        return planes.transpose(0, 2, 1).copy().view(dtype.newbyteorder(">")).reshape(rows, columns)
    raise ValueError(f"unsupported TIFF predictor {predictor}")


def geo_keys(tiff: Tiff) -> dict:
    directory = tiff.tags.get(34735)
    if not directory:
        return {}
    return {directory[i]: directory[i + 3] for i in range(4, 4 + 4 * directory[3], 4)
            if directory[i + 1] == 0}


def to_nodes(cells: np.ndarray) -> np.ndarray:
    """(H, W) cells covering the globe -> (H + 1, W + 1) nodes, int16 metres.

    Each node lies between four cells; Catmull-Rom at the midpoint, (-1, 9, 9, -1) / 16 per
    axis, keeps more of the peaks than their mean. Rows beyond a pole continue on the opposite
    meridian, columns wrap; a pole node is the mean of the polar row."""
    height, width = cells.shape
    weights = (-1.0 / 16.0, 9.0 / 16.0, 9.0 / 16.0, -1.0 / 16.0)

    def cell_row(k: int) -> np.ndarray:
        if k < 0:
            return np.roll(cells[-k - 1], width // 2)
        if k >= height:
            return np.roll(cells[2 * height - k - 1], width // 2)
        return cells[k]

    nodes = np.empty((height + 1, width + 1), dtype=np.int16)
    for start in range(0, height + 1, 256):
        stop = min(start + 256, height + 1)
        rows = np.stack([cell_row(k) for k in range(start - 2, stop + 1)]).astype(np.float64)
        vertical = sum(w * rows[i:i + stop - start] for i, w in enumerate(weights))
        block = sum(w * np.roll(vertical, 2 - i, axis=1) for i, w in enumerate(weights))
        if start == 0:
            block[0] = cells[0].mean()
        if stop == height + 1:
            block[-1] = cells[-1].mean()
        nodes[start:stop, :width] = np.round(np.clip(block, -32768, 32767))
    nodes[:, width] = nodes[:, 0]
    return nodes


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("source", type=Path, help="ETOPO 2022 GeoTIFF")
    parser.add_argument("--output", type=Path, default=OUTPUT)
    parser.add_argument("--trust-source", action="store_true",
                        help="convert a file whose SHA-256 is not the known 60 s surface grid")
    args = parser.parse_args()

    raw = args.source.read_bytes()
    digest = hashlib.sha256(raw).hexdigest()
    if digest in KNOWN_SOURCES:
        print(f"source verified: {KNOWN_SOURCES[digest]}")
    elif not args.trust_source:
        sys.exit(f"Unknown SHA-256 {digest}; pass --trust-source to convert it anyway.")

    tiff = Tiff(memoryview(raw))
    keys = geo_keys(tiff)
    if keys.get(2048, 4326) != 4326:
        sys.exit(f"Expected geographic WGS84 (EPSG:4326), got EPSG:{keys[2048]}")
    point_registered = keys.get(1025, 1) == 2
    width, height = tiff.tag(256), tiff.tag(257)
    scale, tie = tiff.tags.get(33550), tiff.tags.get(33922)
    if not scale or not tie or tie[:2] != (0.0, 0.0):
        sys.exit("Missing or unusual georeferencing (ModelPixelScale / ModelTiepoint)")
    dx, dy = scale[0], scale[1]
    # Extent of the cells (PixelIsArea) or of the nodes (PixelIsPoint).
    nodes_extra = 1 if point_registered else 0
    spans = (width - nodes_extra) * dx, (height - nodes_extra) * dy
    corner = (tie[3], tie[4])
    if (abs(dx - dy) > 1e-12 or abs(spans[0] - 360.0) > 1e-6 or abs(spans[1] - 180.0) > 1e-6
            or max(abs(corner[0] + 180.0), abs(corner[1] - 90.0)) > 1e-6):
        sys.exit(f"Not a global grid from (-180, 90): {width} x {height}, step {dx} / {dy}, "
                 f"corner {corner}, {'point' if point_registered else 'area'} registration")

    print(f"decoding {width} x {height} ({60.0 * dx:g}'), "
          f"{'node' if point_registered else 'cell'}-registered ...")
    nodata = tiff.tags.get(42113)
    image = tiff.read()
    del raw, tiff
    voids = ~np.isfinite(image) if np.issubdtype(image.dtype, np.floating) else np.zeros(1, bool)
    if nodata:
        voids = voids | (image == float(nodata))
    if voids.any():
        sys.exit(f"The grid has {int(voids.sum())} voids; Orbis needs a complete global grid")
    del voids

    if point_registered:
        nodes = np.round(image).astype(np.int16)
        nodes[0] = int(round(float(image[0].mean())))
        nodes[-1] = int(round(float(image[-1].mean())))
        nodes[:, -1] = nodes[:, 0]
    else:
        nodes = to_nodes(image)
    del image

    print(f"nodes {nodes.shape[1]} x {nodes.shape[0]}, elevation {nodes.min()} .. {nodes.max()} m")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("wb") as f:
        f.write(b"OEG1")
        f.write(struct.pack("<II", nodes.shape[1], nodes.shape[0]))
        f.write(struct.pack("<dddd", -180.0, 180.0, 90.0, -90.0))
        f.write(nodes.astype("<i2").tobytes())
    print(f"wrote {args.output} ({args.output.stat().st_size / 1e6:.1f} MB)")


if __name__ == "__main__":
    main()
