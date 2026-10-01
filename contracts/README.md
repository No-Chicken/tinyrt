# TinyRT contracts

`guest-v1.h` is the authoritative C guest ABI. `abi-v1.json` describes native
imports, exports, permissions and events. `wire-v1.json` records package fields
and management framing/opcodes. ABI 1 semantics are unchanged by extraction.
The SDK consumes a generated copy and records source revision/content hashes.
`tests/contracts/test_contracts.py` detects drift against production C code.
New capabilities require explicit version/capability negotiation.
