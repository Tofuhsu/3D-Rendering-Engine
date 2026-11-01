#!/usr/bin/env python3
"""Unit tests for tools/osm2obj.py (run: python3 tests/test_osm2obj.py)."""
import math
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import osm2obj  # noqa: E402

# 1 degree of latitude in metres for the projection used by osm2obj.
DEG = math.pi / 180.0 * osm2obj.EARTH_RADIUS_M


def square(lat0, lon0, size_m, cw=False):
    """Closed lat/lon ring of a square (counter-clockwise unless cw)."""
    d = size_m / DEG
    pts = [(lat0, lon0), (lat0, lon0 + d), (lat0 + d, lon0 + d), (lat0 + d, lon0), (lat0, lon0)]
    return [{"lat": a, "lon": b} for a, b in (pts[::-1] if cw else pts)]


def tri_area(poly, tris):
    total = 0.0
    for i, j, k in tris:
        (x0, y0), (x1, y1), (x2, y2) = poly[i], poly[j], poly[k]
        total += 0.5 * abs((x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0))
    return total


def convert(elements, **kw):
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "m.obj"
        stats = osm2obj.convert({"elements": elements}, out, kw.get("parts", False), 7.0, kw.get("clip"))
        return stats, out.read_text()


class LengthParsing(unittest.TestCase):
    def test_units(self):
        self.assertEqual(osm2obj.parse_length("12"), 12.0)
        self.assertEqual(osm2obj.parse_length("12.5 m"), 12.5)
        self.assertAlmostEqual(osm2obj.parse_length("40'"), 12.192)
        self.assertEqual(osm2obj.parse_length("9;15"), 9.0)
        self.assertIsNone(osm2obj.parse_length("tall"))

    def test_height_priority(self):
        self.assertEqual(osm2obj.building_heights({"height": "20", "building:levels": "2"}, 7)[1:], (20.0, "height"))
        self.assertEqual(osm2obj.building_heights({"building:levels": "4"}, 7)[1:], (15.0, "levels"))
        self.assertEqual(osm2obj.building_heights({}, 7)[1:], (7.0, "default"))
        self.assertEqual(osm2obj.building_heights({"height": "20", "min_height": "5"}, 7)[0], 5.0)


class Triangulation(unittest.TestCase):
    def test_concave_polygon_area(self):
        # L shape, area 3.
        poly = [(0, 0), (2, 0), (2, 1), (1, 1), (1, 2), (0, 2)]
        tris, ok = osm2obj.ear_clip(poly)
        self.assertTrue(ok)
        self.assertEqual(len(tris), 4)
        self.assertAlmostEqual(tri_area(poly, tris), 3.0)

    def test_polygon_with_hole_area(self):
        outer = [(0, 0), (10, 0), (10, 10), (0, 10)]
        hole = [(3, 3), (3, 6), (6, 6), (6, 3)]  # clockwise
        poly = osm2obj.merge_holes(outer, [hole])
        tris, ok = osm2obj.ear_clip(poly)
        self.assertTrue(ok)
        self.assertAlmostEqual(tri_area(poly, tris), 100.0 - 9.0, places=6)


class Conversion(unittest.TestCase):
    def test_box_building(self):
        way = {"type": "way", "id": 1, "tags": {"building": "university", "height": "30"},
               "geometry": square(34.02, -118.28, 20)}
        stats, obj = convert([way])
        self.assertEqual(stats["buildings"], 1)
        self.assertEqual(stats["height_tag"], 1)
        self.assertEqual(stats["triangles"], 4 * 2 + 2 + 2)  # walls + roof + ground
        self.assertIn("usemtl Academic", obj)
        tops = [float(l.split()[2]) for l in obj.splitlines() if l.startswith("v ")]
        self.assertAlmostEqual(max(tops), 30.0)

    def test_clockwise_footprint_gets_outward_walls(self):
        way = {"type": "way", "id": 1, "tags": {"building": "yes"},
               "geometry": square(34.02, -118.28, 20, cw=True)}
        _, obj = convert([way])
        normals = [tuple(map(float, l.split()[1:])) for l in obj.splitlines() if l.startswith("vn ")]
        # The four wall normals are the four compass directions, not doubled.
        walls = {n for n in normals if abs(n[1]) < 1e-6}
        self.assertEqual(walls, {(1.0, 0.0, 0.0), (-1.0, 0.0, 0.0), (0.0, 0.0, 1.0), (0.0, 0.0, -1.0)})

    def test_multipolygon_with_courtyard(self):
        rel = {"type": "relation", "id": 2, "tags": {"building": "university", "type": "multipolygon"},
               "members": [{"type": "way", "role": "outer", "geometry": square(34.02, -118.28, 40)},
                           {"type": "way", "role": "inner", "geometry": square(34.02 + 10 / DEG, -118.28 + 10 / DEG, 10)}]}
        stats, _ = convert([rel])
        self.assertEqual(stats["with_holes"], 1)
        self.assertEqual(stats["fan_fallbacks"], 0)

    def test_clip_by_campus_outline(self):
        campus = {"type": "way", "id": 9, "tags": {"amenity": "university", "name": "Campus"},
                  "geometry": square(34.0, -118.3, 500)}
        inside = {"type": "way", "id": 1, "tags": {"building": "yes"}, "geometry": square(34.001, -118.299, 20)}
        outside = {"type": "way", "id": 2, "tags": {"building": "yes"}, "geometry": square(34.1, -118.1, 20)}
        stats, _ = convert([campus, inside, outside], clip="Campus")
        self.assertEqual(stats["buildings"], 1)
        self.assertEqual(stats["outside_clip"], 1)


if __name__ == "__main__":
    unittest.main()
