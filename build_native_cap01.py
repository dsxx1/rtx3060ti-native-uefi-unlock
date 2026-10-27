# SPDX-License-Identifier: MIT
"""Build the tested, manual-only native CAP01 profile and read-only console probe.

No hardware access, USB writes, firmware downloads, or Windows task installation.
"""
from pathlib import Path
import argparse, hashlib, json, os, shutil, subprocess
import build

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--zig', help='Path to Zig 0.14.1 executable (or use ZIG_EXE)')
    parser.add_argument('--tests-only', action='store_true')
    args=parser.parse_args()
    zig=build.zig_path(args.zig)
    if not Path(zig).is_file():
        raise SystemExit('Zig not found: '+zig)
    out=build.BUILD/'native-cap01'
    out.mkdir(parents=True,exist_ok=True)
    flags=['-std=c11','-fshort-wchar','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
           '-D__KERNEL__','-DGU_UEFI_PORT','-DGU_SOFT_UNLOCK_BUILD=1',
           '-I'+str(build.UNLOCK/'compat'),'-I'+str(build.UNLOCK),
           '-I'+str(build.EDK2),'-I'+str(build.EDK2/'X64')]
    u=build.UNLOCK;p=build.PROBE
    jobs=[
        ('config', [u/s for s in ['tests/test_tailored_suite.c','config.c','checks.c','port.c','runtime.c','format.c','sha256.c','vendor/layout_610.c','vendor/prep_610.c','vendor/vbios_610.c','vendor/gfw_eval.c']],[]),
        ('direct-handoff',[u/'tests/test_direct_handoff.c'],['-Wno-unused-function']),
        ('handoff-trace',[u/s for s in ['tests/test_handoff_trace.c','config.c','format.c','sha256.c']],[]),
        ('console-release',[u/'tests/test_console_release.c'],[]),
        ('native-owner',[u/'tests/test_console_release.c'],['-DGU_NATIVE_CAP01_BUILD=1']),
        ('console-capture',[p/'test_console_capture.c',u/'format.c'],[]),
        ('console-usb-wrapper',[p/'test_uefi_probe.c',p/'capture.c',u/'format.c'],['-DGU_CONSOLE_ONLY']),
        ('gpu-capture',[p/'test_capture.c',p/'capture.c',u/'format.c'],[]),
        ('gpu-usb-wrapper',[p/'test_uefi_probe.c',p/'capture.c',u/'format.c'],[]),
    ]
    results=[]
    for name,sources,defines in jobs:
        exe=out/(name+'.exe')
        subprocess.run([zig,'cc',*flags,*defines,*map(str,sources),'-o',str(exe)],check=True)
        run=subprocess.run([str(exe)],check=True,capture_output=True,text=True)
        results.append({'test':name,'output':run.stdout.strip()})
        print(name+': PASS',flush=True)
    report={'profile':'native-cap01-v4','host_tests':results,'hardware_result_document':'docs/NATIVE-CAP01-RESULT.md'}
    if args.tests_only:
        (out/'test-results.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
        return
    efi=out/'UNLOCK-MANUAL.EFI'
    report['unlock']=build.build_efi(zig,[u/(s+'.c') for s in build.UNLOCK_SOURCES],['-DGU_SOFT_UNLOCK_BUILD=1','-DGU_NATIVE_CAP01_BUILD=1'],efi)
    probe=out/'PROBE-CONSOLE.EFI'
    probe_sources=[p/'uefi_probe.c',p/'console_capture.c',u/'format.c',u/'runtime.c']
    report['console_probe']=build.build_efi(zig,probe_sources,['-DGU_CONSOLE_ONLY','-I'+str(p)],probe)
    # The output is a source-build artifact, not automatically staged on any disk.
    (out/'build-result.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k!='host_tests'},indent=2))

if __name__=='__main__':
    main()
