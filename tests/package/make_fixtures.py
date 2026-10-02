"""Independent signed fixtures. Public test scalar 42; never a production key."""
import hashlib
import struct
import sys
from pathlib import Path
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils
ORDER = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551
KEY = ec.derive_private_key(42, ec.SECP256R1())
DOMAIN = b"TinyRT-package-v1\0"
WASM = b"\0asm\x01\0\0\0\x00\x05\x04test"
ASSETS = bytes(range(256)) * 9
def signed(header, payload, key=KEY):
    r, s = utils.decode_dss_signature(key.sign(DOMAIN + bytes(header[:192]), ec.ECDSA(hashes.SHA256())))
    header[192:256] = r.to_bytes(32, "big") + min(s, ORDER-s).to_bytes(32, "big")
    return bytes(header) + payload
def fixtures(folder):
    folder.mkdir(parents=True, exist_ok=True)
    (folder/"public.bin").write_bytes(KEY.public_key().public_bytes(serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint))
    h = bytearray(256)
    h[:8] = b"TRPKG001"
    struct.pack_into("<HH11I", h, 8, 1, 256, 256+len(WASM)+len(ASSETS), 256, len(WASM),
                     256+len(WASM), len(ASSETS), 3, 1, 15, 8, 12345, 7)
    h[56:64] = b"demo.app"
    title = "\u8ba1\u6570\u5668".encode()
    h[88:88+len(title)] = title
    h[152:184] = hashlib.sha256(WASM+ASSETS).digest()
    good = signed(bytearray(h), WASM+ASSETS)
    (folder/"valid.pkg").write_bytes(good)
    (folder/"expected.sha256").write_bytes(hashlib.sha256(good).digest())
    publisher2 = ec.derive_private_key(43, ec.SECP256R1())
    (folder/"publisher2.bin").write_bytes(publisher2.public_key().public_bytes(serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint))
    for name, app_id, key_id, signer in [
        ("publisher2_valid", "other.app", 8, publisher2),
        ("publisher2_cross", "demo.app", 8, publisher2),
        ("namespace_boundary", "demoevil.app", 7, KEY),
        ("namespace_empty", "demo.", 7, KEY)]:
        scoped = bytearray(h)
        scoped[56:88] = app_id.encode().ljust(32, b"\0")
        struct.pack_into("<I", scoped, 52, key_id)
        (folder/(name+".pkg")).write_bytes(signed(scoped, WASM+ASSETS, signer))
    cases = {}
    def changed(name, offset, value):
        data = bytearray(h); data[offset:offset+len(value)] = value
        cases[name] = signed(data, WASM+ASSETS)
    def u32(name, offset, value):
        changed(name, offset, struct.pack("<I", value))
    changed("magic", 0, b"BAD")
    changed("format", 8, b"\x02\0")
    changed("header_size", 10, b"\xff\0")
    for name, offset, value in [
        ("total_size",12,len(good)+1), ("wasm_offset",16,255), ("wasm_tiny",20,8),
        ("wasm_overflow",20,0xffffffff), ("assets_offset",24,0xffffffff),
        ("assets_overflow",28,0xffffffff), ("version_zero",32,0), ("abi",36,2),
        ("permissions",40,16), ("memory_zero",44,0), ("memory_high",44,17),
        ("budget_zero",48,0), ("budget_high",48,100001), ("key_unknown",52,8)]:
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
    for name,offset in [("payload_tamper",300),("metadata_tamper",48),("signature_tamper",200)]:
        bad=bytearray(good); bad[offset]^=1; cases[name]=bad
    for name,offset,value in [("r_zero",192,0),("s_zero",224,0),("r_order",192,ORDER),
                             ("high_s",224,ORDER-int.from_bytes(good[224:256],"big"))]:
        bad=bytearray(good); bad[offset:offset+32]=value.to_bytes(32,"big"); cases[name]=bad
    cases["truncated"]=good[:-1]; cases["appended"]=good+b"x"; cases["short_header"]=good[:255]
    for name, assets in [("no_assets", b""), ("maximum", bytes(0x200000-256-len(WASM)))]:
        head=bytearray(h)
        struct.pack_into("<I",head,12,256+len(WASM)+len(assets))
        struct.pack_into("<I",head,28,len(assets))
        head[152:184]=hashlib.sha256(WASM+assets).digest()
        result=signed(head,WASM+assets)
        (folder/(name+".pkg")).write_bytes(result)
        (folder/(name+".sha256")).write_bytes(hashlib.sha256(result).digest())
    # A second positive fixture uses a separately assembled title with the test key.
    alternate=bytearray(h); alternate[88:152]=bytes(64); alternate[88:95]=b"Counter"
    reference=signed(alternate,WASM+ASSETS)
    (folder/"reference.pkg").write_bytes(reference)
    (folder/"reference.sha256").write_bytes(hashlib.sha256(reference).digest())
    for name,data in cases.items(): (folder/(name+".pkg")).write_bytes(data)
    (folder/"reject.txt").write_text("\n".join(cases)+"\n",encoding="ascii")
    print(f"Generated valid fixture + {len(cases)} rejection fixtures")
    # v2 fixture is assembled independently of the SDK packer.
    v2 = bytearray(h)
    v2[:8] = b"TRPKG002"
    struct.pack_into("<HH5I", v2, 8, 2, 256, 272+len(WASM), 256, 1, 16, 0)
    payload = struct.pack("<4I", 1, 0, 272, len(WASM)) + WASM
    v2[152:184] = hashlib.sha256(payload).digest()
    r, s = utils.decode_dss_signature(KEY.sign(b"TinyRT-package-v2\0" + bytes(v2[:192]), ec.ECDSA(hashes.SHA256())))
    v2[192:256] = r.to_bytes(32, "big") + min(s, ORDER-s).to_bytes(32, "big")
    (folder/"v2_wasm.pkg").write_bytes(bytes(v2)+payload)
    import make_v2_fixtures
    make_v2_fixtures.fixtures(folder,h)
if __name__=="__main__":
    fixtures(Path(sys.argv[1]))
