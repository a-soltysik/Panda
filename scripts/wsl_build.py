#!/usr/bin/env python3
"""Synchronize the Windows checkout and build it from a WSL-local mirror."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


MIRROR_NAME = "Panda-wsl-build"
MARKER_NAME = ".panda-wsl-build-mirror.json"
PRESETS = {
    "gcc-development",
    "gcc-quality",
    "gcc-unity",
    "gcc-docs",
    "gcc-coverage",
    "gcc-sanitizers",
    "gcc-thread-sanitizer",
    "gcc-tests",
    "clang-tests",
}
TEST_PRESETS = {
    "gcc-quality",
    "gcc-coverage",
    "gcc-sanitizers",
    "gcc-thread-sanitizer",
    "gcc-tests",
    "clang-tests",
}


def parse_arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--preset", required=True, choices=sorted(PRESETS))
    parser.add_argument("--target", action="append", default=[])
    parser.add_argument("--jobs", type=int)
    parser.add_argument("--run-tests", action="store_true")
    parser.add_argument("--include-system-tests", action="store_true")
    parser.add_argument("--test-regex")
    parser.add_argument("--sync-only", action="store_true")
    return parser.parse_args()


def run(command, *, cwd=None, environment=None):
    print("+ " + " ".join(map(str, command)), flush=True)
    subprocess.run(command, cwd=cwd, env=environment, check=True)


def create_or_validate_mirror(source, mirror):
    if mirror.is_symlink():
        raise RuntimeError(f"Refusing symlink as managed mirror: {mirror}")
    marker = mirror / MARKER_NAME
    expected = {"source": str(source), "version": 1}
    if mirror.exists():
        if marker.is_symlink() or not marker.is_file():
            raise RuntimeError(
                f"Refusing to synchronize unmarked directory: {mirror}"
            )
        actual = json.loads(marker.read_text(encoding="utf-8"))
        if actual != expected:
            raise RuntimeError(
                f"Managed mirror belongs to a different source: {mirror}"
            )
        return
    mirror.mkdir(parents=True)
    marker.write_text(json.dumps(expected, indent=2) + "\n", encoding="utf-8")


def synchronize(source, mirror):
    exclusions = [
        "--exclude=/.git",
        "--exclude=/.cache/",
        "--exclude=/.idea/",
        "--exclude=/.vs/",
        "--exclude=/.vscode/",
        "--exclude=/.codex/",
        "--exclude=/.aws/",
        "--exclude=/.env",
        "--exclude=/.env.*",
        "--exclude=/.panda-wsl-build-mirror.json",
        "--exclude=/build*/",
        "--exclude=/cmake-build-*/",
        "--exclude=/out/",
        "--exclude=/CMakeUserPresets.json",
    ]
    run([
        "rsync", "--archive", "--delete", "--human-readable", "--stats",
        *exclusions, f"{source}/", f"{mirror}/",
    ])
    source_cache = source / ".cache" / "cpm"
    if source_cache.is_dir():
        target_cache = mirror / ".cache" / "cpm"
        target_cache.mkdir(parents=True, exist_ok=True)
        run([
            "rsync", "--archive", "--ignore-existing", "--human-readable",
            "--stats", f"{source_cache}/", f"{target_cache}/",
        ])


def available_memory_bytes():
    available = None
    for line in Path("/proc/meminfo").read_text().splitlines():
        if line.startswith("MemAvailable:"):
            available = int(line.split()[1]) * 1024
            break
    if available is None:
        available = 6 * 1024**3

    limit = Path("/sys/fs/cgroup/memory.max")
    current = Path("/sys/fs/cgroup/memory.current")
    if limit.is_file() and current.is_file():
        limit_text = limit.read_text().strip()
        if limit_text != "max":
            available = min(available, max(1, int(limit_text) - int(current.read_text())))
    return available


def recommended_jobs():
    cpus = os.cpu_count() or 1
    return min(cpus, max(1, available_memory_bytes() // (3 * 1024**3)), 8)


def configure_environment():
    environment = os.environ.copy()
    environment.pop("VK_INSTANCE_LAYERS", None)
    environment.pop("VK_LAYER_DEBUG_ACTION", None)
    environment.pop("VK_LAYER_LOG_FILENAME", None)
    # CPM checks copied Git trees; Windows checkout line endings otherwise look dirty in WSL.
    git_config_count = int(environment.get("GIT_CONFIG_COUNT", "0"))
    environment[f"GIT_CONFIG_KEY_{git_config_count}"] = "core.autocrlf"
    environment[f"GIT_CONFIG_VALUE_{git_config_count}"] = "true"
    environment["GIT_CONFIG_COUNT"] = str(git_config_count + 1)
    use_ccache = shutil.which("ccache") is not None
    if use_ccache:
        cache_directory = Path.home() / ".cache" / "panda-ccache"
        cache_directory.mkdir(parents=True, exist_ok=True)
        environment["CCACHE_DIR"] = str(cache_directory)
        environment["CCACHE_MAXSIZE"] = "10G"
    return environment, use_ccache


def run_build(arguments, mirror):
    environment, use_ccache = configure_environment()
    configure = ["cmake", "--preset", arguments.preset]
    if use_ccache:
        configure.extend([
            "-DCMAKE_C_COMPILER_LAUNCHER=ccache",
            "-DCMAKE_CXX_COMPILER_LAUNCHER=ccache",
        ])
        print("Using ccache in the WSL home directory.", flush=True)
    run(configure, cwd=mirror, environment=environment)

    jobs = arguments.jobs or recommended_jobs()
    print(f"Building with {jobs} parallel job(s).", flush=True)
    build = [
        "cmake", "--build", "--preset", arguments.preset,
        "--parallel", str(jobs),
    ]
    if arguments.target:
        build.extend(["--target", *arguments.target])
    run(build, cwd=mirror, environment=environment)
    if arguments.run_tests or arguments.include_system_tests:
        run_tests(arguments, mirror, environment)


def run_tests(arguments, mirror, environment):
    build_directory = mirror / f"build-{arguments.preset}-Linux"
    if arguments.include_system_tests:
        test_command = [
            "ctest", "--test-dir", str(build_directory), "--output-on-failure",
        ]
    else:
        test_command = ["ctest", "--preset", arguments.preset]
    if arguments.test_regex:
        test_command.extend(["-R", arguments.test_regex])
    run(test_command, cwd=mirror, environment=environment)


def main():
    arguments = parse_arguments()
    source = arguments.source.resolve(strict=True)
    if not (source / "CMakePresets.json").is_file():
        raise RuntimeError(f"Not a Panda source checkout: {source}")
    if arguments.jobs is not None and arguments.jobs < 1:
        raise RuntimeError("--jobs must be a positive integer")
    if (
        (arguments.run_tests or arguments.include_system_tests)
        and arguments.preset not in TEST_PRESETS
    ):
        raise RuntimeError(f"Preset {arguments.preset} does not enable tests")
    if arguments.target and (arguments.run_tests or arguments.include_system_tests):
        raise RuntimeError("--target cannot be combined with test execution")
    if arguments.sync_only and (
        arguments.target
        or arguments.run_tests
        or arguments.include_system_tests
        or arguments.test_regex
    ):
        raise RuntimeError("--sync-only cannot be combined with build or test options")
    if arguments.test_regex and not (arguments.run_tests or arguments.include_system_tests):
        raise RuntimeError("--test-regex requires --run-tests or --include-system-tests")

    mirror = Path.home().resolve() / "src" / MIRROR_NAME
    create_or_validate_mirror(source, mirror)
    synchronize(source, mirror)
    if not arguments.sync_only:
        run_build(arguments, mirror)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"WSL build failed: {error}", file=sys.stderr)
        raise SystemExit(getattr(error, "returncode", 1) or 1)
