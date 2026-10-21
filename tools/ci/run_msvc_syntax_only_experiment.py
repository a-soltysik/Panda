#!/usr/bin/env python3
"""Run syntax-only compiler and existing analysis checks for Panda C++ objects."""

from __future__ import annotations

import argparse
import json
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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project-dir", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--parallel", type=int, default=4)
    parser.add_argument("--dry-run", action="store_true", help="Ask Ninja to list the commands without running them")
    args = parser.parse_args()

    project_dir = args.project_dir.resolve()
    build_dir = args.build_dir.resolve()
    database_path = build_dir / "compile_commands.json"
    if not database_path.is_file():
        parser.error(f"Compilation database not found: {database_path}")
    if args.parallel < 1:
        parser.error("--parallel must be greater than zero")

    try:
        database = json.loads(database_path.read_text(encoding="utf-8"))
        targets: set[str] = set()
        syntax_only_count = 0
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
                syntax_only_count += 1
                command = entry.get("command", "")
                if "/WX" not in command.upper():
                    missing_wx.append(str(source))
    except (KeyError, OSError, json.JSONDecodeError, ValueError) as error:
        print(f"Cannot prepare syntax-only experiment: {error}", file=sys.stderr)
        return 2

    if not targets or not syntax_only_count:
        print("Compilation database has no C/C++/CUDA targets or no Panda C++ sources", file=sys.stderr)
        return 2
    if missing_wx:
        print("Panda C++ compilation entries missing /WX:", file=sys.stderr)
        print("\n".join(missing_wx), file=sys.stderr)
        return 2

    target_list = sorted(targets)
    print(f"Panda C++ syntax-only units with /WX: {syntax_only_count}")
    print(f"Total C/C++/CUDA object targets (including full code generation): {len(target_list)}")
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
    print(f"SYNTAX_ONLY_ANALYSIS_SECONDS={elapsed:.2f}")
    if result.returncode != 0:
        return result.returncode
    if args.dry_run:
        print("Dry run only; no compiler or analyzer was invoked")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
