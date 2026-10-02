"""Signed integration fixtures from the actual host compiler (public test key)."""
from pathlib import Path
import hashlib
import struct
import subprocess
import sys
from cryptography.hazmat.primitives.asymmetric import ec
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/"tests/package"))
sys.path.insert(0,str(ROOT/"tests/fixtures"))
from make_section_fixtures import envelope, resign
from reference_package import encode
from make_packages import identity, trap_after_set
out,compiler=Path(sys.argv[1]),Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=True)
flags=["--target=x86_64","--bounds-checks=1","--stack-bounds-checks=1","--enable-loop-poll",
       "--disable-simd","--disable-ref-types","--opt-level=3","--size-level=1"]
for name,wasm,version in (("counter-aot",(ROOT/"tests/fixtures/guests/counter-v1.wasm").read_bytes(),1),
                          ("counter-aot-error",trap_after_set(),2)):
    src=out/(name+".wasm");native=out/(name+".aot");src.write_bytes(wasm)
    subprocess.run([str(compiler),*flags,"-o",str(native),str(src)],check=True,stdout=subprocess.DEVNULL)
    meta=bytearray(256);struct.pack_into("<HHII",meta,0,1,256,5,7)
    meta[16:22]=b"x86_64";meta[32:39]=b"generic"
    meta[48:68]=bytes([1])*20;meta[68:88]=bytes([2])*20
    for pos,value in ((88,3),(120,1),(152,5),(184,6)):meta[pos:pos+32]=bytes([value])+bytes(31)
    meta[216:248]=hashlib.sha256(wasm).digest()
    header=encode(wasm,app_id="demo.counter",title=name,version=version)[:256]
    content=envelope(header,[(1,wasm),(2,bytes(meta)+native.read_bytes())])
    content=resign(content,ec.derive_private_key(1,ec.SECP256R1()))
    path=out/(name+".trpkg");path.write_bytes(content);identity(path)
    if name=="counter-aot":
        bad=bytearray(native.read_bytes());bad[0]^=1
        invalid_header=bytearray(header);struct.pack_into("<I",invalid_header,32,2)
        content=envelope(invalid_header,[(1,wasm),(2,bytes(meta)+bad)])
        path=out/"counter-aot-invalid.trpkg"
        path.write_bytes(resign(content,ec.derive_private_key(1,ec.SECP256R1())));identity(path)
