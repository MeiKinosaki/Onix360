# WX360 XEX import reverse-engineering notes

## References inspected

- Xenia, `src/xenia/cpu/xex_module.cc`, function `XexModule::SetupLibraryImports`:
  https://github.com/xenia-project/xenia/blob/95a5c3ee250f80c3b9d139658649d9ffb6db3eec/src/xenia/cpu/xex_module.cc
- ReXGlue SDK, `src/kernel/xex_module.cpp`, function `XEXModule::SetupLibraryImports`:
  https://github.com/rexglue/rexglue-sdk/blob/ebac1cf0d04bab0042a5dd6148b34ab3953e9c0d/src/kernel/xex_module.cpp
- 360tools independent parser:
  https://github.com/sp00nznet/360tools/blob/1b53928767fa134c1ea2ea42007bc39852eea1fc/tools/parse_xex_imports.py

These are implementation references, not official Microsoft specifications. Their behavior is cross-checked between two Xenia-derived loaders and a separate parser.

## Observed import-record behavior

The import library contains an array of guest addresses. Each address points to a 32-bit big-endian record in the mapped image. The high byte selects the record kind and the low 16 bits carry the ordinal.

- Type 0: value slot. The loader records its address as `value_address`.
- Type 1: function thunk. It follows the matching type-0 record for the same ordinal and supplies `thunk_address`.
- A function import's value slot is patched with the guest thunk address. The guest thunk code is separately rewritten to dispatch to the kernel implementation.
- A variable import has a type-0 value slot and is patched with the guest address of the variable, when implemented.
- Type-1 records without a preceding matching type-0 record are malformed and must be rejected.

The record address itself is not interchangeable with the value stored in that record. In particular, writing a kernel export address into a type-1 thunk record would corrupt the executable thunk.

## WX360 implementation boundary

WX360 now keeps `value_address` and `thunk_address` separate, pairs records by library and ordinal, and patches function value slots with thunk addresses while variable slots receive registered guest variable addresses. Function thunk storage is checked before patching.

Still required before claiming real XEX import compatibility:

1. Validate the import directory layout against retail/devkit XEX samples with known hashes.
2. Implement guest thunk rewriting (PPC `sc`/return sequence) with correct ABI and kernel dispatch.
3. Populate version-aware, evidence-backed exports for `xboxkrnl.exe` and `xam.xex`.
4. Validate missing exports and variable storage behavior against independent test vectors.
5. Run a complete mapped XEX test with real relocation/import metadata.

No commercial XEX was used in these tests. Synthetic vectors validate the parser and patching mechanics only.
