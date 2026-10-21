#!/usr/bin/env python3
"""Measure full-codegen and syntax-only analysis over the same MSVC object targets."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
from time import perf_counter


OWNED_SOURCE_DIRECTORIES = {"src", "examples", "benchmarks", "tests"}
CPP_EXTENSIONS = {".cc", ".cpp", ".cxx", ".c++"}
COMPILABLE_EXTENSIONS = CPP_EXTENSIONS | {".c", ".cu"}


def is_owned_cpp_source(source: Path, project_dir: Path) -> bool:
    try:
        relative = source.resolve().relative_to(project_dir)
    except ValueError:
        return False
    return relative.parts[0] in OWNED_SOURCE_DIRECTORIES and source.suffix.lower() in CPP_EXTENSIONS


def ninja_target(output: str, command_directory: Path, build_dir: Path) -> str:
    target = Path(output)
    if not target.is_absolute():
        target = command_directory / target
    try:
        return target.resolve().relative_to(build_dir).as_posix()
    except ValueError as error:
        raise ValueError(f"Compile output is outside the build directory: {output}") from error


def cmake_cache_value(cache_file: Path, name: str) -> str:
    prefix = f"{name}:"
    for line in cache_file.read_text(encoding="utf-8").splitlines():
        if line.startswith(prefix):
            return line.split("=", maxsplit=1)[1]
    raise ValueError(f"CMake cache does not define {name}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project-dir", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--mode", choices=("full-codegen", "syntax-only"), required=True)
    parser.add_argument("--parallel", type=int, default=4)
    parser.add_argument("--dry-run", action="store_true", help="Ask Ninja to list the commands without running them")
    parser.add_argument("--github-output", type=Path, help="Append the measured duration to a GitHub Actions environment file")
    args = parser.parse_args()

    project_dir = args.project_dir.resolve()
    build_dir = args.build_dir.resolve()
    database_path = build_dir / "compile_commands.json"
    if not database_path.is_file():
        parser.error(f"Compilation database not found: {database_path}")
    cache_file = build_dir / "CMakeCache.txt"
    if not cache_file.is_file():
        parser.error(f"CMake cache not found: {cache_file}")
    if args.parallel < 1:
        parser.error("--parallel must be greater than zero")

    try:
        expected_experiment = "ON" if args.mode == "syntax-only" else "OFF"
        actual_experiment = cmake_cache_value(cache_file, "PANDA_EXPERIMENT_MSVC_SYNTAX_ONLY").upper()
        if actual_experiment != expected_experiment:
            raise ValueError(
                f"PANDA_EXPERIMENT_MSVC_SYNTAX_ONLY is {actual_experiment}, expected {expected_experiment} for {args.mode}"
            )
        for language in ("C", "CXX", "CUDA"):
            launcher = cmake_cache_value(cache_file, f"CMAKE_{language}_COMPILER_LAUNCHER")
            if launcher:
                raise ValueError(
                    f"CMAKE_{language}_COMPILER_LAUNCHER must be empty for a compiler-cache-free A/B measurement"
                )

        database = json.loads(database_path.read_text(encoding="utf-8"))
        targets: set[str] = set()
        owned_cpp_targets: set[str] = set()
        missing_wx: list[str] = []
        for entry in database:
            source = Path(entry["file"])
            if not source.is_absolute():
                source = Path(entry["directory"]) / source
            if source.suffix.lower() not in COMPILABLE_EXTENSIONS:
                continue
            output = entry.get("output")
            if not output:
                raise ValueError(f"Compilation database entry has no output field: {source}")
            targets.add(ninja_target(output, Path(entry["directory"]), build_dir))
            if is_owned_cpp_source(source, project_dir):
                owned_cpp_targets.add(ninja_target(output, Path(entry["directory"]), build_dir))
                command = entry.get("command", "")
                if "/WX" not in command.upper():
                    missing_wx.append(str(source))
    except (KeyError, OSError, json.JSONDecodeError, ValueError) as error:
        print(f"Cannot prepare MSVC object-build measurement: {error}", file=sys.stderr)
        return 2

    if not targets or not owned_cpp_targets:
        print("Compilation database has no C/C++/CUDA targets or no Panda C++ sources", file=sys.stderr)
        return 2
    if missing_wx:
        print("Panda C++ compilation entries missing /WX:", file=sys.stderr)
        print("\n".join(missing_wx), file=sys.stderr)
        return 2

    target_list = sorted(targets)
    target_fingerprint = hashlib.sha256("\n".join(target_list).encode("utf-8")).hexdigest()
    expected_fingerprint = os.environ.get("MSVC_OBJECT_TARGETS_SHA256")
    if args.mode == "syntax-only":
        if expected_fingerprint and expected_fingerprint != target_fingerprint:
            print("The syntax-only pass does not use the same object-target set as the full-codegen baseline", file=sys.stderr)
            return 2
        if os.environ.get("GITHUB_ACTIONS") == "true" and not expected_fingerprint:
            print("The full-codegen target-set fingerprint is missing from the GitHub Actions environment", file=sys.stderr)
            return 2
    print(f"Panda C++ object targets with /WX: {len(owned_cpp_targets)}")
    print(f"Total C/C++/CUDA object targets: {len(target_list)}")

    inspection = subprocess.run(
        ["ninja", "-C", str(build_dir), "-t", "commands", *target_list],
        cwd=project_dir,
        capture_output=True,
        check=False,
        text=True,
    )
    if inspection.returncode != 0:
        print(inspection.stderr, file=sys.stderr)
        return inspection.returncode
    commands = inspection.stdout.casefold()
    if "ccache" in commands:
        print("CMake's Ninja commands still invoke ccache; refusing an uncontrolled comparison", file=sys.stderr)
        return 2
    syntax_launcher_count = commands.count("msvcsyntaxonlyrun.cmake")
    expected_launcher_count = len(owned_cpp_targets) if args.mode == "syntax-only" else 0
    if syntax_launcher_count != expected_launcher_count:
        print(
            f"Expected {expected_launcher_count} syntax-only launchers in Ninja commands, found {syntax_launcher_count}",
            file=sys.stderr,
        )
        return 2

    command = ["ninja", "-C", str(build_dir), "-j", str(args.parallel)]
    if args.dry_run:
        command.append("-n")
    command.extend(target_list)
    started = perf_counter()
    result = subprocess.run(
        command,
        cwd=project_dir,
        stdout=subprocess.DEVNULL if args.dry_run else None,
        check=False,
    )
    elapsed = perf_counter() - started
    metric = "FULL_CODEGEN_ANALYSIS_SECONDS" if args.mode == "full-codegen" else "SYNTAX_ONLY_ANALYSIS_SECONDS"
    print(f"{metric}={elapsed:.2f}")
    if result.returncode != 0:
        return result.returncode
    if args.github_output:
        with args.github_output.open("a", encoding="utf-8", newline="\n") as output_file:
            output_file.write(f"{metric}={elapsed:.2f}\n")
            if args.mode == "full-codegen":
                output_file.write(f"MSVC_OBJECT_TARGETS_SHA256={target_fingerprint}\n")
    if args.dry_run:
        print("Dry run only; no compiler or analyzer was invoked")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
