# High-resolution elevation tiles (optional)

Put SRTM-format `.hgt` tiles in `data/hgt/` (create the folder, or pass `--hgt dir`). Orbis
indexes them at startup and streams them in on demand around the camera, under a memory budget
(`--tile-budget MB`, default 1536), using them instead of the 18 km base data and blending over
4 km towards anything that is not loaded. The folder can be as large as you like.

**Recommended for the Alps:** the void-filled DEMs by Jonathan de Ferranti on
<http://viewfinderpanoramas.org> (section "Digital elevation data"): 3 arc-second (~90 m) or
1 arc-second (~30 m, Alps available) tiles. Download the zip files covering the area, unzip, and
copy the `.hgt` files here. 3" and 1" tiles may be mixed. SRTM tiles from NASA / USGS work the
same way.

Good starter set (Mont Blanc - Matterhorn - Bernese Oberland): `N45E006`, `N45E007`, `N46E007`,
`N46E008`.

Check that it worked:

```
orbis --check-alps
```

prints the real height of famous summits and valleys next to what Orbis now renders. These files
are large and are not committed (see `.gitignore`). Please keep the data provider's attribution
if you publish images.
