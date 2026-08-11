#!/usr/bin/env python3
"""Read and roll the Unreleased section of CHANGELOG.md.

Two subcommands, both used by .github/workflows/release.yml:

  extract            print the Unreleased body (the Highlights source)
  roll <version>     rewrite Unreleased as a dated section for <version>
                     and leave a fresh empty Unreleased behind

`extract` exits non-zero when the section is missing or empty, which is
what stops a release from being cut with no release notes.
"""

import argparse
import datetime
import re
import sys
from pathlib import Path

DEFAULT_PATH = Path(__file__).resolve().parent.parent / "CHANGELOG.md"

UNRELEASED_RE = re.compile(
    r"^## \[Unreleased\][^\n]*\n(?P<body>.*?)(?=^## |\Z)",
    re.MULTILINE | re.DOTALL,
)


def read_unreleased(text: str) -> str:
    """Return the Unreleased body, or raise ValueError if absent/empty."""
    match = UNRELEASED_RE.search(text)
    if match is None:
        raise ValueError(
            "CHANGELOG.md has no '## [Unreleased]' section. The release "
            "workflow needs one to build the Highlights section."
        )
    body = match.group("body").strip()
    if not body:
        raise ValueError(
            "The '## [Unreleased]' section of CHANGELOG.md is empty. Add "
            "entries describing this release before running the Release "
            "workflow."
        )
    return body


def roll(text: str, version: str, today: str) -> str:
    """Turn Unreleased into a dated `version` section, leaving a new one."""
    body = read_unreleased(text)
    replacement = (
        f"## [Unreleased]\n\n"
        f"## [{version}] - {today}\n\n"
        f"{body}\n\n"
    )
    return UNRELEASED_RE.sub(lambda _: replacement, text, count=1)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--path", type=Path, default=DEFAULT_PATH, help="path to CHANGELOG.md"
    )
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("extract", help="print the Unreleased body")
    roll_parser = sub.add_parser("roll", help="stamp Unreleased with a version")
    roll_parser.add_argument("version", help="e.g. v3.8.0")
    roll_parser.add_argument(
        "--date", default=datetime.date.today().isoformat(), help="YYYY-MM-DD"
    )

    args = parser.parse_args()
    text = args.path.read_text()

    try:
        if args.command == "extract":
            print(read_unreleased(text))
        else:
            version = args.version.lstrip("v")
            args.path.write_text(roll(text, version, args.date))
            print(f"CHANGELOG.md: rolled Unreleased into {version} ({args.date})")
    except ValueError as err:
        print(f"error: {err}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
