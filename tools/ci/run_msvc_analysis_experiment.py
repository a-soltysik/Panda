#!/usr/bin/env python3
"""Measure integrated MSVC analysis against parallel compile-database analysis."""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
from time import perf_counter


OWNED_SOURCE_DIRECTORIES = {"src", "examples", "benchmarks", "tests"}
CPP_EXTENSIONS = {".cc", ".cpp", ".cxx", ".c++"}
EXCLUDE_HEADER_FILTER = r"(^|[/\\])(_deps|legacy)[/\\]"


def cmake_cache(cache_file: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in cache_file.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("//") or line.startswith("#") or ":" not in line:
            continue
        name, value = line.split("=", maxsplit=1)
        values[name.split(":", maxsplit=1)[0]] = value
    return values


def absolute_source(entry: dict[str, str]) -> Path:
    source = Path(entry["file"])
    if not source.is_absolute():
        source = Path(entry["directory"]) / source
    return source.resolve()


def is_owned_cpp_source(source: Path, project_dir: Path) -> bool:
    try:
        relative = source.relative_to(project_dir)
    except ValueError:
        return False
    return (
        bool(relative.parts)
        and relative.parts[0] in OWNED_SOURCE_DIRECTORIES
        and source.suffix.lower() in CPP_EXTENSIONS
    )


def compile_command_text(entry: dict[str, str]) -> str:
    if "command" in entry:
        return entry["command"]
    return subprocess.list2cmdline(entry["arguments"])


def compilation_fingerprint(entries: list[dict[str, str]], project_dir: Path, build_dir: Path) -> str:
    records: list[str] = []
    for entry in entries:
        source = absolute_source(entry)
        source_relative = source.relative_to(project_dir).as_posix()
        output_value = entry.get("output")
        if not output_value:
            raise ValueError(f"Compile database entry has no output field: {source}")
        output = Path(output_value)
        if not output.is_absolute():
            output = Path(entry["directory"]) / output
        try:
            output_relative = output.resolve().relative_to(build_dir).as_posix()
        except ValueError as error:
            raise ValueError(f"Compile output is outside the build directory: {entry.get('output')}") from error
        command = compile_command_text(entry)
        for root in sorted((str(build_dir), str(project_dir)), key=len, reverse=True):
            command = command.replace(root.replace("/", "\\"), "<ROOT>")
            command = command.replace(root.replace("\\", "/"), "<ROOT>")
        records.append(f"{source_relative}|{output_relative}|{command}")
    return hashlib.sha256("\n".join(sorted(records)).encode("utf-8")).hexdigest()


def read_inputs(
    project_dir: Path, build_dir: Path, expected_analysis: bool
) -> tuple[dict[str, str], list[dict[str, str]]]:
    cache_path = build_dir / "CMakeCache.txt"
    database_path = build_dir / "compile_commands.json"
    if not cache_path.is_file() or not database_path.is_file():
        raise ValueError(f"Expected CMakeCache.txt and compile_commands.json in {build_dir}")
    cache = cmake_cache(cache_path)
    if cache.get("PANDA_WARNINGS_AS_ERRORS", "").upper() != "ON":
        raise ValueError("PANDA_WARNINGS_AS_ERRORS must be ON for the MSVC experiment")
    for language in ("C", "CXX", "CUDA"):
        launcher = cache.get(f"CMAKE_{language}_COMPILER_LAUNCHER", "")
        if launcher.strip():
            raise ValueError(
                f"CMAKE_{language}_COMPILER_LAUNCHER must be empty; compiler caching is disabled for this experiment"
            )
    for option in ("PANDA_ENABLE_CLANG_TIDY", "PANDA_ENABLE_CPPCHECK"):
        expected = "ON" if expected_analysis else "OFF"
        if cache.get(option, "").upper() != expected:
            raise ValueError(f"{option} must be {expected} for this experiment mode")

    database = json.loads(database_path.read_text(encoding="utf-8"))
    entries = [
        entry
        for entry in database
        if is_owned_cpp_source(absolute_source(entry), project_dir)
    ]
    if not entries:
        raise ValueError("The compilation database has no Panda C++ sources")
    missing_wx = [
        str(absolute_source(entry))
        for entry in entries
        if "/WX" not in compile_command_text(entry).upper()
    ]
    if missing_wx:
        raise ValueError("Panda C++ compilation entries missing /WX:\n" + "\n".join(missing_wx))
    if any("ccache" in compile_command_text(entry).casefold() for entry in entries):
        raise ValueError("The Panda C++ compilation database invokes ccache")
    return cache, entries


def append_github_output(path: Path | None, key_values: dict[str, str]) -> None:
    if path is None:
        return
    with path.open("a", encoding="utf-8", newline="\n") as output_file:
        for name, value in key_values.items():
            output_file.write(f"{name}={value}\n")


def inspect_ninja_commands(build_dir: Path) -> None:
    result = subprocess.run(
        ["ninja", "-C", str(build_dir), "-t", "commands"],
        capture_output=True,
        check=False,
        text=True,
    )
    if result.returncode:
        raise RuntimeError(result.stderr or "Ninja could not list the build commands")
    if "ccache" in result.stdout.casefold():
        raise ValueError("Ninja commands still invoke ccache; refusing a cached measurement")


def measure_build(args: argparse.Namespace) -> int:
    project_dir = args.project_dir.resolve()
    build_dir = args.build_dir.resolve()
    try:
        _, entries = read_inputs(project_dir, build_dir, expected_analysis=args.mode == "integrated")
        fingerprint = compilation_fingerprint(entries, project_dir, build_dir)
        inspect_ninja_commands(build_dir)
    except (KeyError, OSError, json.JSONDecodeError, ValueError, RuntimeError) as error:
        print(f"Cannot prepare MSVC build measurement: {error}", file=sys.stderr)
        return 2

    print(f"Panda C++ compile entries with /WX: {len(entries)}")
    unique_sources = {absolute_source(entry) for entry in entries}
    print(f"Unique Panda C++ sources: {len(unique_sources)}")
    print(f"Panda C++ compile-entry fingerprint: {fingerprint}")
    metric = "INTEGRATED_BUILD_SECONDS" if args.mode == "integrated" else "PARALLEL_BUILD_SECONDS"

    clean = subprocess.run(["ninja", "-C", str(build_dir), "-t", "clean"], check=False)
    if clean.returncode:
        return clean.returncode
    command = ["cmake", "--build", "--preset", "msvc-quality", "--parallel", "4"]
    print("Running: " + subprocess.list2cmdline(command))
    started = perf_counter()
    result = subprocess.run(command, cwd=project_dir, check=False)
    elapsed = perf_counter() - started
    print(f"{metric}={elapsed:.2f}")
    append_github_output(
        args.github_output,
        {"elapsed_seconds": f"{elapsed:.2f}", "compile_fingerprint": fingerprint},
    )
    return result.returncode


def clang_tidy_command(source: Path, clang_tidy: str, build_dir: Path, cxx_flags: str) -> list[str]:
    command = [
        clang_tidy,
        "-p",
        str(build_dir),
        "--extra-arg=-Wno-unknown-warning-option",
        f"--exclude-header-filter={EXCLUDE_HEADER_FILTER}",
        "--extra-arg=--target=x86_64-pc-windows-msvc",
        "--extra-arg-before=--driver-mode=cl",
    ]
    exception_flag = re.search(r"(/EH[^ ]+)", cxx_flags)
    if exception_flag:
        command.append(f"--extra-arg={exception_flag.group(1)}")
    command.append(str(source))
    return command


def run_clang_tidy(
    sources: list[Path], executable: str, build_dir: Path, cxx_flags: str, jobs: int
) -> tuple[float, int]:
    started = perf_counter()
    failures = 0
    with ThreadPoolExecutor(max_workers=jobs) as executor:
        pending = {
            executor.submit(
                subprocess.run,
                clang_tidy_command(source, executable, build_dir, cxx_flags),
                cwd=build_dir.parent,
                capture_output=True,
                check=False,
                text=True,
                errors="replace",
            ): source
            for source in sources
        }
        for future in as_completed(pending):
            source = pending[future]
            result = future.result()
            output = result.stdout + result.stderr
            if output:
                print(f"[clang-tidy] {source}\n{output}", end="", flush=True)
            if result.returncode:
                failures += 1
                message = f"clang-tidy failed for {source} with exit code {result.returncode}"
                print(message, file=sys.stderr)
    return perf_counter() - started, failures


def run_cppcheck(entries: list[dict[str, str]], build_dir: Path, executable: str, jobs: int) -> tuple[float, int]:
    analysis_dir = build_dir / "panda-standalone-analysis"
    analysis_dir.mkdir(exist_ok=True)
    database_path = analysis_dir / "compile_commands.json"
    database_path.write_text(json.dumps(entries), encoding="utf-8")
    command = [
        executable,
        f"--project={database_path}",
        "--enable=warning,style,performance,portability",
        "--error-exitcode=1",
        "--std=c++23",
        "--inline-suppr",
        "--quiet",
        "-j",
        str(jobs),
    ]
    print("Running: " + subprocess.list2cmdline(command))
    started = perf_counter()
    result = subprocess.run(command, check=False, capture_output=True, text=True, errors="replace")
    output = result.stdout + result.stderr
    if output:
        print(f"[cppcheck]\n{output}", end="", flush=True)
    return perf_counter() - started, result.returncode


def run_analysis(args: argparse.Namespace) -> int:
    project_dir = args.project_dir.resolve()
    build_dir = args.build_dir.resolve()
    try:
        cache, entries = read_inputs(project_dir, build_dir, expected_analysis=True)
        fingerprint = compilation_fingerprint(entries, project_dir, build_dir)
        inspect_ninja_commands(build_dir)
    except (KeyError, OSError, json.JSONDecodeError, ValueError, RuntimeError) as error:
        print(f"Cannot prepare standalone MSVC analysis: {error}", file=sys.stderr)
        return 2

    clang_tidy = shutil.which("clang-tidy")
    cppcheck = shutil.which("cppcheck")
    if not clang_tidy or not cppcheck:
        print("clang-tidy and cppcheck must both be available on PATH", file=sys.stderr)
        return 2

    cxx_flags = cache.get("CMAKE_CXX_FLAGS", "")
    print(f"Panda C++ compile entries to analyze: {len(entries)}")
    sources = sorted({absolute_source(entry) for entry in entries})
    print(f"Unique Panda C++ sources to analyze: {len(sources)}")
    print(f"Panda C++ compile-entry fingerprint: {fingerprint}")
    print(f"clang-tidy workers: {args.jobs}; Cppcheck workers: {args.jobs}")

    started = perf_counter()
    with ThreadPoolExecutor(max_workers=2) as executor:
        tidy_future = executor.submit(run_clang_tidy, sources, clang_tidy, build_dir, cxx_flags, args.jobs)
        cppcheck_future = executor.submit(run_cppcheck, entries, build_dir, cppcheck, args.jobs)
        tidy_seconds, tidy_failures = tidy_future.result()
        cppcheck_seconds, cppcheck_code = cppcheck_future.result()
    elapsed = perf_counter() - started
    print(f"CLANG_TIDY_SECONDS={tidy_seconds:.2f}")
    print(f"CPPCHECK_SECONDS={cppcheck_seconds:.2f}")
    print(f"PARALLEL_ANALYSIS_SECONDS={elapsed:.2f}")
    append_github_output(
        args.github_output,
        {
            "analysis_seconds": f"{elapsed:.2f}",
            "clang_tidy_seconds": f"{tidy_seconds:.2f}",
            "cppcheck_seconds": f"{cppcheck_seconds:.2f}",
            "compile_fingerprint": fingerprint,
        },
    )
    if tidy_failures or cppcheck_code:
        print(
            f"Standalone analysis failed: clang-tidy invocations={tidy_failures}, cppcheck exit={cppcheck_code}",
            file=sys.stderr,
        )
        return 1
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)

    build_parser = commands.add_parser("build", help="measure a fresh quality build")
    build_parser.add_argument("--mode", choices=("integrated", "compile-only"), required=True)
    build_parser.add_argument("--project-dir", type=Path, required=True)
    build_parser.add_argument("--build-dir", type=Path, required=True)
    build_parser.add_argument("--github-output", type=Path)
    build_parser.set_defaults(run=measure_build)

    analysis_parser = commands.add_parser("analyze", help="run standalone analyzers concurrently")
    analysis_parser.add_argument("--project-dir", type=Path, required=True)
    analysis_parser.add_argument("--build-dir", type=Path, required=True)
    analysis_parser.add_argument("--jobs", type=int, default=2)
    analysis_parser.add_argument("--github-output", type=Path)
    analysis_parser.set_defaults(run=run_analysis)

    args = parser.parse_args()
    if getattr(args, "jobs", 1) < 1:
        parser.error("--jobs must be greater than zero")
    if getattr(args, "github_output", None) is not None:
        args.github_output = args.github_output.resolve()
    return args.run(args)


if __name__ == "__main__":
    raise SystemExit(main())
