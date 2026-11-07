#!/usr/bin/env python3
"""Fits ClimateParameters (src/core/terrain/Climate.hpp) to station climatology.

January and July mean temperatures (C) of 40 stations, rounded from the WMO 1991-2020 normals
as published by the national weather services. The oceanicity is computed exactly as in
Climate.cpp, from data/earth_etopo10.oeg. Needs numpy and scipy.
"""
import struct
from pathlib import Path

import numpy as np
from scipy.optimize import least_squares

STATIONS = [  # name, latitude, longitude, elevation m, January, July
    ("Singapore", 1.3, 103.8, 15, 26.8, 27.9), ("Cairo", 30.0, 31.2, 23, 14.0, 28.0),
    ("Madrid", 40.4, -3.7, 667, 6.3, 25.6), ("Rome", 41.9, 12.5, 20, 8.0, 25.0),
    ("Kosice", 48.7, 21.3, 206, -2.5, 20.0), ("Paris", 48.85, 2.35, 35, 5.0, 20.0),
    ("Berlin", 52.5, 13.4, 34, 0.5, 19.5), ("Moscow", 55.75, 37.6, 156, -6.5, 19.2),
    ("Stockholm", 59.3, 18.1, 44, -1.5, 18.0), ("Helsinki", 60.2, 24.9, 51, -4.0, 18.0),
    ("Yakutsk", 62.0, 129.7, 98, -38.6, 19.5), ("Tromso", 69.7, 18.9, 10, -4.1, 12.5),
    ("Murmansk", 69.0, 33.1, 51, -10.0, 13.0), ("Utqiagvik", 71.3, -156.8, 10, -25.0, 5.0),
    ("Alert", 82.5, -62.3, 30, -32.0, 3.3), ("Ottawa", 45.4, -75.7, 70, -10.0, 21.0),
    ("Chicago", 41.9, -87.6, 180, -4.6, 23.3), ("Denver", 39.7, -105.0, 1600, -1.0, 23.5),
    ("Tokyo", 35.7, 139.7, 40, 5.4, 25.8), ("Beijing", 39.9, 116.4, 44, -3.1, 26.7),
    ("Buenos Aires", -34.6, -58.4, 25, 24.5, 11.0), ("Cape Town", -33.9, 18.4, 10, 21.5, 12.5),
    ("Melbourne", -37.8, 145.0, 30, 20.5, 10.5), ("Ushuaia", -54.8, -68.3, 50, 9.5, 1.5),
    ("Punta Arenas", -53.0, -70.9, 37, 11.5, 2.3), ("McMurdo", -77.8, 166.7, 10, -3.0, -27.0),
    ("Nairobi", -1.3, 36.8, 1795, 19.0, 15.8), ("Mexico City", 19.4, -99.1, 2240, 14.0, 17.5),
    ("Delhi", 28.6, 77.2, 216, 14.0, 31.0), ("Sydney", -33.9, 151.2, 6, 23.0, 13.0),
    ("Reykjavik", 64.1, -21.9, 20, 0.0, 11.5), ("Anchorage", 61.2, -149.9, 40, -8.0, 15.5),
    ("Winnipeg", 49.9, -97.1, 240, -16.0, 20.0), ("Longyearbyen", 78.2, 15.6, 28, -14.0, 6.5),
    ("Resolute", 74.7, -94.8, 67, -32.0, 4.5), ("Dikson", 73.5, 80.4, 42, -24.5, 5.0),
    ("Orcadas", -60.7, -44.7, 6, 0.9, -9.7), ("Casey", -66.3, 110.5, 30, 0.3, -14.5),
    ("Mawson", -67.6, 62.9, 16, 0.0, -18.5), ("Dumont d'Urville", -66.7, 140.0, 43, -0.6, -16.8),
]
LAPSE = 0.0065
COLDEST_DAY = 20.0
WIDTH, HEIGHT = 720, 360
UPWIND_LENGTH, UPWIND_RANGE, STEP = 1.2e6, 4.0e6, 5.0e4
RADIUS = 6371000.0


def sea_mask() -> np.ndarray:
    data = (Path(__file__).resolve().parent.parent / "data" / "earth_etopo10.oeg").read_bytes()
    w, h = struct.unpack_from("<II", data, 4)
    grid = np.frombuffer(data, "<i2", offset=44).reshape(h, w)
    lat = 90.0 - (np.arange(HEIGHT) + 0.5) * 180.0 / HEIGHT
    lon = -180.0 + (np.arange(WIDTH) + 0.5) * 360.0 / WIDTH
    rows = np.round((90.0 - lat) / 180.0 * (h - 1)).astype(int)
    cols = np.round((lon + 180.0) / 360.0 * (w - 1)).astype(int)
    return grid[rows][:, cols] < 0  # nearest node; Climate.cpp samples the grid itself


def westerly(lat_deg: float) -> float:
    return float(np.interp(abs(lat_deg), [0, 25, 35, 60, 70, 90], [-1, -1, 1, 1, -1, -1]))


def oceanicity_map(sea: np.ndarray) -> np.ndarray:
    out = np.zeros((HEIGHT, WIDTH))
    steps = np.arange(0.0, UPWIND_RANGE, STEP)
    weights = np.exp(-steps / UPWIND_LENGTH)
    weights /= weights.sum()
    for r in range(HEIGHT):
        lat = 90.0 - (r + 0.5) * 180.0 / HEIGHT
        cells = steps / (RADIUS * max(np.cos(np.radians(lat)), 0.2)) / np.radians(360.0 / WIDTH)
        west = (np.arange(WIDTH)[:, None] - np.round(cells)[None, :]).astype(int) % WIDTH
        east = (np.arange(WIDTH)[:, None] + np.round(cells)[None, :]).astype(int) % WIDTH
        s = westerly(lat)
        out[r] = (0.5 * (1 + s) * (sea[r][west] @ weights) +
                  0.5 * (1 - s) * (sea[r][east] @ weights))
    return out


def bilinear(field: np.ndarray, lat: float, lon: float) -> float:
    x = (lon + 180.0) / 360.0 * WIDTH - 0.5
    y = np.clip((90.0 - lat) / 180.0 * HEIGHT - 0.5, 0, HEIGHT - 1)
    x0, y0 = int(np.floor(x)), min(int(np.floor(y)), HEIGHT - 2)
    tx, ty = x - x0, y - y0
    a = (1 - tx) * field[y0, x0 % WIDTH] + tx * field[y0, (x0 + 1) % WIDTH]
    b = (1 - tx) * field[y0 + 1, x0 % WIDTH] + tx * field[y0 + 1, (x0 + 1) % WIDTH]
    return (1 - ty) * a + ty * b


def temperature(p, lat, oceanic, day):
    teq, drop, extra, aeq, apol, south, cont, warmth, antarctic = p
    s2 = np.sin(np.radians(lat)) ** 2
    # Around Antarctica the sea freezes: no damping of the seasons, and the ice sheet's cold.
    ice = np.clip((-lat - 50.0) / 20.0, 0.0, 1.0)
    ice = ice * ice * (3.0 - 2.0 * ice)
    mean = teq - drop * s2 - extra * s2 ** 4 + warmth * (oceanic - 0.5) * s2 - antarctic * ice
    south_factor = south + (1.0 - south) * ice
    amp = (aeq + apol * s2) * np.where(lat < 0, south_factor, 1.0) * (1 + cont * (0.5 - oceanic))
    sign = np.where(lat < 0, -1.0, 1.0)
    return mean - sign * amp * np.cos(2 * np.pi * (day - COLDEST_DAY) / 365.25)


def main() -> None:
    field = oceanicity_map(sea_mask())
    lat = np.array([s[1] for s in STATIONS])
    elevation = np.array([s[3] for s in STATIONS])
    oceanic = np.array([bilinear(field, s[1], s[2]) for s in STATIONS])
    observed = np.concatenate([[s[4] for s in STATIONS], [s[5] for s in STATIONS]])

    def residuals(p):
        return np.concatenate([temperature(p, lat, oceanic, 15.0),
                               temperature(p, lat, oceanic, 196.0)]) - LAPSE * np.tile(elevation, 2) - observed

    fit = least_squares(residuals, [28, 28, 15, 2, 15, 0.7, 1, 8, 5], loss="soft_l1", f_scale=3.0)
    error = residuals(fit.x)
    names = ["equatorTemperature", "polarDrop", "polarExtraDrop", "equatorAmplitude",
             "polarAmplitude", "southernDamping", "continentality", "maritimeWarmth",
             "antarcticCold"]
    for name, value in zip(names, fit.x):
        print(f"{name} = {value:.2f}")
    print(f"median |error| {np.median(np.abs(error)):.1f} C, rms {np.sqrt(np.mean(error ** 2)):.1f} C")
    for s, o, jan, jul in zip(STATIONS, oceanic, error[:len(STATIONS)], error[len(STATIONS):]):
        print(f"  {s[0]:13s} oceanicity {o:.2f}  January {jan:+5.1f}  July {jul:+5.1f}")


if __name__ == "__main__":
    main()
