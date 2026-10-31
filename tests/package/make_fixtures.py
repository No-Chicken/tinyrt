"""Independent format-1 fixtures; the verifier ignores key ID and signature bytes."""
import hashlib
import struct
import sys
from pathlib import Path
WASM = b"\0asm\x01\0\0\0\x00\x05\x04test"
ASSETS = bytes(range(256)) * 9
def fixtures(folder):
    folder.mkdir(parents=True, exist_ok=True)
    h = bytearray(256)
    h[:8] = b"TRPKG001"
    struct.pack_into("<HH5I", h, 8, 1, 256, 304+len(ASSETS), 256, 2, 16, 0)
    struct.pack_into("<6I",h,32,3,1,15,8,12345,7)
    payload = (struct.pack('<8I',1,0,288,len(WASM),3,0,304,len(ASSETS))
               + WASM + b'\0' + ASSETS)
    h[56:64] = b"demo.app"
    title = "\u8ba1\u6570\u5668".encode()
    h[88:88+len(title)] = title
    h[152:184] = hashlib.sha256(payload).digest()
    good = bytes(h) + payload
    (folder/"valid.pkg").write_bytes(good)
    (folder/"expected.sha256").write_bytes(hashlib.sha256(good).digest())
    # Any key ID, signature bytes or legal namespace must pass.
    anything = bytearray(h); struct.pack_into("<I", anything, 52, 0xdeadbeef)
    anything[192:256] = b"\xff" * 64; anything[56:88] = b"my-game".ljust(32, b"\0")
    (folder/"any_signature.pkg").write_bytes(bytes(anything) + payload)
    cases = {}
    def changed(name, offset, value):
        data = bytearray(h); data[offset:offset+len(value)] = value
        cases[name] = bytes(data) + payload
    def u32(name, offset, value):
        changed(name, offset, struct.pack("<I", value))
    changed("magic", 0, b"BAD")
    changed("format", 8, b"\x02\0")
    changed("header_size", 10, b"\xff\0")
    for name, offset, value in [
        ("total_size",12,len(good)+1), ("wasm_offset",16,255), ("wasm_tiny",20,8),
        ("wasm_overflow",20,0xffffffff), ("assets_offset",24,0xffffffff),
        ("assets_overflow",28,0xffffffff), ("version_zero",32,0), ("abi",36,2),
        ("permissions",40,64), ("memory_zero",44,0), ("memory_high",44,17),
        ("budget_zero",48,0), ("budget_high",48,100001)]:
        u32(name,offset,value)
    for name,offset,value in [
        ("id_empty",56,bytes(32)), ("id_no_nul",56,b"a"*32), ("id_upper",56,b"D"),
        ("id_padding",87,b"x"), ("title_empty",88,bytes(64)), ("title_no_nul",88,b"a"*64),
        ("title_overlong",88,b"\xc0\xaf\0"+bytes(61)),
        ("title_surrogate",88,b"\xed\xa0\x80\0"+bytes(60)),
        ("title_too_high",88,b"\xf4\x90\x80\x80\0"+bytes(59)),
        ("title_truncated",88,b"\xe4\0"+bytes(62)),
        ("title_continuation",88,b"\x80\0"+bytes(62)),
        ("title_padding",151,b"x"), ("reserved",184,b"x"), ("payload_hash",152,bytes(32))]:
        changed(name,offset,value)
    bad=bytearray(good); bad[300]^=1; cases["payload_tamper"]=bytes(bad)
    cases["truncated"]=good[:-1]; cases["appended"]=good+b"x"; cases["short_header"]=good[:255]
    from make_section_fixtures import envelope
    for name, assets in [("no_assets", b""), ("maximum", bytes(0x200000-304))]:
        result=envelope(h,[(1,WASM)]+([(3,assets)] if assets else []))
        (folder/(name+".pkg")).write_bytes(result)
        (folder/(name+".sha256")).write_bytes(hashlib.sha256(result).digest())
    # A second positive fixture uses a separately assembled title.
    alternate=bytearray(h); alternate[88:152]=bytes(64); alternate[88:95]=b"Counter"
    reference=bytes(alternate)+payload
    (folder/"reference.pkg").write_bytes(reference)
    (folder/"reference.sha256").write_bytes(hashlib.sha256(reference).digest())
    for name,data in cases.items(): (folder/(name+".pkg")).write_bytes(data)
    (folder/"reject.txt").write_text("\n".join(cases)+"\n",encoding="ascii")
    print(f"Generated valid fixture + {len(cases)} rejection fixtures")
    # Section fixture is assembled independently of the SDK packer.
    section = bytearray(h)
    section[:8] = b"TRPKG001"
    struct.pack_into("<HH5I", section, 8, 1, 256, 272+len(WASM), 256, 1, 16, 0)
    payload = struct.pack("<4I", 1, 0, 272, len(WASM)) + WASM
    section[152:184] = hashlib.sha256(payload).digest()
    (folder/"section_wasm.pkg").write_bytes(bytes(section)+payload)
    import make_section_fixtures
    make_section_fixtures.fixtures(folder,h)
if __name__=="__main__":
    fixtures(Path(sys.argv[1]))
