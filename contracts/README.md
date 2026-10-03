# TinyRT contracts

`guest-v1.h` is the authoritative C guest ABI. `abi-v1.json` describes native
imports, exports, permissions and events. `wire-v1.json` records package fields
and management framing/opcodes. ABI 1 semantics are unchanged by extraction.
The SDK consumes a generated copy and records source revision/content hashes.
`tests/contracts/test_contracts.py` detects drift against production C code.
New capabilities require explicit version/capability negotiation.

ABI 1 now accepts opt-in motion events (kind 7, mask 0x80) in addition to
the existing masks. Arguments are signed raw-sensor ax/ay/az in mg, bounded
to +/-16000. Existing applications keep their input semantics. An older
runtime rejects the new mask at init; motion applications require a host
and desktop runner supporting this extension. No package or BLE layout changes.
