#!/usr/bin/env python3
"""The one script behind the pixi tasks: configure / build / test / run / debug.

WHY A SCRIPT RATHER THAN FIVE TASK STRINGS. Two things every task needs and no
task string can express portably. First, WHERE the engine is: ``AETHER_DIR``
else the sibling checkout -- the same two-step ``CMakeLists.txt`` does, and it
has to agree with it or a configure and a build would disagree about which
engine they are for. Second, that ``configure``/``build``/``test`` must run
INSIDE THE ENGINE'S pixi environment, because that is where conan (bgfx from
source), the pinned cmake/ninja and the conda prefix ``find_package`` searches
all live.

This repo's own environment holds python and nothing else, deliberately: a
second set of toolchain pins is a second solve, and two solves drift. A games
tree compiled by a different toolchain than the ``aether`` it links is the
ADR-0171 crash from the other direction, and that one took a day to find.

``run`` and ``debug`` do NOT go through the engine environment, because they do
not need it: the binaries carry an absolute RPATH into the engine's prefix, and
their asset and shader roots are baked in as absolute paths at configure time
(``AETHER_ASSET_DIR``/``AETHER_SHADER_DIR``), so they run from any directory
with nothing activated.

Nothing here builds before running: the pixi tasks declare ``depends-on =
["build"]``, so pixi does it and a failed build stops the run rather than
letting a stale binary be read as evidence -- the engine's ``tools/go.py``
lesson, spent once already.
"""

from __future__ import annotations

import os
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
# Our OWN `current`, in our own `_build`: configure.py writes the symlink under
# whatever `--source` it was given, so this repo and the engine never share a
# tree or a `current` (see its --source comment).
BUILD = ROOT / "_build" / "current"
GAMES = ("coin_rush", "lantern", "tideworn", "hearthfield")


def engine() -> Path:
    """The aether checkout, resolved exactly as CMakeLists.txt resolves it."""
    named = os.environ.get("AETHER_DIR") or str(ROOT.parent / "aether")
    path = Path(named).expanduser().resolve()
    if not (path / "pixi.toml").is_file():
        sys.exit(
            f"no aether engine at {path}. Clone it beside this repo, or set "
            "AETHER_DIR=<path> in the environment.")
    return path


def shell_arg(arg: str) -> str:
    """One argument for pixi's shell — quoted on POSIX, bare on Windows.

    pixi's Windows shell strips NEITHER single nor double quotes, so a quoted
    path arrives with the quotes in it and every task here died on `can't open
    file "'D:\\...\\configure.py'"`. It does preserve argv boundaries, so a bare
    path with a space survives — measured, both ways, before this changed.
    """
    return arg if os.name == "nt" else shlex.quote(arg)


def in_engine_env(cmd: list[str]) -> int:
    """Run `cmd` in the engine's pixi environment, pointed at OUR build tree.

    AETHER_BUILD_DIR is what makes the engine's build.py build this repo:
    without it, it follows the ENGINE's `_build/current` and would quietly
    build the engine's own tree instead of ours.
    """
    if not shutil.which("pixi"):
        sys.exit("pixi is not on PATH — https://pixi.sh, then re-run.")
    env = dict(os.environ)
    env["AETHER_BUILD_DIR"] = str(BUILD)
    full = ["pixi", "run", "--manifest-path", str(engine() / "pixi.toml"),
            "-e", "default", *(shell_arg(c) for c in cmd)]
    print("+", " ".join(full), flush=True)
    return subprocess.run(full, env=env).returncode


def require_configured() -> None:
    if not (BUILD / "CMakeCache.txt").is_file():
        sys.exit(f"{BUILD} is not configured — run `pixi run configure` first")


def configure(args: list[str]) -> int:
    """Configure this repo's tree; the engine builds inside it as a subdir.

    No `--keep-current`: the symlink this moves is OURS (configure.py writes it
    under `--source`), and every task below follows it. The recipe in CLAUDE.md
    passed the flag and then had to name the build directory by hand.
    """
    return in_engine_env(
        ["python", str(engine() / "tools" / "configure.py"), *args,
         "--source", str(ROOT)])


def build(args: list[str]) -> int:
    require_configured()
    return in_engine_env(
        ["python", str(engine() / "tools" / "build.py"), *args])


def ctest(args: list[str]) -> int:
    require_configured()
    return in_engine_env(["ctest", "--test-dir", str(BUILD), "-j",
                          str(os.cpu_count() or 4), "--output-on-failure",
                          *args])


def test(args: list[str]) -> int:
    """The WHOLE suite — the engine's ~1500 cases plus the games' own.

    That is what this build tree contains, and running the engine's suite here
    is not waste: these games are the only consumer of the engine that this
    checkout combination proves compiles and links.

    `-j` where the engine's own task runs serially, because 29s (measured
    2026-09-04) versus minutes decides whether the suite gets run at all. The
    35 `ui` cases share a GPU under it; if that ever turns flaky, drop the -j
    rather than the test.
    """
    return ctest(args)


def test_headless(args: list[str]) -> int:
    """The `headless` set: everything that needs no display — 1544 of 1579.

    Worth offering HERE and not only in the engine: until 2026-09-04 three of
    the four game suites carried no label, so this set covered one game of four
    and a games checkout had no honest way to ask for it. Each game's
    tests/label_tests.cmake is what makes the question answerable.
    """
    return ctest(["-L", "headless", *args])


def test_game(args: list[str]) -> int:
    """One game's tests only — its doctest cases AND its seam/layout tests.

    Selected by build SUBDIRECTORY, because a label cannot name a game: the
    labels split cases by what they NEED (a display or not), and all four games
    are headless. `-L headless` is the whole-repo fast set above, not a
    per-game selector.
    """
    if not args or args[0] not in GAMES:
        sys.exit(f"usage: pixi run test-game <{'|'.join(GAMES)}>")
    require_configured()
    return in_engine_env(["ctest", "--test-dir", str(BUILD / args[0]),
                          "--output-on-failure", *args[1:]])


def find_binary(name: str) -> Path:
    """An executable under the build tree, by bare name (the engine's `go`)."""
    require_configured()
    given = Path(name)
    if given.is_file():
        return given
    wanted = {given.name, f"{given.name}.exe"}
    root = BUILD / "bin"
    found = sorted(p for p in root.rglob("*")
                   if p.is_file() and p.name in wanted
                   and (os.name == "nt" or os.access(p, os.X_OK)))
    if not found:
        sys.exit(f"✗ no executable named '{name}' under {root}\n"
                 f"  the games are: {', '.join(GAMES)}")
    if len(found) > 1:
        listed = "\n   ".join(str(p) for p in found)
        sys.exit(f"✗ ambiguous name '{name}':\n   {listed}")
    return found[0]


def run(args: list[str]) -> int:
    """Launch a game IN THE ENGINE'S ENVIRONMENT, which Windows needs.

    A game links freetype and the rest out of the engine's conda prefix. On
    Linux CMake bakes an absolute RPATH into that prefix, which is what makes
    the repo's "they run from anywhere with nothing activated" true. WINDOWS
    HAS NO RPATH: launched bare, every game here died with STATUS_DLL_NOT_FOUND
    (0xC0000135) and nothing said which library. So the launcher supplies the
    environment instead of relying on a mechanism that is Linux-only.
    """
    if not args:
        sys.exit(f"usage: pixi run run <{'|'.join(GAMES)}|binary> [args...]")
    path = find_binary(args[0])
    return in_engine_env([str(path), *args[1:]])


def debug(args: list[str]) -> int:
    """Under gdb, from PATH — nothing in this repo's environment supplies one.

    Stated rather than hidden: on Windows there is no gdb, so `pixi run debug`
    is a Linux task. AETHER_DEBUGGER overrides it (lldb takes the same
    `--args`).
    """
    if not args:
        sys.exit(f"usage: pixi run debug <{'|'.join(GAMES)}|binary> [args...]")
    dbg = os.environ.get("AETHER_DEBUGGER", "gdb")
    if not shutil.which(dbg):
        sys.exit(f"'{dbg}' is not on PATH — install it, or set "
                 "AETHER_DEBUGGER to a debugger that takes `--args`.")
    path = find_binary(args[0])
    cmd = [dbg, "--args", str(path), *args[1:]]
    print("+", " ".join(cmd), flush=True)
    return subprocess.run(cmd).returncode


def captures_check(args: list[str]) -> int:
    """The games' capture oracles and cost budgets, through the ENGINE'S checker.

    `--root` rather than a copy of check_captures.py, on purpose: a second copy
    drifts, and the engine's is the one carrying the per-oracle `--rerecord`
    guard that stops a wrong digest being installed by hand. One checker, two
    repos, and the engine's own sixteen oracles exercise the same code path.
    """
    require_configured()
    return in_engine_env(
        ["python", str(engine() / "tools" / "check_captures.py"),
         "--root", str(ROOT), *args])


COMMANDS = {
    "configure": configure,
    "build": build,
    "test": test,
    "test-headless": test_headless,
    "test-game": test_game,
    "run": run,
    "debug": debug,
    "captures-check": captures_check,
}


def main(argv: list[str]) -> int:
    if not argv or argv[0] not in COMMANDS:
        sys.exit(f"usage: dev.py <{'|'.join(COMMANDS)}> [args...]")
    return COMMANDS[argv[0]](argv[1:])


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
