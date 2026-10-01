# Package store tests

The synchronous simulated NOR supports partial-write failure injection. Tests exercise catalog recovery, update visibility, space/version bounds, ownership and corruption handling. The package verifier here is explicitly test-only. Full signature/Wasm checking belongs to package and integration suites. Product hardware evidence lives in EEBadge and is not a dependency of this repository.

Run the independent root build described in [README](../../README.md).
The independent media producer writes TRDIR002 bytes directly. Coverage includes
16 plus one apps, 2 MiB packages, rounded extent overlap/bounds, first fit,
fragmented/full copy-on-write updates, quarantined extent reservation, legacy
format refusal, inventory revision changes and overflow, and the existing
partial program/erase/sync commit fault matrix. Large package staging also
injects a partial failure at its last erase sector.
Historical machine-specific logs and device reports are archived outside this repository;
reproduce current results from the source and explicit dependency paths above.
