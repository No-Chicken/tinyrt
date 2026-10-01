"""Produce real signed application fixtures, with explicit PUBLIC TEST key."""
import hashlib
from pathlib import Path
import struct
import sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/"tests/fixtures"))
from reference_package import encode
sys.path.insert(0,str(ROOT/"tests/runtime"))
from make_fixtures import module
from make_round_fixtures import fixtures as round_fixtures
def u(n):
    out=bytearray()
    while n>=128: out.append((n&127)|128); n>>=7
    out.append(n); return bytes(out)
def si(n):
    out=bytearray()
    while True:
        part=n&127; n>>=7
        done=(n==0 and not part&64) or (n==-1 and part&64)
        out.append(part if done else part|128)
        if done: return bytes(out)
def text(s):
    b=s.encode(); return u(len(b))+b
def section(i,b): return bytes([i])+u(len(b))+b
def const(n): return b"\x41"+si(n)
def trap_after_set():
    # Two real ABI imports; the event mutates host KV then traps.
    types=u(4)+b"".join(b"\x60"+u(n)+b"\x7f"*n+b"\x01\x7f" for n in (1,2,4,0))
    imports=u(2)+text("tinyrt")+text("draw_clear")+b"\0\0"+text("tinyrt")+text("kv_set")+b"\0\1"
    exports=u(3)+b"".join(text(n)+b"\0"+u(i) for n,i in (("tinyrt_init",2),("tinyrt_event",3),("tinyrt_render",4)))
    code=[const(0),const(0)+const(999)+b"\x10\x01\x1a\x00"+const(0),
          const(0x123456)+b"\x10\x00\x1a"+const(0)]
    bodies=u(3)+b"".join(u(len(b)+2)+b"\0"+b+b"\x0b" for b in code)
    return b"\0asm\1\0\0\0"+section(1,types)+section(2,imports)+section(3,b"\3\1\2\3")+section(5,b"\1\1\1\2")+section(7,exports)+section(10,bodies)
def identity(path):
    b=path.read_bytes()
    # Separate producer metadata; consumer does not derive expected identity.
    sidecar=b[56:88]+b[32:36]+hashlib.sha256(b).digest()+struct.pack("<I",len(b))
    path.with_suffix(".identity").write_bytes(sidecar)
def main(folder):
    folder.mkdir(parents=True,exist_ok=True)
    error=folder/"trap-after-set.wasm"; error.write_bytes(trap_after_set())
    unknown=folder/"unknown.wasm"; unknown.write_bytes(module("unknown"))
    jobs=[("counter-v1",ROOT/"tests/fixtures/guests/counter-v1.wasm","demo.counter",1),
          ("counter-v2",ROOT/"tests/fixtures/guests/counter-v2.wasm","demo.counter",2),
          ("counter-error",error,"demo.counter",3),
          ("invalid-wasm",unknown,"demo.invalid",1)]
    rounds=round_fixtures()
    for version,name in enumerate(("stop_valid","stop_failure","stop_trap","stop_spin","stop_bad_signature"),1):
        wasm=folder/(name+".wasm");wasm.write_bytes(rounds[name])
        jobs.append((name,wasm,"demo.stop",version))
    for name,wasm,app_id,version in jobs:
        output=folder/(name+".trpkg")
        output.write_bytes(encode(wasm.read_bytes(),app_id=app_id,title=name,version=version))
        identity(output)
    bad=bytearray((folder/"counter-v2.trpkg").read_bytes()); bad[200]^=1
    output=folder/"invalid-signature.trpkg"; output.write_bytes(bad); identity(output)
    print(f"INTEGRATION fixtures={len(jobs)+1} real P256 packages; public test key_id=1")
if __name__=="__main__": main(Path(sys.argv[1]))
