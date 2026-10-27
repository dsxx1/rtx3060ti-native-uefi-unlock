# One successful native UEFI boot — 2026-10-27

## Configuration

MSI MAG Z390M MORTAR (MS-7C00), BIOS 1.B1; Intel i5-9600KF; MSI RTX 3060 Ti LHR
8 GiB (`10de:2489`, subsystem `1462:3903`), VBIOS `94.04.46.00.d4`;
Windows 11 Pro 26100, NVIDIA 610.88. DisplayPort output on the RTX; no iGPU.

Earlier CSM-based attempts changed the expected selectors but lost display.
A read-only capture identified active `CsmVideo` in the ownership graph. After
switching to native UEFI video, the owner was `nvgop-ga1xx` `0x6000a`, with one
physical GOP at 2560x1440 and the same GraphicsConsole/ConSplitter consumer shape.
The profile below then completed one manual unlock and Windows/display recovery.

## Observed outcome

- Expected post-FLR selectors: SS0 `0x88888888`, SS1 `8`, FEAT PLM `0xffffffff`.
- Transaction: result 0, quiesced 1, fault 0, retained 0, maps 0, one FLR.
- Windows booted in that session; NVML returned normally, PnP problem code was 0,
  and DisplayPort was active in all five startup observations.
- No fresh nvlddmkm/Display/WHEA errors were found in the collected interval.
- Six FP16/BF16 GEMM validation cases passed, including non-square shapes;
  all checked cells agreed with their CPU reference (`max_abs=0`).

Instruction benchmark: `mma_bench.exe 20000 3`, 38 SMs, sm_86. TFLOPS are medians
of three repetitions, not end-to-end model or application throughput:

| Mode | Median TFLOPS | Three runs |
|---|---:|---|
| FP16 / FP16 accumulate | 72.2 | 70.2 / 72.2 / 72.5 |
| FP16 / FP32 accumulate | 72.5 | 72.8 / 72.5 / 70.2 |
| BF16 / FP32 accumulate | 72.6 | 72.9 / 72.6 / 69.3 |
| FP32 FFMA control | 17.4 | 17.4 / 17.7 / 16.7 |

FP32-accumulate Tensor rate is approximately equal to FP16-accumulate rate in
this instruction test, supporting the expected full-rate state in Windows.
There is no matched locked-state run of this same executable, so this is not a
measured 2x before/after speedup claim. Token generation, ComfyUI, games,
long-duration stability, restart and sleep/resume persistence remain untested.

The exact manually tested EFI was reproduced byte-for-byte by
`build_native_cap01.py` during publication preparation:

```
UNLOCK-MANUAL.EFI 116224 bytes
SHA256 006d73ba8ce6f2968a758a7276a668ae268bd68b5861feba6e30b4ee43ab6065

PROBE-CONSOLE.EFI 10240 bytes
SHA256 f6b0ec67fc3023ab55147a392aab73f936334bed15a3e0c8fe05aab84bde461b
```

These digests describe those exact builds, not all compilers or future edits.
Raw UEFI/Windows logs, PROM, firmware, partition GUIDs, serials, host names,
MAC addresses and local user paths are intentionally not published.
