#!/bin/sh
# Download OpenStreetMap data around USC's University Park campus and convert the
# campus buildings to OBJ. Data (c) OpenStreetMap contributors, ODbL 1.0.
# Uses the OSM API `map` call (one request, ~5 MB); please don't run it in a loop.
set -e
cd "$(dirname "$0")/.."
out=assets/third_party/usc_campus
mkdir -p "$out"
curl -fsS -A "3D-Rendering-Engine osm2obj" -o "$out/usc_campus.osm" \
  "https://api.openstreetmap.org/api/0.6/map?bbox=-118.2930,34.0160,-118.2760,34.0260"
python3 tools/osm2obj.py "$out/usc_campus.osm" "$out/usc_campus.obj" \
  --clip-name "University of Southern California"
