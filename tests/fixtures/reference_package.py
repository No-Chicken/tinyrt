"""Test-only package-v1 encoder; the signature area stays zero."""
import hashlib
import struct

def encode(wasm, *, app_id, title, version, key_id=1, assets=b""):
    sections=[(1,wasm)]+([(3,assets)] if assets else [])
    cursor=256+16*len(sections)
    table=bytearray();content=bytearray()
    for kind,data in sections:
        pad=(-cursor)%4;content+=bytes(pad);cursor+=pad
        table+=struct.pack("<IIII",kind,0,cursor,len(data))
        content+=data;cursor+=len(data)
    payload=table+content
    header = bytearray(256)
    header[:8] = b"TRPKG001"
    struct.pack_into("<HH11I", header, 8, 1, 256, cursor, 256,
                     len(sections), 16, 0, version, 1, 15, 2, 100000, key_id)
    name, label = app_id.encode("ascii"), title.encode("utf-8")
    assert 0 < len(name) < 32 and 0 < len(label) < 64
    header[56:56+len(name)] = name
    header[88:88+len(label)] = label
    header[152:184] = hashlib.sha256(payload).digest()
    return bytes(header) + payload
