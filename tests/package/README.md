# Package verification tests

Independent Python signed fixtures exercise valid envelopes and malformed metadata/signatures against the real Windows BCrypt verifier. The runtime validation callback is an explicit stub in this suite; integration tests cover real Wasm loading. SDK CLI interoperability belongs to SDK tests. The ESP-IDF PSA compile smoke test is now in the product repository at esp32/tests/tinyrt/package_esp32.

Run the independent root build described in [README](../../README.md).
Historical machine-specific logs and device reports are archived outside this repository;
reproduce current results from the source and explicit dependency paths above.
