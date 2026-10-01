# Core integration tests

Real SHA-256/P-256, package store, manager and pinned WAMR execute frozen core C/Wasm fixtures. Only physical NOR and persistent KV are modeled. Covers installation, update, reboot reconstruction, KV preservation, call rollback, malformed packages and uninstall/reinstall cleanup. Tests use the independent tests/fixtures/reference_package.py encoder and public test scalar 1; no SDK packer is imported or executed.

Run the independent root build described in [README](../../README.md).
Historical machine-specific logs and device reports are archived outside this repository;
reproduce current results from the source and explicit dependency paths above.
