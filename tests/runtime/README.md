# Runtime tests

Real pinned WAMR executes deterministic generated invalid/valid modules and frozen regression guests. Covers imports/exports, permission and memory limits, bounded instruction execution, guest pointer checks, state poisoning and accounting. Generated modules live in the build directory. Frozen guest source and reconstruction instructions are in tests/fixtures/guests.

Run the independent root build described in [README](../../README.md).
Historical machine-specific logs and device reports are archived outside this repository;
reproduce current results from the source and explicit dependency paths above.
