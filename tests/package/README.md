# Package verification tests

Independent Python signed fixtures exercise valid envelopes and malformed metadata/signatures against the real Windows BCrypt verifier. The runtime validation callback is an explicit stub in this suite; integration tests cover real Wasm loading. SDK CLI interoperability belongs to SDK tests. The ESP-IDF PSA compile smoke test is now in the product repository at esp32/tests/tinyrt/package_esp32.

Format-1 section fixtures are serialized independently in `make_section_fixtures.py`. Tests cover canonical section tables, the complete 2 MiB limit, signed metadata, source-Wasm binding, explicit AOT authority, development restrictions, current/previous release keys, target/compatibility fallback, every read failure and validator status propagation. Both Wasm and AOT callbacks here are explicit stubs; passing this suite does not prove native-code validity or safety. See [package specification](../../specs/package.md).

Run the independent root build described in [README](../../README.md).
Historical machine-specific logs and device reports are archived outside this repository;
reproduce current results from the source and explicit dependency paths above.
