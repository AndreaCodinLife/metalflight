#!/usr/bin/env python3
"""Qualify the opt-in TEB fiber host-window workaround in a fresh owned prefix."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def clean_env():
    prefixes = ("WINE", "CX_", "MSFS24_", "ROSETTA_", "DXMT_", "GST_", "MULLION_")
    return {k: v for k, v in os.environ.items() if not k.startswith(prefixes)}


def stop(runtime, env):
    for args in (("-k",), ("-w",)):
        subprocess.run([str(runtime / "bin/wineserver"), *args], env=env,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=30)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--runtime", required=True, type=Path)
    ap.add_argument("--cc", required=True, type=Path)
    ap.add_argument("--work", required=True, type=Path)
    args = ap.parse_args()
    runtime = args.runtime.resolve()
    work = args.work.resolve()
    if work.exists():
        ap.error("--work must be a new directory")
    if not (runtime / "bin/wine").is_file():
        ap.error("runtime is missing bin/wine")
    wineboot = runtime / "lib/wine/x86_64-windows/wineboot.exe"
    if not wineboot.is_file():
        ap.error("runtime is missing lib/wine/x86_64-windows/wineboot.exe")
    work.mkdir(parents=True, mode=0o700)
    source = Path(__file__).with_name("fiber_stack_open_contract.c")
    exe = work / "fiber_stack_open_contract.exe"
    compile_cmd = [str(args.cc.expanduser().absolute()), "-O1", "-Wall", "-Wextra",
                   "-Werror", str(source), "-o", str(exe)]
    with (work / "compile.log").open("x") as log:
        subprocess.run(compile_cmd, stdout=log, stderr=subprocess.STDOUT, check=True)

    rows = []
    result = {
        "schema": 1,
        "runtime": str(runtime),
        "runtime_ntdll_sha256": sha(runtime / "lib/wine/x86_64-unix/ntdll.so"),
        "fixture_source_sha256": sha(source),
        "fixture_exe_sha256": sha(exe),
        "compile_command": compile_cmd,
        "cases": rows,
        "game_launched": False,
    }
    (work / "results.json").write_text(json.dumps(result, indent=2) + "\n")

    cases = (
        ("enabled", "1", "fiber_stack_open_contract.exe", True),
        ("disabled", "0", "fiber_stack_open_contract.exe", False),
        ("wrong-target", "1", "different_target.exe", False),
    )
    all_passed = True
    for label, gate, target, preserve_guard in cases:
        case_dir = work / label
        prefix = case_dir / "prefix"
        case_dir.mkdir(mode=0o700)
        env = clean_env()
        env.update(
            WINEPREFIX=str(prefix),
            CX_ROOT=str(runtime),
            CX_BOTTLE_PATH=str(case_dir),
            WINEDEBUG="-all,+msfs_platform,+seh",
            WINEMSYNC="0",
            WINEDLLOVERRIDES="winedbg.exe=d;mscoree,mshtml=",
            ROSETTA_ADVERTISE_AVX="1",
            MSFS24_WINE_GUARD_STEP="1",
            MSFS24_WINE_STACK_POLL="1",
            MSFS24_WINE_STACK_PREFLIGHT="1",
            MSFS24_WINE_STACK_PREFLIGHT_WINDOW="0x40000",
            MSFS24_WINE_STACK_OPEN=gate,
            MSFS24_WINE_GUARD_TARGET=target,
        )
        try:
            with (case_dir / "wineboot.log").open("x") as log:
                boot = subprocess.run([str(runtime / "bin/wine"), str(wineboot), "-i"], env=env,
                                      stdout=log, stderr=subprocess.STDOUT, timeout=180)
            stop(runtime, env)
            if boot.returncode:
                raise RuntimeError(f"{label}: wineboot exited {boot.returncode}")
            try:
                with (case_dir / "run.log").open("x") as log:
                    run = subprocess.run([str(runtime / "bin/wine"), str(exe), label], env=env,
                                         stdout=log, stderr=subprocess.STDOUT, timeout=60)
                code = run.returncode
            except subprocess.TimeoutExpired:
                code = 124
            finally:
                stop(runtime, env)
            text = (case_dir / "run.log").read_text(errors="replace").replace("\r", "")
            oracle = f"CONTRACT mode={label} "
            line = next((line for line in text.splitlines() if oracle in line), "")
            if preserve_guard:
                fibers = [line for line in text.splitlines() if line.startswith("FIBER index=")]
                guards = []
                passed = code == 0 and "fibers=4 PASS=1" in line and len(fibers) == 4
                for fiber_line in fibers:
                    match = re.search(r"prewrite_limit=([0-9a-f]+) postwrite_limit=([0-9a-f]+) guard=([0-9a-f]+) protect=([0-9a-f]+)/([0-9a-f]+) .* PASS=1$", fiber_line)
                    if not match:
                        passed = False
                        continue
                    pre_limit, post_limit, guard, before_protect, after_protect = match.groups()
                    guards.append(guard)
                    passed &= pre_limit == post_limit and before_protect == "104" and after_protect == "104"
                passed &= len(set(guards)) == 4
                for guard in guards:
                    passed &= bool(re.search(rf"stack host-guard opened:.* guard={guard} ", text))
            else:
                guard_match = re.search(r"^STACK .* guard=([0-9a-f]+) ", text, re.MULTILINE)
                guard = guard_match.group(1) if guard_match else None
                fault_match = re.search(r"dispatch_exception code=c00000fd(?:(?!dispatch_exception rip=)[\s\S])*?info\[1\]=0*([0-9a-f]+)", text, re.IGNORECASE)
                fault_address = fault_match.group(1) if fault_match else None
                passed = (code != 124 and guard is not None and fault_address is not None and
                          int(guard, 16) == int(fault_address, 16) and
                          "Unhandled stack overflow" in text and
                          "stack host-guard opened:" not in text)
                line = f"expected stack overflow guard={guard} fault={fault_address}" if passed else line
            row = {"case": label, "passed": bool(passed), "exit_code": code,
                   "oracle": line, "log_sha256": sha(case_dir / "run.log")}
            rows.append(row)
            print(label, "PASS" if passed else "FAIL", flush=True)
            all_passed &= bool(passed)
        except Exception as exc:
            rows.append({"case": label, "passed": False, "error": str(exc)})
            print(label, "FAIL", str(exc), flush=True)
            all_passed = False
        finally:
            stop(runtime, env)
        result["all_passed"] = all_passed
        (work / "results.json").write_text(json.dumps(result, indent=2) + "\n")

    result["all_passed"] = all_passed
    (work / "results.json").write_text(json.dumps(result, indent=2) + "\n")
    return 0 if all_passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
