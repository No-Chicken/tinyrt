"""Independent v2 serializer for C verifier tests; no SDK helpers imported."""
import hashlib
import struct
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec, utils

ORDER = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551
WASM = b"\0asm\1\0\0\0\0\5\4test"
AOT = b"\0aot\5\0\0\0fixture-native-module"
KEY = ec.derive_private_key(42, ec.SECP256R1())

def resign(data, key=KEY):
    data = bytearray(data)
    data[152:184] = hashlib.sha256(data[256:]).digest()
    r, s = utils.decode_dss_signature(key.sign(b"TinyRT-package-v2\0"+data[:192], ec.ECDSA(hashes.SHA256())))
    data[192:256] = r.to_bytes(32, "big") + min(s, ORDER-s).to_bytes(32, "big")
    return bytes(data)

def envelope(header, sections):
    head = bytearray(header)
    head[:8] = b"TRPKG002"
    cursor = 256 + len(sections)*16
    table, payload = bytearray(), bytearray()
    for kind, content in sections:
        padding = (-cursor) % 4
        payload += bytes(padding); cursor += padding
        table += struct.pack("<4I", kind, 0, cursor, len(content))
        payload += content; cursor += len(content)
    struct.pack_into("<HH5I", head, 8, 2, 256, cursor, 256, len(sections), 16, 0)
    return resign(head + table + payload)

def fixtures(folder, header):
    meta = bytearray(256)
    struct.pack_into("<HHII", meta, 0, 1, 256, 5, 7)
    meta[16:22] = b"xtensa"; meta[32:39] = b"esp32s3"
    meta[48:68] = bytes([1])*20; meta[68:88] = bytes([2])*20
    for pos, value in [(88,3),(120,4),(152,5),(184,6)]: meta[pos:pos+32] = bytes([value])*32
    meta[216:248] = hashlib.sha256(WASM).digest()
    native = bytes(meta) + AOT
    good = envelope(header, [(1,WASM),(2,native),(3,b"resources")])
    names = {
        "v2_wasm_assets": envelope(header, [(1,WASM),(3,b"resources")]),
        "v2_aot": good,
        "v2_aot_only": envelope(header, [(2,native)]),
        "v2_maximum": envelope(header, [(1,WASM),(3,bytes(0x200000-304))]),
    }
    previous = bytearray(good); struct.pack_into("<I", previous, 52, 8)
    names["v2_previous_key"] = resign(previous, ec.derive_private_key(43,ec.SECP256R1()))
    outside = bytearray(good); outside[56:88] = b"other.app".ljust(32,b"\0")
    names["v2_outside_demo"] = resign(outside)
    rejects = {}
    def changed(name, offset, value):
        data = bytearray(good); data[offset:offset+len(value)] = value
        rejects[name] = resign(data)
    for name, offset, value in [
        ("table_offset",16,260),("table_count_zero",20,0),("table_count_many",20,4),
        ("entry_size",24,32),("header_flags",28,1),("unknown_section",256,4),
        ("duplicate_section",272,1),("section_flags",260,1),("unaligned",280,319),
        ("overlap",280,304),("gap",280,324),("overflow_offset",280,0xfffffffc),
        ("overflow_size",284,0xffffffff),("empty_section",284,0),
    ]: changed(name,offset,struct.pack("<I",value))
    # Three-entry table ends at 304, then Wasm15 + one zero pad, then native metadata.
    for name, offset, value in [
        ("padding",319,b"x"),("meta_version",320,b"\2\0"),("meta_size",322,b"\xff\0"),
        ("meta_reserved",332,b"x"),("arch_empty",336,bytes(16)),
        ("arch_bad_padding",351,b"x"),("cpu_bad_text",352,b"ESP32S3"),
        ("meta_tail",568,b"x"),("source_mismatch",536,bytes([9])*32),
    ]: changed(name,offset,value)
    for flags in (0,1,3,5,6,15): changed("safety_"+str(flags),328,struct.pack("<I",flags))
    for pos in (368,388,408,440,472,504,536):
        changed("zero_identity_"+str(pos),pos,bytes(20 if pos in (368,388) else 32))
    rejects["resources_only"] = envelope(header, [(3,b"resources")])
    bad = bytearray(good); bad[320+256] ^= 1; rejects["native_tamper"] = bytes(bad)
    bad = bytearray(good); bad[-1] ^= 1; rejects["resource_tamper"] = bytes(bad)
    bad = bytearray(good); bad[304] ^= 1; rejects["wasm_tamper"] = bytes(bad)
    bad = bytearray(good); bad[440] ^= 1; rejects["metadata_tamper"] = bytes(bad)
    rejects["truncated"] = good[:-1]
    rejects["trailing"] = good+b"\0"
    for name, content in names.items(): (folder/(name+".pkg")).write_bytes(content)
    for name, content in rejects.items(): (folder/("v2_reject_"+name+".pkg")).write_bytes(content)
    (folder/"v2_reject.txt").write_text("\n".join("v2_reject_"+name for name in rejects),encoding="ascii")
