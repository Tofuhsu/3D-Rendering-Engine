# USC University Park campus buildings

- **Data:** © OpenStreetMap contributors. Available under the
  [Open Database License (ODbL) 1.0](https://opendatacommons.org/licenses/odbl/1-0/);
  see <https://www.openstreetmap.org/copyright>. Any screenshot or video of this
  model must credit "© OpenStreetMap contributors".
- **Source file:** `usc_campus.osm`, downloaded 2026-11-01 from the OSM API
  (`/api/0.6/map?bbox=-118.2930,34.0160,-118.2760,34.0260`), 5,453,385 bytes.
- **Derived model:** `usc_campus.obj` / `.mtl`, made by `tools/osm2obj.py` with
  `--clip-name "University of Southern California"` (buildings whose centroid is
  inside OSM relation 18383936). Regenerate with `tools/fetch_usc_campus.sh`.
- **Result:** 157 buildings, 9,684 triangles; heights from the `height` tag
  (110 buildings), `building:levels` × 3.5 m (21), or a 7 m default (26).
- Geometry and flat colours only; no USC logos or trademarks.
