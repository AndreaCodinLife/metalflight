#!/usr/bin/env python3
"""Exercise Steam-only Wine options on authored executables in a fresh prefix."""
from pathlib import Path
import argparse
import json
import os
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    runtime = args.runtime.resolve()
    work = args.work.resolve()
    work.mkdir(mode=0o700)  # Never reuse a Steam/game prefix or prior results.
    source = Path(__file__).resolve().parents[1] / 'probes/steam_launch_options.c'
    subprocess.run([str(args.compiler.resolve()), '-O2', '-Wall', '-Wextra', '-static',
                    '-municode', str(source), '-o', str(work/'steamwebhelper.exe')], check=True)
    for name in ('other.exe', 'STEAMWEBHELPER.EXE', 'launcher-parent.exe'):
        # Case-insensitive APFS may identify the uppercase helper with the original.
        target = work/name
        if not target.exists(): shutil.copy2(work/'steamwebhelper.exe', target)
    env = {k:v for k,v in os.environ.items() if not k.startswith(
        ('WINE','CX_','DYLD_','GST_','MSFS24_','MULLION_','ROSETTA_','DXMT_'))}
    env.update(WINEPREFIX=str(work/'prefix'), WINEDEBUG='-all', WINEMSYNC='0',
               WINEDLLOVERRIDES='mscoree,mshtml=', ROSETTA_ADVERTISE_AVX='1')
    cases = [
        ('helper_enabled','steamwebhelper.exe','1',True,[]),
        ('helper_unset','steamwebhelper.exe',None,False,[]),
        ('helper_disabled','steamwebhelper.exe','0',False,[]),
        ('helper_invalid_value','steamwebhelper.exe','10',False,[]),
        ('other_executable','other.exe','1',False,[]),
        ('case_insensitive_name','STEAMWEBHELPER.EXE','1',True,[]),
        ('no_duplicate_option','steamwebhelper.exe','1',True,['--in-process-gpu']),
        ('similar_option_is_distinct','steamwebhelper.exe','1',True,['--in-process-gpu-extra']),
        ('windows_child_process','launcher-parent.exe','1',False,['--spawn']),
    ]
    results=[]
    try:
        for name, executable, value, expected, extra in cases:
            run_env=dict(env)
            if value is not None: run_env['MSFS24_WINE_STEAM_IN_PROCESS_GPU']=value
            argv=['--expect-on' if expected else '--expect-off','--unicode','caf\u00e9-\u65e5\u672c',*extra]
            run=subprocess.run([str(runtime/'bin/wine'),str(work/executable),*argv],
                env=run_env,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,text=True,timeout=35)
            output=[line for line in run.stdout.splitlines() if line.startswith('OwnedSteamOptions ')]
            result=dict(name=name,exit_code=run.returncode,output=output,passed=run.returncode==0 and bool(output))
            results.append(result);print(json.dumps(result),flush=True)
    finally:
        subprocess.run([str(runtime/'bin/wineserver'),'-k'],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,timeout=15,check=True)
        subprocess.run([str(runtime/'bin/wineserver'),'-w'],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,timeout=15,check=True)
        (work/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    assert len(results)==len(cases) and all(result['passed'] for result in results)


if __name__=='__main__': main()
