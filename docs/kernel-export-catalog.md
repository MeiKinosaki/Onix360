# Kernel export catalog (reference metadata)

This file records ordinal/name/kind metadata only. It is **not** an implementation
of the corresponding kernel routines and does not assign guest addresses.

## xboxkrnl.exe

The entries below are transcribed from the Xenia research project's
`src/xenia/kernel/xboxkrnl/xboxkrnl_table.inc` (BSD-3-Clause project).
They are useful for cross-checking import records, but are not proof that every
title or every kernel revision uses an identical export table.

| Ordinal (hex) | Export | Kind |
|---:|---|---|
| 0001 | DbgBreakPoint | function |
| 0002 | DbgBreakPointWithStatus | function |
| 0003 | DbgPrint | function |
| 0009 | ExAllocatePool | function |
| 000A | ExAllocatePoolWithTag | function |
| 000C | ExConsoleGameRegion | variable |
| 000D | ExCreateThread | function |
| 000E | ExEventObjectType | variable |
| 000F | ExFreePool | function |
| 0010 | ExGetXConfigSetting | function |
| 0012 | ExMutantObjectType | variable |
| 0017 | ExSemaphoreObjectType | variable |
| 001B | ExThreadObjectType | variable |
| 001C | ExTimerObjectType | variable |
| 0036 | IoCompletionObjectType | variable |
| 003A | IoDeviceObjectType | variable |
| 003E | IoFileObjectType | variable |
| 0059 | KeDebugMonitorData | variable |
| 00AD | KeTimeStampBundle | variable |

## Resolution contract

* Function exports must resolve to guest-callable trampoline addresses. A host
  function pointer is never a valid guest address.
* Variable exports must resolve to allocated guest storage containing the
  expected guest data structure. Treating a variable as a callable thunk is
  incorrect.
* Unknown ordinals, unsupported libraries, duplicate metadata and unresolved
  function implementations must fail closed. Do not synthesize addresses.
* A metadata entry does not imply that its implementation exists.

The current WX360 resolver is still a minimal address patcher. It does not yet
provide a complete function-trampoline registry or variable-storage allocator.
Consequently this catalog is preparatory and must not be described as complete
kernel import support.

## RSA trust boundary

XEX security metadata contains a 256-byte signature, but signature verification
is meaningful only with the correct, independently sourced public key and the
exact signed byte range / padding rules for the target XEX security format.
No guessed modulus, development key, or key copied from an unrelated title is
accepted as a trust anchor. Until an authoritative public key and independent
known-answer vectors are checked in, retail signature verification remains
unsupported and the validated-loader path must reject signed images that
require that verification.

## References

* Xenia export table:
  https://github.com/xenia-project/xenia/blob/master/src/xenia/kernel/xboxkrnl/xboxkrnl_table.inc
* Xenia kernel export registration and variable mappings:
  https://github.com/xenia-project/xenia/blob/master/src/xenia/kernel/xboxkrnl/xboxkrnl_module.cc
* Xenia kernel shim model:
  https://github.com/xenia-project/xenia/blob/master/docs/kernel.md
