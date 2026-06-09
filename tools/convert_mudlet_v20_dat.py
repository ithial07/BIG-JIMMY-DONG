#!/usr/bin/env python3
"""
Convert a Mudlet binary map .dat file (tested with map format v20 / Qt 5.12
QDataStream layout) into the compact JSON schema used by ArdaBestClient.

Usage:
  python tools/convert_mudlet_v20_dat.py input.dat output.json
"""
from __future__ import annotations
import json, re, struct, sys
from pathlib import Path

COMMANDS = ["n", "ne", "e", "se", "s", "sw", "w", "nw", "up", "down", "in", "out"]

class Reader:
    def __init__(self, b: bytes):
        self.b = b
        self.o = 0
    def rem(self): return len(self.b) - self.o
    def tell(self): return self.o
    def u8(self):
        if self.o >= len(self.b): raise EOFError((self.o, len(self.b)))
        v = self.b[self.o]; self.o += 1; return v
    def i8(self):
        if self.o >= len(self.b): raise EOFError((self.o, len(self.b)))
        v = struct.unpack_from(">b", self.b, self.o)[0]; self.o += 1; return v
    def u16(self):
        if self.o + 2 > len(self.b): raise EOFError((self.o, len(self.b)))
        v = struct.unpack_from(">H", self.b, self.o)[0]; self.o += 2; return v
    def u32(self):
        if self.o + 4 > len(self.b): raise EOFError((self.o, len(self.b)))
        v = struct.unpack_from(">I", self.b, self.o)[0]; self.o += 4; return v
    def i32(self):
        if self.o + 4 > len(self.b): raise EOFError((self.o, len(self.b)))
        v = struct.unpack_from(">i", self.b, self.o)[0]; self.o += 4; return v
    def f64(self):
        if self.o + 8 > len(self.b): raise EOFError((self.o, len(self.b)))
        v = struct.unpack_from(">d", self.b, self.o)[0]; self.o += 8; return v
    def qbool(self): return bool(self.u8())
    def qstr(self):
        n = self.u32()
        if n == 0xffffffff:
            return None
        if n > self.rem():
            raise ValueError(f"QString length {n} exceeds remaining {self.rem()} at {self.o}")
        s = self.b[self.o:self.o+n].decode("utf-16-be", errors="replace")
        self.o += n
        return s
    def qcolor(self):
        spec = self.u8()
        a = self.u16(); r = self.u16(); g = self.u16(); b = self.u16(); _pad = self.u16()
        return {"spec": spec, "a": a // 257, "r": r // 257, "g": g // 257, "b": b // 257}
    def qfont(self):
        return {
            "family": self.qstr(),
            "styleName": self.qstr(),
            "pointSize": self.f64(),
            "pixelSize": self.i32(),
            "styleHint": self.u8(),
            "styleStrategy": self.u16(),
            "charSet": self.u8(),
            "weight": self.u8(),
            "bits": self.u8(),
            "stretch": self.u16(),
            "extendedBits": self.u8(),
            "letterSpacing": self.i32(),
            "wordSpacing": self.i32(),
            "hintingPreference": self.u8(),
            "capitalization": self.u8(),
        }
    def qmap(self, kr, vr, maxn=10_000_000):
        n = self.u32()
        if n > maxn:
            raise ValueError(f"huge QMap/QHash count {n} at offset {self.o - 4}")
        return [(kr(), vr()) for _ in range(n)]
    def qlist(self, vr, maxn=10_000_000):
        n = self.u32()
        if n > maxn:
            raise ValueError(f"huge QList/QSet count {n} at offset {self.o - 4}")
        return [vr() for _ in range(n)]
    qset = qlist
    def vec3(self): return [self.f64(), self.f64(), self.f64()]
    def pointf(self): return [self.f64(), self.f64()]
    def sizef(self): return [self.f64(), self.f64()]

def parse_custom_lines(r: Reader):
    return dict(r.qmap(r.qstr, lambda: r.qlist(r.pointf)))

def parse_room(r: Reader, version: int):
    rid = r.i32()
    area = r.i32()
    x = r.i32(); y = r.i32(); z = r.i32()
    raw_exits = [r.i32() for _ in range(12)]
    exits = {cmd: dst for cmd, dst in zip(COMMANDS, raw_exits) if dst != -1}
    environment = r.i32()
    weight = r.i32()
    if version < 8:
        for _ in range(4): r.f64()
    name = r.qstr() or ""
    locked = r.qbool()

    special = {}
    special_locks = []
    if version >= 21:
        hidden = r.qbool()
        special = dict(r.qmap(r.qstr, r.i32))
    elif version >= 6:
        for dest, cmd in r.qmap(r.i32, r.qstr):
            if not cmd:
                continue
            if cmd.startswith("1"):
                special_locks.append(cmd[1:])
                cmd = cmd[1:]
            elif cmd.startswith("0"):
                cmd = cmd[1:]
            if cmd:
                special[cmd] = dest
    if version >= 19:
        symbol = r.qstr() or ""
    elif version >= 9:
        c = r.i8(); symbol = chr(c) if c > 32 else ""
    else:
        symbol = ""

    if version >= 21:
        _symbol_color = r.qcolor()
    user_data = dict(r.qmap(r.qstr, r.qstr)) if version >= 10 else {}

    custom_lines = {}
    if version >= 11:
        if version >= 20:
            custom_lines = parse_custom_lines(r)
            _custom_lines_arrow = dict(r.qmap(r.qstr, r.qbool))
            _custom_lines_color = dict(r.qmap(r.qstr, r.qcolor))
            _custom_lines_style = dict(r.qmap(r.qstr, r.i32))
        if version >= 21:
            special_locks = r.qset(r.qstr)
        exit_locks = r.qlist(r.i32)
    else:
        exit_locks = []
    exit_stubs = r.qlist(r.i32) if version >= 13 else []
    if version >= 16:
        exit_weights = dict(r.qmap(r.qstr, r.i32))
        doors = dict(r.qmap(r.qstr, r.i32))
    else:
        exit_weights = {}
        doors = {}

    terrain = ""
    m = re.search(r"\[\s*([^\]]+?)\s*\]", name)
    if m:
        terrain = m.group(1).strip()

    obj = {
        "id": rid, "area": area, "x": x, "y": y, "z": z,
        "name": name, "terrain": terrain,
        "env": environment, "weight": max(1, weight), "locked": locked,
        "exits": exits,
    }
    if special: obj["special"] = special
    if doors: obj["doors"] = doors
    if exit_locks: obj["exitLocks"] = exit_locks
    if exit_stubs: obj["exitStubs"] = exit_stubs
    if exit_weights: obj["exitWeights"] = exit_weights
    if symbol: obj["symbol"] = symbol
    if user_data: obj["userData"] = user_data
    if custom_lines: obj["customLines"] = custom_lines
    return obj

def parse_map(path: Path):
    r = Reader(path.read_bytes())
    version = r.i32()
    if version < 1 or version > 127:
        raise ValueError(f"Not a Mudlet map or unsupported header: {version}")
    env_colors = dict(r.qmap(r.i32, r.i32))
    area_names = dict(r.qmap(r.i32, r.qstr))
    custom_env_colors = dict(r.qmap(r.i32, r.qcolor))
    hash_to_room_id = dict(r.qmap(r.qstr, r.i32))
    user_data = dict(r.qmap(r.qstr, r.qstr)) if version >= 17 else {}
    font = r.qfont() if version >= 19 else {}
    fudge = r.f64() if version >= 19 else 1.0
    only_symbol_font = r.qbool() if version >= 19 else False

    areas = []
    if version >= 14:
        area_size = r.i32()
        for _ in range(area_size):
            area_id = r.i32()
            rooms = r.qset(r.i32)
            z_levels = r.qlist(r.i32)
            area_exits = r.qmap(r.i32, lambda: [r.i32(), r.i32()])
            grid_mode = r.qbool()
            max_x = r.i32(); max_y = r.i32(); max_z = r.i32()
            min_x = r.i32(); min_y = r.i32(); min_z = r.i32()
            span = r.vec3()
            xmax_for_z = dict(r.qmap(r.i32, r.i32)); ymax_for_z = dict(r.qmap(r.i32, r.i32))
            xmin_for_z = dict(r.qmap(r.i32, r.i32)); ymin_for_z = dict(r.qmap(r.i32, r.i32))
            pos = r.vec3()
            is_zone = r.qbool(); zone_area_ref = r.i32()
            area_user_data = dict(r.qmap(r.qstr, r.qstr)) if version >= 17 else {}
            areas.append({
                "id": area_id, "name": area_names.get(area_id, str(area_id)),
                "roomCount": len(rooms), "zLevels": z_levels,
                "gridMode": grid_mode, "isZone": is_zone, "zoneAreaRef": zone_area_ref,
                "bounds": {"minX": min_x, "maxX": max_x, "minY": min_y, "maxY": max_y, "minZ": min_z, "maxZ": max_z},
                "userData": area_user_data,
            })

    room_id_hash = dict(r.qmap(r.qstr, r.i32)) if version >= 18 else {}

    # v11-v20 old labels. This converter skips pixmap data because this map has no labels.
    if version >= 11 and version <= 20:
        areas_with_labels_total = r.i32()
        if areas_with_labels_total:
            raise NotImplementedError("This compact converter does not handle embedded Mudlet map labels/pixmaps yet.")

    rooms = []
    while r.rem() > 0:
        rooms.append(parse_room(r, version))

    return {
        "schema": "ardabest-map-v1",
        "source": path.name,
        "version": version,
        "areaNames": {str(k): v for k, v in area_names.items()},
        "roomIdHash": room_id_hash,
        "areas": areas,
        "rooms": rooms,
        "notes": "Generated from Mudlet binary map data. Coordinates use Mudlet x/y/z.",
    }

def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    src = Path(argv[1])
    dst = Path(argv[2])
    result = parse_map(src)
    dst.write_text(json.dumps(result, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")
    print(f"Wrote {len(result['rooms'])} rooms to {dst}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
