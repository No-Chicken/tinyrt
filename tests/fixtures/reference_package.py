"""Test-only package-v1 encoder. Public scalar 1 is never a production key."""
import hashlib
import struct
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec, utils

ORDER = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551

def encode(wasm, *, app_id, title, version):
    header = bytearray(256)
    header[:8] = b"TRPKG001"
    struct.pack_into("<HH11I", header, 8, 1, 256, 256+len(wasm), 256,
                     len(wasm), 256+len(wasm), 0, version, 1, 15, 2, 100000, 1)
    name, label = app_id.encode("ascii"), title.encode("utf-8")
    assert 0 < len(name) < 32 and 0 < len(label) < 64
    header[56:56+len(name)] = name
    header[88:88+len(label)] = label
    header[152:184] = hashlib.sha256(wasm).digest()
    key = ec.derive_private_key(1, ec.SECP256R1())
    r, s = utils.decode_dss_signature(key.sign(b"TinyRT-package-v1\0" + header[:192], ec.ECDSA(hashes.SHA256())))
    header[192:256] = r.to_bytes(32,"big") + min(s, ORDER-s).to_bytes(32,"big")
    return bytes(header) + wasm
