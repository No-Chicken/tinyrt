"""Check that checked-in regression guests retain their recorded identity."""
import hashlib
import json
from pathlib import Path
folder = Path(__file__).resolve().parent / "guests"
for name, expected in json.loads((folder/"sha256.json").read_text(encoding="utf-8")).items():
    assert hashlib.sha256((folder/name).read_bytes()).hexdigest() == expected, name
print("Frozen C/Wasm fixture hashes match")
