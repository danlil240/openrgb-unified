#!/usr/bin/env python3
"""ARGB zone measurement helper for the openrgb-unified project.

Raw OpenRGB SDK client (protocol v3) for measuring real LED counts on the
IT5711 ARGB headers. Requires OpenRGB running with its SDK server on
127.0.0.1:6742 (launch OpenRGB.exe with --server).

Usage:
  python argb_measure.py list
  python argb_measure.py direct <ctrl>
  python argb_measure.py resize <ctrl> <zone> <size>
  python argb_measure.py pattern <ctrl> <zone> [size]   # 4-color cycle
  python argb_measure.py blocks <ctrl> <zone> <n1,n2,..> # solid color per block
  python argb_measure.py solid <ctrl> <zone> <rrggbb>
  python argb_measure.py off <ctrl> <zone>
"""
import socket
import struct
import sys
import time

HOST, PORT = "127.0.0.1", 6742
MAGIC = b"ORGB"
PKT_COUNT, PKT_DATA, PKT_VERSION = 0, 1, 40
PKT_RESIZE, PKT_SETZONELEDS, PKT_CUSTOMMODE = 1000, 1051, 1100


def rgb(r, g, b):
    return r | (g << 8) | (b << 16)


BLOCK_COLORS = [
    rgb(255, 0, 0), rgb(0, 255, 0), rgb(0, 0, 255), rgb(255, 255, 0),
    rgb(255, 0, 255), rgb(0, 255, 255), rgb(255, 255, 255), rgb(255, 128, 0),
]


class Reader:
    def __init__(self, data):
        self.d, self.o = data, 0

    def u16(self):
        v = struct.unpack_from("<H", self.d, self.o)[0]
        self.o += 2
        return v

    def u32(self):
        v = struct.unpack_from("<I", self.d, self.o)[0]
        self.o += 4
        return v

    def string(self):
        n = self.u16()
        s = self.d[self.o:self.o + n].split(b"\0")[0].decode(errors="replace")
        self.o += n
        return s


class SDK:
    def __init__(self):
        self.sock = socket.create_connection((HOST, PORT), timeout=10)
        self.sock.settimeout(10)
        self.send(0, PKT_VERSION, struct.pack("<I", 3))

    def send(self, dev, pkt, payload=b""):
        self.sock.sendall(MAGIC + struct.pack("<III", dev, pkt, len(payload)) + payload)

    def recv_exact(self, n):
        buf = b""
        while len(buf) < n:
            chunk = self.sock.recv(n - len(buf))
            if not chunk:
                raise ConnectionError("server closed connection")
            buf += chunk
        return buf

    def expect(self, pkt_id, timeout=8):
        self.sock.settimeout(timeout)
        deadline = time.time() + timeout
        while time.time() < deadline:
            hdr = self.recv_exact(16)
            dev, pkt, size = struct.unpack("<III", hdr[4:])
            data = self.recv_exact(size) if size else b""
            if pkt == pkt_id:
                return dev, data
        raise TimeoutError(f"no reply for packet {pkt_id}")

    def controller_count(self):
        self.send(0, PKT_COUNT)
        _, data = self.expect(PKT_COUNT)
        return struct.unpack("<I", data[:4])[0]

    def controller(self, idx):
        self.send(idx, PKT_DATA, struct.pack("<I", 3))
        _, data = self.expect(PKT_DATA)
        r = Reader(data)
        r.u32()  # data size
        info = {"idx": idx, "type": r.u32()}
        for f in ("name", "vendor", "description", "version", "serial", "location"):
            info[f] = r.string()
        num_modes, info["active_mode"] = r.u16(), r.u32()
        info["modes"] = []
        for _ in range(num_modes):
            r.string()
            for _ in range(12):
                r.u32()
            for _ in range(r.u16()):
                r.u32()
            info["modes"].append(None)
        num_zones = r.u16()
        info["zones"] = []
        for _ in range(num_zones):
            zname = r.string()
            ztype, zmin, zmax, zcount = r.u32(), r.u32(), r.u32(), r.u32()
            if r.u16():
                h, w = r.u32(), r.u32()
                r.o += h * w * 4
            info["zones"].append(
                {"idx": len(info["zones"]), "name": zname, "type": ztype,
                 "min": zmin, "max": zmax, "count": zcount})
        return info

    def set_custom_mode(self, idx):
        self.send(idx, PKT_CUSTOMMODE)

    def resize_zone(self, idx, zone, size):
        self.send(idx, PKT_RESIZE, struct.pack("<ii", zone, size))

    def set_zone_colors(self, idx, zone, colors):
        body = struct.pack("<IH", zone, len(colors)) + b"".join(
            struct.pack("<I", c) for c in colors)
        total = 4 + len(body)
        self.send(idx, PKT_SETZONELEDS, struct.pack("<I", total) + body)


def cycle(n):
    return [BLOCK_COLORS[i % 4] for i in range(n)]


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    cmd = sys.argv[1]
    sdk = SDK()
    if cmd == "list":
        n = sdk.controller_count()
        for i in range(n):
            c = sdk.controller(i)
            print(f"[{i}] {c['name']} (type {c['type']})")
            for z in c["zones"]:
                print(f"    zone {z['idx']}: {z['name']}  "
                      f"count={z['count']} min={z['min']} max={z['max']} type={z['type']}")
    elif cmd == "direct":
        sdk.set_custom_mode(int(sys.argv[2]))
    elif cmd == "resize":
        sdk.resize_zone(int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]))
    elif cmd == "pattern":
        ctrl, zone = int(sys.argv[2]), int(sys.argv[3])
        size = int(sys.argv[4]) if len(sys.argv) > 4 else 0
        if size:
            sdk.resize_zone(ctrl, zone, size)
            time.sleep(0.5)
        sdk.set_zone_colors(ctrl, zone, cycle(size or 128))
    elif cmd == "blocks":
        ctrl, zone = int(sys.argv[2]), int(sys.argv[3])
        sizes = [int(x) for x in sys.argv[4].split(",")]
        colors = []
        for i, s in enumerate(sizes):
            colors += [BLOCK_COLORS[i % len(BLOCK_COLORS)]] * s
        sdk.set_zone_colors(ctrl, zone, colors)
    elif cmd == "solid":
        ctrl, zone = int(sys.argv[2]), int(sys.argv[3])
        hexc = sys.argv[4].lstrip("#")
        r, g, b = (int(hexc[i:i + 2], 16) for i in (0, 2, 4))
        sdk.set_zone_colors(ctrl, zone, [rgb(r, g, b)] * 128)
    elif cmd == "off":
        sdk.set_zone_colors(int(sys.argv[2]), int(sys.argv[3]), [0] * 128)
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
