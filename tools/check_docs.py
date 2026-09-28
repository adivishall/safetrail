#!/usr/bin/env python3
"""Fail if any relative link in the repository's Markdown points nowhere.

Documentation that cites files -- "asserted in tests/index/differential_test.cpp",
"see RESULTS.md" -- is only as good as those paths. Files get renamed; this
catches the citation that was left behind. Checks every tracked *.md for
relative links and inline `path/to/file.ext` citations of source files.

Usage: python3 tools/check_docs.py      (make validate runs it)
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LINK = re.compile(r'\]\(([^)#\s]+)(?:#[^)\s]*)?\)')
# `tests/foo/bar_test.cpp`, `src/x/y.cpp`, `include/safetrail/a/b.hpp`, `tools/z.py`
CITE = re.compile(r'`((?:src|tests|include|apps|tools|bench|data)/[A-Za-z0-9_./-]+\.(?:cpp|hpp|py|csv|svg|geojson|txt))`')


def main():
    os.chdir(ROOT)
    files = subprocess.check_output(["git", "ls-files", "*.md"]).decode().split()
    files += [f for f in subprocess.check_output(
        ["git", "ls-files", "--others", "--exclude-standard", "*.md"]).decode().split()]
    bad = []
    for md in sorted(set(files)):
        if not os.path.exists(md):
            continue
        text = open(md, encoding="utf-8").read()
        base = os.path.dirname(md)
        for target in LINK.findall(text):
            if re.match(r"^[a-z]+:", target):
                continue
            if not os.path.exists(os.path.normpath(os.path.join(base, target))):
                bad.append((md, target))
        # Archived course material and the two history logs record what was true
        # when they were written and may cite files that have since moved; only
        # the maintained docs are held to their inline citations.
        if "/course/" in md or os.path.basename(md) in ("WORKLOG.md", "DEFECT_LOG.md"):
            continue
        for path in CITE.findall(text):
            if not os.path.exists(path) and not os.path.exists(os.path.join(base, path)):
                bad.append((md, path))
    for md, target in bad:
        print("  broken: %s -> %s" % (md, target))
    print("docs: %d broken reference(s) across %d files" % (len(bad), len(set(files))))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
