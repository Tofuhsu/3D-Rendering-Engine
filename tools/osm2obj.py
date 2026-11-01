#!/usr/bin/env python3
"""osm2obj: OpenStreetMap buildings -> extruded OBJ/MTL for the engine.

Input is either an OSM XML file (e.g. the OSM API `map` call, see
tools/fetch_usc_campus.sh) or Overpass API JSON with inline geometry
(`out geom;`). Each building footprint becomes vertical walls plus
a flat roof (ear-clipping triangulation, holes supported) on a ground plane.
Coordinates are metres in a local east/up/south frame centred on the data.

Heights, in order of preference: `height` tag, `building:levels` x 3.5 m,
otherwise a default (counted in the report). `min_height` /
`building:min_level` raise the base.

usage: tools/osm2obj.py <input.osm|input.json> <out.obj> [--clip-name NAME]
                         [--include-parts] [--default-height M]

--clip-name keeps only buildings whose centroid lies inside the outline of the
amenity=university way/relation with that exact name.

Data (c) OpenStreetMap contributors, ODbL 1.0. Credit it wherever the model is shown.
"""
import argparse
import json
import math
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

EARTH_RADIUS_M = 6378137.0
METRES_PER_LEVEL = 3.5

# Material classes: building tag values -> name, diffuse colour.
MATERIALS = {
    "Academic": ((0.62, 0.30, 0.22), {"university", "college", "school", "library", "civic", "public"}),
    "Residential": ((0.80, 0.68, 0.50), {"dormitory", "residential", "apartments", "house", "hotel"}),
    "Sports": ((0.60, 0.62, 0.68), {"stadium", "sports_hall", "sports_centre", "grandstand", "pavilion"}),
    "Parking": ((0.45, 0.45, 0.47), {"parking", "garage", "garages", "carport"}),
    "Religious": ((0.85, 0.82, 0.72), {"church", "chapel", "religious"}),
    "Other": ((0.76, 0.73, 0.68), set()),
}
GROUND = ("Ground", (0.33, 0.42, 0.30))


def material_for(tags):
    value = tags.get("building", tags.get("building:part", "yes"))
    if tags.get("amenity") == "parking":
        return "Parking"
    for name, (_, values) in MATERIALS.items():
        if value in values:
            return name
    return "Other"


def parse_length(text):
    """'12', '12 m', '12.5m', "40'", '40 ft', '12;15' -> metres (first value) or None."""
    if not text:
        return None
    text = text.split(";")[0].strip().lower()
    m = re.match(r"^([0-9]*\.?[0-9]+)\s*(m|ft|')?$", text)
    if not m:
        return None
    value = float(m.group(1))
    return value * 0.3048 if m.group(2) in ("ft", "'") else value


def parse_number(text):
    try:
        return float(text.split(";")[0])
    except (AttributeError, ValueError):
        return None


def building_heights(tags, default_height):
    """Return (base, top, source) in metres."""
    top = parse_length(tags.get("height"))
    source = "height"
    if top is None:
        levels = parse_number(tags.get("building:levels"))
        if levels is not None and levels > 0:
            top = levels * METRES_PER_LEVEL + (1.0 if tags.get("roof:levels") is None else 0.0)
            source = "levels"
    if top is None:
        top, source = default_height, "default"
    base = parse_length(tags.get("min_height"))
    if base is None:
        min_level = parse_number(tags.get("building:min_level"))
        base = min_level * METRES_PER_LEVEL if min_level else 0.0
    base = min(base, top - 0.5)
    return max(base, 0.0), top, source


# ---------------------------------------------------------------- geometry

def signed_area(ring):
    a = 0.0
    for i in range(len(ring)):
        x0, y0 = ring[i]
        x1, y1 = ring[(i + 1) % len(ring)]
        a += x0 * y1 - x1 * y0
    return 0.5 * a


def clean_ring(ring):
    """Drop the closing point, repeated points and collinear points."""
    out = []
    for p in ring:
        if not out or math.dist(p, out[-1]) > 1e-3:
            out.append(p)
    if len(out) > 1 and math.dist(out[0], out[-1]) <= 1e-3:
        out.pop()
    changed = True
    while changed and len(out) >= 3:
        changed = False
        for i in range(len(out)):
            a, b, c = out[i - 1], out[i], out[(i + 1) % len(out)]
            cross = (b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0])
            if abs(cross) < 1e-6:
                del out[i]
                changed = True
                break
    return out


def point_in_triangle(p, a, b, c):
    def side(p1, p2, p3):
        return (p1[0] - p3[0]) * (p2[1] - p3[1]) - (p2[0] - p3[0]) * (p1[1] - p3[1])
    d1, d2, d3 = side(p, a, b), side(p, b, c), side(p, c, a)
    neg = d1 < 0 or d2 < 0 or d3 < 0
    pos = d1 > 0 or d2 > 0 or d3 > 0
    return not (neg and pos)


def segments_intersect(p1, p2, p3, p4):
    def orient(a, b, c):
        v = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
        return (v > 1e-9) - (v < -1e-9)
    o1, o2, o3, o4 = orient(p1, p2, p3), orient(p1, p2, p4), orient(p3, p4, p1), orient(p3, p4, p2)
    return o1 != o2 and o3 != o4 and 0 not in (o1, o2, o3, o4)


def merge_holes(outer, holes):
    """Join each hole to the outer ring with a two-way bridge (outer CCW, holes CW).

    Returns one simple-ish polygon (list of points) suitable for ear clipping.
    """
    poly = list(outer)
    for hole in sorted(holes, key=lambda h: -max(p[0] for p in h)):
        hi = max(range(len(hole)), key=lambda i: hole[i][0])
        hp = hole[hi]
        edges = [(poly[i], poly[(i + 1) % len(poly)]) for i in range(len(poly))]
        edges += [(hole[i], hole[(i + 1) % len(hole)]) for i in range(len(hole))]
        best = None
        for i, q in enumerate(poly):
            if any(segments_intersect(hp, q, e0, e1) for e0, e1 in edges):
                continue
            d = math.dist(hp, q)
            if best is None or d < best[0]:
                best = (d, i)
        if best is None:
            continue  # no visible bridge; skip this hole (roof covers it)
        i = best[1]
        rotated = hole[hi:] + hole[:hi]
        poly = poly[: i + 1] + rotated + [hp, poly[i]] + poly[i + 1:]
    return poly


def ear_clip(poly):
    """Triangulate a CCW polygon. Returns (triangles as index triples, ok)."""
    idx = list(range(len(poly)))
    tris = []
    guard = 0
    while len(idx) > 3 and guard < 10 * len(poly) * len(poly):
        guard += 1
        n = len(idx)
        clipped = False
        for k in range(n):
            i0, i1, i2 = idx[k - 1], idx[k], idx[(k + 1) % n]
            a, b, c = poly[i0], poly[i1], poly[i2]
            cross = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
            if cross <= 1e-12:
                continue  # reflex or degenerate corner
            if any(point_in_triangle(poly[j], a, b, c) and poly[j] not in (a, b, c)
                   for j in idx if j not in (i0, i1, i2)):
                continue
            tris.append((i0, i1, i2))
            del idx[k]
            clipped = True
            break
        if not clipped:
            # Self-intersecting or badly bridged input: fan the rest.
            tris += [(idx[0], idx[j], idx[j + 1]) for j in range(1, len(idx) - 1)]
            return tris, False
    if len(idx) == 3:
        tris.append(tuple(idx))
    return tris, True


def assemble_rings(ways):
    """Join way segments (lists of points) that share endpoints into closed rings."""
    pending = [list(w) for w in ways if len(w) >= 2]
    rings = []
    while pending:
        ring = pending.pop(0)
        progress = True
        while math.dist(ring[0], ring[-1]) > 1e-6 and progress:
            progress = False
            for i, seg in enumerate(pending):
                if math.dist(seg[0], ring[-1]) < 1e-6:
                    ring += seg[1:]
                elif math.dist(seg[-1], ring[-1]) < 1e-6:
                    ring += seg[::-1][1:]
                elif math.dist(seg[-1], ring[0]) < 1e-6:
                    ring = seg[:-1] + ring
                elif math.dist(seg[0], ring[0]) < 1e-6:
                    ring = seg[::-1][:-1] + ring
                else:
                    continue
                pending.pop(i)
                progress = True
                break
        if math.dist(ring[0], ring[-1]) <= 1e-6 and len(ring) >= 4:
            rings.append(ring)
    return rings


# ---------------------------------------------------------------- input

def read_osm_xml(path):
    """OSM XML -> Overpass-style elements with inline geometry."""
    root = ET.parse(path).getroot()
    nodes, ways, elements = {}, {}, []
    for e in root:
        if e.tag == "node":
            nodes[e.get("id")] = (float(e.get("lat")), float(e.get("lon")))
    for e in root:
        tags = {t.get("k"): t.get("v") for t in e.findall("tag")}
        if e.tag == "way":
            geom = [nodes[n.get("ref")] for n in e.findall("nd") if n.get("ref") in nodes]
            ways[e.get("id")] = geom
            elements.append({"type": "way", "id": int(e.get("id")), "tags": tags,
                             "geometry": [{"lat": a, "lon": b} for a, b in geom]})
    for e in root:
        if e.tag == "relation":
            tags = {t.get("k"): t.get("v") for t in e.findall("tag")}
            members = []
            for m in e.findall("member"):
                if m.get("type") == "way" and m.get("ref") in ways:
                    members.append({"type": "way", "role": m.get("role"),
                                    "geometry": [{"lat": a, "lon": b} for a, b in ways[m.get("ref")]]})
            elements.append({"type": "relation", "id": int(e.get("id")), "tags": tags, "members": members})
    return {"elements": elements}


def point_in_polygon(p, ring):
    inside = False
    for i in range(len(ring)):
        (y0, x0), (y1, x1) = ring[i], ring[i - 1]
        if (y0 > p[0]) != (y1 > p[0]) and p[1] < (x1 - x0) * (p[0] - y0) / (y1 - y0) + x0:
            inside = not inside
    return inside


def clip_polygons(elements, name):
    """Outer rings (lat/lon) of the amenity=university element(s) with this name."""
    rings = []
    for e in elements:
        tags = e.get("tags", {})
        if tags.get("amenity") != "university" or tags.get("name") != name:
            continue
        if e["type"] == "way":
            rings.append([(g["lat"], g["lon"]) for g in e["geometry"]])
        else:
            outers = [[(g["lat"], g["lon"]) for g in m["geometry"]]
                      for m in e.get("members", []) if m.get("role") in ("outer", "")]
            rings += assemble_rings(outers)
    return rings


# ---------------------------------------------------------------- conversion

def collect_buildings(elements, include_parts):
    """Yield (id, tags, outer_rings, inner_rings) with rings in lat/lon."""
    key_ok = lambda t: "building" in t or (include_parts and "building:part" in t)
    for e in elements:
        tags = e.get("tags", {})
        if not key_ok(tags):
            continue
        if e["type"] == "way" and "geometry" in e:
            ring = [(g["lat"], g["lon"]) for g in e["geometry"] if g]
            yield f"way/{e['id']}", tags, [ring], []
        elif e["type"] == "relation" and tags.get("type") == "multipolygon":
            outers = [[(g["lat"], g["lon"]) for g in m.get("geometry", []) if g]
                      for m in e.get("members", []) if m.get("role") == "outer"]
            inners = [[(g["lat"], g["lon"]) for g in m.get("geometry", []) if g]
                      for m in e.get("members", []) if m.get("role") == "inner"]
            yield f"relation/{e['id']}", tags, assemble_rings(outers), assemble_rings(inners)


def convert(data, out_obj, include_parts, default_height, clip_name=None):
    elements = data.get("elements", [])
    buildings = list(collect_buildings(elements, include_parts))
    clipped_out = 0
    if clip_name:
        clip = clip_polygons(elements, clip_name)
        if not clip:
            sys.exit(f"no amenity=university outline named {clip_name!r}")
        kept = []
        for b in buildings:
            pts = [p for r in b[2] for p in r]
            if pts:
                c = (sum(p[0] for p in pts) / len(pts), sum(p[1] for p in pts) / len(pts))
                if any(point_in_polygon(c, r) for r in clip):
                    kept.append(b)
                    continue
            clipped_out += 1
        buildings = kept
    if not buildings:
        sys.exit("no buildings in input")

    lats = [p[0] for _, _, outers, _ in buildings for r in outers for p in r]
    lons = [p[1] for _, _, outers, _ in buildings for r in outers for p in r]
    lat0, lon0 = 0.5 * (min(lats) + max(lats)), 0.5 * (min(lons) + max(lons))
    k = math.pi / 180.0 * EARTH_RADIUS_M

    def project(lat, lon):
        # Local equirectangular projection: (east, north) in metres.
        return ((lon - lon0) * k * math.cos(math.radians(lat0)), (lat - lat0) * k)

    out_obj = Path(out_obj)
    mtl_name = out_obj.with_suffix(".mtl").name
    v_lines, vn_lines, faces = [], [], {}  # faces: material -> list of (object name, [face lines])
    stats = {"buildings": 0, "triangles": 0, "height_tag": 0, "levels_tag": 0, "default_height": 0,
             "with_holes": 0, "fan_fallbacks": 0, "skipped_degenerate": 0, "named": 0}
    normal_index = {}

    def vn(n):
        if n not in normal_index:
            vn_lines.append("vn %.5f %.5f %.5f" % n)
            normal_index[n] = len(vn_lines)
        return normal_index[n]

    def v(e, y, n_):
        # World: x = east, y = up, z = south.
        v_lines.append("v %.3f %.3f %.3f" % (e, y, -n_))
        return len(v_lines)

    xs, zs = [], []
    for ident, tags, outers, inners in buildings:
        base, top, source = building_heights(tags, default_height)
        mat = material_for(tags)
        lines = []
        tris_here = 0
        for outer in outers:
            ring = clean_ring([project(*p) for p in outer])
            if len(ring) < 3 or abs(signed_area(ring)) < 1.0:
                stats["skipped_degenerate"] += 1
                continue
            if signed_area(ring) < 0:
                ring.reverse()
            holes = []
            for inner in inners:
                h = clean_ring([project(*p) for p in inner])
                if len(h) >= 3:
                    if signed_area(h) > 0:
                        h.reverse()
                    holes.append(h)
            xs += [p[0] for p in ring]
            zs += [-p[1] for p in ring]

            # Walls: outer ring CCW and holes CW, so (dn, -de) points out of the solid.
            for loop in [ring] + holes:
                for i in range(len(loop)):
                    (e0, n0), (e1, n1) = loop[i], loop[(i + 1) % len(loop)]
                    de, dn = e1 - e0, n1 - n0
                    length = math.hypot(de, dn)
                    if length < 1e-6:
                        continue
                    ni = vn((round(dn / length, 5), 0.0, round(de / length, 5)))
                    a, b = v(e0, base, n0), v(e1, base, n1)
                    c, d = v(e1, top, n1), v(e0, top, n0)
                    lines.append(f"f {a}//{ni} {b}//{ni} {c}//{ni} {d}//{ni}")
                    tris_here += 2

            # Roof: flat, triangulated, normal up.
            poly = merge_holes(ring, holes) if holes else ring
            tris, ok = ear_clip(poly)
            stats["fan_fallbacks"] += 0 if ok else 1
            up = vn((0.0, 1.0, 0.0))
            ids = [v(p[0], top, p[1]) for p in poly]
            for t in tris:
                lines.append("f " + " ".join(f"{ids[j]}//{up}" for j in t))
            tris_here += len(tris)
            if holes:
                stats["with_holes"] += 1
        if not lines:
            continue
        name = tags.get("name", ident)
        stats["named"] += 1 if "name" in tags else 0
        stats["buildings"] += 1
        stats["triangles"] += tris_here
        stats[{"height": "height_tag", "levels": "levels_tag", "default": "default_height"}[source]] += 1
        faces.setdefault(mat, []).append((re.sub(r"\s+", "_", name), lines))

    # Ground plane with a 40 m margin.
    margin = 40.0
    x0, x1, z0, z1 = min(xs) - margin, max(xs) + margin, min(zs) - margin, max(zs) + margin
    up = vn((0.0, 1.0, 0.0))
    g = [v(x0, -0.05, -z0), v(x0, -0.05, -z1), v(x1, -0.05, -z1), v(x1, -0.05, -z0)]
    faces[GROUND[0]] = [("Ground", [f"f {g[0]}//{up} {g[1]}//{up} {g[2]}//{up} {g[3]}//{up}"])]
    stats["triangles"] += 2

    with open(out_obj, "w") as f:
        f.write("# USC University Park campus buildings, generated by tools/osm2obj.py\n")
        f.write("# Data (c) OpenStreetMap contributors, ODbL 1.0 - https://www.openstreetmap.org/copyright\n")
        f.write(f"# origin lat {lat0:.6f} lon {lon0:.6f}; x = east, y = up, z = south (metres)\n")
        f.write(f"mtllib {mtl_name}\n")
        f.write("\n".join(v_lines) + "\n")
        f.write("\n".join(vn_lines) + "\n")
        for mat, objects in faces.items():
            f.write(f"usemtl {mat}\n")
            for name, lines in objects:
                f.write(f"o {name}\n" + "\n".join(lines) + "\n")

    with open(out_obj.with_suffix(".mtl"), "w") as f:
        f.write("# Materials for osm2obj output (flat colours, no textures)\n")
        for name, (kd, _) in list(MATERIALS.items()) + [(GROUND[0], (GROUND[1], None))]:
            f.write(f"newmtl {name}\nKa {kd[0]} {kd[1]} {kd[2]}\nKd {kd[0]} {kd[1]} {kd[2]}\n"
                    "Ks 0.05 0.05 0.05\nNs 16\n\n")

    stats["outside_clip"] = clipped_out
    stats["extent_m"] = [round(x1 - x0, 1), round(z1 - z0, 1)]
    stats["origin"] = [round(lat0, 6), round(lon0, 6)]
    stats["per_material"] = {m: len(o) for m, o in faces.items()}
    return stats


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("input")
    ap.add_argument("output")
    ap.add_argument("--include-parts", action="store_true", help="also extrude building:part outlines")
    ap.add_argument("--default-height", type=float, default=7.0, help="metres when no height/levels tag")
    ap.add_argument("--clip-name", help="keep buildings inside this amenity=university outline")
    args = ap.parse_args()
    if args.input.endswith((".osm", ".xml")):
        data = read_osm_xml(args.input)
    else:
        with open(args.input) as f:
            data = json.load(f)
    stats = convert(data, args.output, args.include_parts, args.default_height, args.clip_name)
    print(json.dumps(stats, indent=2))


if __name__ == "__main__":
    main()
