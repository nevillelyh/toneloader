#!/usr/bin/env python3

import pathlib
import re
import subprocess


root = pathlib.Path(__file__).resolve().parent.parent
cmake_path = root / "CMakeLists.txt"
notices_path = root / "THIRD_PARTY_NOTICES.md"
cmake = cmake_path.read_text()
notices = notices_path.read_text()
updates = []

pattern = re.compile(
    r"(FetchContent_Declare\((?P<name>\w+).*?"
    r"GIT_REPOSITORY\s+(?P<repository>\S+).*?"
    r"GIT_TAG\s+)(?P<revision>[0-9a-f]{40})",
    re.DOTALL,
)


def update(match):
    output = subprocess.run(
        ["git", "ls-remote", "--symref", match["repository"], "HEAD"],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.splitlines()
    branch = next(
        line.removeprefix("ref: refs/heads/").removesuffix("\tHEAD")
        for line in output
        if line.startswith("ref: refs/heads/") and line.endswith("\tHEAD")
    )
    revision = next(
        line.split("\t", 1)[0]
        for line in output
        if re.fullmatch(r"[0-9a-f]{40}\tHEAD", line)
    )
    updates.append((match["name"], branch, match["revision"], revision))
    return match[1] + revision


updated_cmake, count = pattern.subn(update, cmake)
if count == 0:
    raise SystemExit("No FetchContent Git commit pins found")

updated_notices = notices
for name, branch, old, new in updates:
    if old not in updated_notices:
        raise SystemExit(f"{name}: current revision is missing from THIRD_PARTY_NOTICES.md")
    updated_notices = updated_notices.replace(old, new)
    print(f"{name} ({branch}): {old} -> {new}")

if updated_cmake == cmake:
    print("FetchContent dependencies are already up to date")
    raise SystemExit

cmake_path.write_text(updated_cmake)
notices_path.write_text(updated_notices)
print("Review transitive dependency revisions and licenses in THIRD_PARTY_NOTICES.md")
subprocess.run(["make", "test", "validate"], cwd=root, check=True)
