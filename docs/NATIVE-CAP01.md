# Native CAP01 profile: MSI Z390M / RTX 3060 Ti

This is an experimental **manual-only exact-card profile**, with one successful
physical Windows boot on 2026-10-27. Read [the result and limits](NATIVE-CAP01-RESULT.md).
It is not validated for other RTX 3060 Ti cards, other motherboard firmware, or
the generic GUI's full catalog. The upstream generic route remains separate.

## What changed

The failing machine initially used firmware `CsmVideo/BiosVideo`, GUID
`29cf55f8-b675-4f5d-8f2f-b87a3ecfd063`, despite booting the USB through UEFI.
The later read-only capture showed native `nvgop-ga1xx` after the firmware's
CSM video route was disabled. Native-profile guards require driver version
`0x6000a`, image size 179776 bytes, the captured driver entry RVAs and image path,
the target GPU/subsystem, and the saved GPU PROM digest.

The loader validates the TextOut/GraphicsConsole ownership graph, detaches its
consumers in order, releases the GPU owner, performs the existing unlock
transaction, verifies cleanup and selectors, and starts Windows directly.
It does not reconnect GOP after FLR. The screen can stay dark until Windows
initializes the display. Logging continues in a separate `HANDOFF.TXT` after
the main report handles have closed.

## Build without touching hardware

Requirements: Windows x64, Python 3.14+, Zig 0.14.1. Use `ZIG_EXE` or `--zig`.

```powershell
python build_native_cap01.py --zig C:\Tools\zig\zig.exe
```

This runs nine host-test groups and creates:

- `build/native-cap01/UNLOCK-MANUAL.EFI`
- `build/native-cap01/PROBE-CONSOLE.EFI`
- `build/native-cap01/build-result.json`

No USB writes, GPU access, downloads or scheduled tasks occur during this build.
For host tests alone, pass `--tests-only`.

## Stage the read-only diagnostic first

```powershell
.\Stage-NativeCap01.ps1 -DriveLetter F -ProbeOnly
```

The target must be an existing non-system FAT32 USB disk. The script does not
format or repartition it. Existing replaced files are backed up, and capture
directories are retained. `-FolderPath .\build\staging-test` stages locally.

Boot the UEFI USB entry. Do not press U. The probe writes
`GPU-PROBE/CAPxx/CONSOLE.TXT` and returns to firmware. It enumerates protocols
and metadata without PCI/MMIO access, driver detach, resets or unlock payloads.
Raw logs can contain machine-specific identifiers; keep them private.

Changing CSM/UEFI settings is outside the staging script. Confirm the Windows
UEFI/GPT boot path and any encryption recovery requirements before changing
firmware settings. UEFI mode does not require Secure Boot; these locally built
EFI applications are unsigned. Do not substitute another card's ROM digest.

## Stage this exact unlock profile

First generate the pinned firmware inputs using the existing upstream process:

```powershell
python prepare_assets.py
.\Stage-NativeCap01.ps1 -DriveLetter F
```

Firmware is generated locally and checked against `firmware-manifest.json`.
It is not committed in this fork. Never use the old generic automatic loader
for this profile. The native build rejects automatic mode and missing native
handoff settings before any driver release.

Save work and use a full Windows shutdown (`shutdown /s /t 0`). Choose the USB
from the firmware boot menu, wait for READY, and press **U once**. Do not retry U
within the same power session. A Windows startup, picture, correct numerical
results and sustained throughput must all be checked in that same boot.

If the screen stays black, keep the computer running long enough for the
optional Windows collector below. Monitor power cycling or reconnecting DP
can be checked without power-cycling the GPU. A forced reboot loses the
opportunity to inspect that Windows session. Normal recovery is to power off,
remove USB and select the existing Windows Boot Manager; no Windows EFI files
or boot variables are changed by this project.

## Optional one-boot Windows evidence

`windows-diagnostics/Collect-NvidiaState.ps1` records NVML output, adapter problem
codes, active monitors and relevant boot/display events. Commands have a timeout;
it never resets the GPU, changes drivers, benchmarks, or transmits data.

For a manual capture, no task installation is needed:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\windows-diagnostics\Collect-NvidiaState.ps1 -Mode Manual -OutputRoot .\local\windows-capture
```

For collection before login, run `windows-diagnostics/Install-Capture.ps1` from
an elevated PowerShell. Review the script first. It copies the manifest-checked
collector into a protected `ProgramData/CodexUefiCapture/native-cap01-v4` folder,
installs a SYSTEM startup task, tests it in the current boot and arms it for
one following Windows boot, expiring after 48 hours. This **does change Windows
Task Scheduler and creates protected diagnostic files**. No driver/kernel
component is installed. The task removes itself after five samples over about
four minutes. Do not consume the armed attempt with an intermediate ordinary boot.

Logs remain under that ProgramData directory. `Remove-Capture.ps1`, run elevated,
removes/disarms the diagnostic task and preserves files. A hard power-off can
leave only partial samples and a dormant task; no automatic unlock is performed.

## Known limits

The profile binds specific image metadata, not a complete hash of all loaded
firmware code. A GUID and size are not a code signature. The inherited generic
GOP-reconnect path has known validation limitations; it is disabled and required
to stay disabled in this native build. Host tests do not simulate vendor firmware,
DisplayPort link training, Windows initialization or all failure modes.
