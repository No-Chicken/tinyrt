# Frozen regression guests

These small C guests were preserved from the original TinyRT prototype on
2026-10-01 before SDK extraction. They exercise real ABI calls in runtime and
manager integration tests. Their source and checked-in Wasm are owned by this
test fixture; normal core builds never read the SDK or product repository.

The Wasm was built using Zig 0.13.0. Reproduce with
`python tests/fixtures/build_guests.py --cc /path/to/zig --out /temporary/output`,
then compare the files against `sha256.json`. Existing source permissions and
provenance are retained; this migration assigns no new project license.
