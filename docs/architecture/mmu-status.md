# Onix360 MMU status

## Implemented experimental model

The repository contains an opt-in 32-bit PowerPC OEA hashed-page-table model:

- SDR1 HTABORG/HTABMASK decoding and table-size/alignment validation.
- Eight-entry PTEGs, big-endian PTE0/PTE1, primary and secondary hash lookup.
- VSID/API matching, 4 KiB page offsets, PP permissions and R/C updates.
- Segment T (direct-store), Ks/Kp and N handling in the memory-bus adapter.
- Cross-page accesses when the translated physical pages are contiguous.
- Synthetic tests and an explicit XenonCore opt-in configuration path.

This is a generic PowerPC OEA compatibility model. It is not proof that the Xbox 360 Xenon uses the same SDR1/HTAB mechanism.

## Guest address-space windows

A separate classifier now models the software-visible 32-bit guest address windows used by Xenia: 4 KiB/64 KiB virtual ranges, XEX image ranges (including the encrypted window), and physical aliases using 64 KiB, 16 MiB and 4 KiB windows. This is a guest mapping policy reference; it does not replace the Xenon hypervisor page table.

## Xbox 360-specific gap

The Xenon is a 64-bit PowerPC processor that executes Xbox 360 software in 32-bit mode. Public Xbox 360 architecture research describes a console-specific hypervisor MMU and a 64-bit physical-address representation whose upper half carries encryption/integrity flags. The hypervisor maintains its own private page table and executable-page permissions.

Therefore the OEA SDR1/HTAB implementation above must remain labelled experimental and must not be counted as a complete Xenon hardware MMU. The production Xenon path still needs evidence-based implementation of:

1. The console-specific page-table entry and walk format.
2. Effective-to-physical translation and upper 32-bit cryptographic flags.
3. Hypervisor-owned page permissions and executable-page state.
4. L2 encryption/hash behavior and the boundaries between CPU, GPU and DMA memory access.
5. Validation against documented Xenon behavior or trusted hardware traces.

## References

- Xenia CPU notes: Xenon is a 64-bit PowerPC processor running in 32-bit mode: https://github.com/xenia-project/xenia/blob/master/docs/cpu.md
- Xbox 360 architecture research, secure communication and hypervisor MMU: https://blink.copetti.org/writings/consoles/xbox-360/anti-piracy-and-homebrew/
- PowerPC Programming Environments Manual, SDR1 and 32-bit OEA hashed page tables: https://bitsavers.trailing-edge.com/components/motorola/PowerPC/2001_MPCFPE32B_PowerPC_Programming_Environments_Manual_Rev_2.pdf
