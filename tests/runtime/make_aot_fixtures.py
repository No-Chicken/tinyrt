"""Compile actual host AOT fixtures with the pinned loop-poll compiler."""
from pathlib import Path
import subprocess
import sys
from make_fixtures import module, u, sec, c, call
from make_round_fixtures import guest
from make_pixel_fixtures import fixtures

out, compiler = Path(sys.argv[1]), Path(sys.argv[2])
out.mkdir(parents=True, exist_ok=True)
cases = fixtures()
names = ["frame_observed", "frame_partial_failure", "pixel_valid", "pixel_then_skip",
         "pixel_maximum", "guard_pure", "guard_import", "guard_init", "guard_event", "guard_render", "guard_stop"]
for stage in ("init", "event", "render", "stop"):
    for kind, body in (("pure", b"\x03\x40\x0c\0\x0b"+c(0)), ("trap", b"\0"+c(0))):
        bodies={"init":c(0),"event":c(0),"render":c(0)+call(0)+b"\x1a"+c(0),"stop":c(0)}
        bodies[stage]=body
        name=kind+"_"+stage
        cases[name]=guest([("draw_clear",1)],**bodies);names.append(name)
for name in ("nomax", "huge", "ctor", "post", "start", "unknown", "oob"):
    cases[name] = module(name)
    names.append(name)
def take_u(data, pos):
    value, shift = 0, 0
    while True:
        byte = data[pos]; pos += 1
        value |= (byte & 127) << shift
        if not byte & 128: return value, pos
        shift += 7
data, pos, parts = module(), 8, []
while pos < len(data):
    kind = data[pos]; size, start = take_u(data, pos + 1)
    payload = data[start:start + size]; pos = start + size
    if kind in (1, 3, 10):
        count, skip = take_u(payload, 0)
        payload = u(count + 1) + payload[skip:] + {1: b"\x60\0\0", 3: u(9), 10: b"\x02\0\x0b"}[kind]
    if kind == 10: parts.append(sec(8, u(9)))
    parts.append(sec(kind, payload))
cases["start"] = data[:8] + b"".join(parts)
cases["huge"] = guest([],c(0),memory_pages=17).replace(sec(5,b"\1\1\x11\2"),sec(5,b"\1\1\x11\x11"))
flags = ["--target=x86_64", "--bounds-checks=1", "--stack-bounds-checks=1",
         "--enable-loop-poll", "--disable-simd", "--disable-ref-types",
         "--opt-level=3", "--size-level=1"]
for name in names:
    wasm = out / (name + ".wasm")
    wasm.write_bytes(cases[name])
    subprocess.run([str(compiler), *flags, "-o", str(out / (name + ".aot")), str(wasm)],
                   check=True, stdout=subprocess.DEVNULL)
