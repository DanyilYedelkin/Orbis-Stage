#!/usr/bin/env python3
"""Converts NOAA ETOPO1 (10 arc-minute, ice surface) into Orbis' elevation grid format.

Source: NOAA ETOPO1 Global Relief Model (public domain, doi:10.7289/V5C8276M), resampled to
10 arc-minutes and distributed as ETOPO_10min_Ice.vtp in the PyVista example-data repository.

Usage:
    python tools/prepare_etopo.py                 # downloads the source file
    python tools/prepare_etopo.py ETOPO_10min_Ice.vtp

Output: data/earth_etopo10.oeg - see src/core/terrain/ElevationGrid.hpp for the format:
    char[4]  magic "OEG1"
    uint32   width, height          (grid nodes; longitude fastest)
    float64  lonMin, lonMax         (degrees, west -> east)
    float64  latMax, latMin         (degrees, north -> south: row 0 is the northernmost)
    int16[height][width]            elevation in metres, little-endian
Only numpy is required.
"""
import base64
import hashlib
import re
import struct
import sys
import urllib.request
import zlib
from pathlib import Path

import numpy as np

SOURCE_URL = ("https://raw.githubusercontent.com/pyvista/vtk-data/master/Data/EarthModels/"
              "ETOPO_10min_Ice.vtp")
SOURCE_SHA256 = "89fff96333d351ad207bce0599c8a1df011853be8aaaa6face1f0c472d6676bf"
OUTPUT = Path(__file__).resolve().parent.parent / "data" / "earth_etopo10.oeg"


def decode_compressed_array(appended: str, offset: int, dtype) -> np.ndarray:
    """VTK 'appended, base64, zlib': a base64 header (UInt32 nblocks, blocksize, lastblocksize,
    compressed sizes...), then the base64-encoded concatenation of zlib blocks."""
    def b64_len(n_bytes: int) -> int:
        return (n_bytes + 2) // 3 * 4

    first = base64.b64decode(appended[offset:offset + 8])
    nblocks = struct.unpack_from("<I", first)[0]
    header_bytes = (3 + nblocks) * 4
    header_len = b64_len(header_bytes)
    header = struct.unpack(f"<{3 + nblocks}I",
                           base64.b64decode(appended[offset:offset + header_len])[:header_bytes])
    compressed_sizes = header[3:]
    data_len = b64_len(sum(compressed_sizes))
    payload = base64.b64decode(appended[offset + header_len:offset + header_len + data_len])

    out, pos = [], 0
    for size in compressed_sizes:
        out.append(zlib.decompress(payload[pos:pos + size]))
        pos += size
    return np.frombuffer(b"".join(out), dtype=dtype)


def main() -> None:
    if len(sys.argv) > 1:
        raw = Path(sys.argv[1]).read_bytes()
    else:
        print(f"Downloading {SOURCE_URL} ...")
        raw = urllib.request.urlopen(SOURCE_URL).read()
    digest = hashlib.sha256(raw).hexdigest()
    if digest != SOURCE_SHA256:
        sys.exit(f"Unexpected SHA-256 {digest}; the source file changed - refusing to convert.")

    text = raw.decode("latin-1")
    offsets = {m.group(1): int(m.group(2)) for m in re.finditer(
        r'<DataArray type="\w+" Name="(\w+)"[^>]*offset="(\d+)"', text)}
    appended = text[text.index('<AppendedData encoding="base64">'):]
    appended = appended[appended.index("_") + 1:]

    altitude = decode_compressed_array(appended, offsets["altitude"], "<f4")
    points = decode_compressed_array(appended, offsets["Points"], "<f4").reshape(-1, 3)

    # Recover the grid layout from the point coordinates instead of assuming it.
    lon = np.degrees(np.arctan2(points[:, 1], points[:, 0]))
    lat = np.degrees(np.arcsin(np.clip(points[:, 2], -1.0, 1.0)))
    width = int(np.argmax(np.abs(np.diff(lat)) > 1e-6)) + 1  # longitude varies fastest
    height = len(lat) // width
    assert width * height == len(lat), "points do not form a regular grid"

    grid = altitude.reshape(height, width).astype(np.float64)
    lat_rows = lat.reshape(height, width)[:, 0]
    # Longitudes of one row, unwrapped to a monotonic sequence (atan2 jumps from +180 to -180).
    lon_row = np.degrees(np.unwrap(np.radians(lon.reshape(height, width)[height // 2, :])))
    step = np.diff(lon_row)
    assert np.allclose(step, step[0], atol=1e-3), "longitude spacing is not uniform"
    if step[0] < 0:                        # stored east -> west: flip
        grid, lon_row = grid[:, ::-1], lon_row[::-1]
    assert abs((lon_row[-1] - lon_row[0]) - 360.0) < 1e-3, "expected a full 360 degree row"
    # Re-centre on Greenwich: columns must run from -180 to +180. The first and last column are
    # the same meridian, so drop one duplicate and append it again at the other end.
    start = lon_row[0] % 360.0             # e.g. 0 for a 0..360 grid
    shift = int(round(((start - (-180.0)) % 360.0) / step[0] * -1)) % (width - 1)
    core = np.roll(grid[:, :-1], shift, axis=1)
    grid = np.concatenate([core, core[:, :1]], axis=1)
    lon_min, lon_max = -180.0, 180.0
    if lat_rows[0] < lat_rows[-1]:         # stored south -> north: flip to north -> south
        grid = grid[::-1, :]
        lat_rows = lat_rows[::-1]

    print(f"grid {width} x {height}, latitude {lat_rows[0]:.3f} .. {lat_rows[-1]:.3f}, "
          f"elevation {grid.min():.0f} .. {grid.max():.0f} m")

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    with OUTPUT.open("wb") as f:
        f.write(b"OEG1")
        f.write(struct.pack("<II", width, height))
        f.write(struct.pack("<dddd", lon_min, lon_max, 90.0, -90.0))
        f.write(np.round(grid).astype("<i2").tobytes())
    print(f"wrote {OUTPUT} ({OUTPUT.stat().st_size / 1e6:.1f} MB)")


if __name__ == "__main__":
    main()
