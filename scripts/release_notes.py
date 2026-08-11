#!/usr/bin/env python3
"""Assemble GitHub release notes: Highlights + auto-generated commit list.

The Highlights section comes from the Unreleased section of CHANGELOG.md
(human-written, review-able in a PR). The commit list comes from
`gh release create --generate-notes`, which the workflow captures and
passes in via --generated. Everything is markdown.
"""

import argparse
import re
import sys
from pathlib import Path


def build(
    version: str,
    highlights: str,
    generated: str,
    repo: str,
    previous: str | None,
) -> str:
    parts = [f"## Highlights\n\n{highlights.strip()}\n"]

    generated = generated.strip()
    if generated:
        # `--generate-notes` emits its own "## What's Changed" heading plus a
        # trailing "**Full Changelog**: ..." link. Keep both; they sit fine
        # below Highlights.
        parts.append(f"\n{generated}\n")
    elif previous:
        parts.append(
            f"\n**Full Changelog**: "
            f"https://github.com/{repo}/compare/{previous}...{version}\n"
        )

    parts.append(
        f"\n---\n\n"
        f"### Upgrading\n\n"
        f"Point your ESPHome configuration at this release and rebuild:\n\n"
        f"```yaml\n"
        f"external_components:\n"
        f"  - source:\n"
        f"      type: git\n"
        f"      url: https://github.com/{repo}\n"
        f"      ref: {version}\n"
        f"    components: [ patch_acl_reassembly, subzero_protocol, subzero_appliance ]\n"
        f"```\n\n"
        f"See the [full changelog]"
        f"(https://github.com/{repo}/blob/{version}/CHANGELOG.md) for every "
        f"change in this release.\n"
    )
    return "".join(parts)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True, help="e.g. v3.8.0")
    parser.add_argument(
        "--highlights", required=True, type=Path, help="file with Highlights markdown"
    )
    parser.add_argument(
        "--generated",
        type=Path,
        help="file with `gh --generate-notes` output (optional)",
    )
    parser.add_argument("--repo", required=True, help="owner/name")
    parser.add_argument("--previous", help="previous tag, for the compare link")
    parser.add_argument("--out", required=True, type=Path, help="output file")

    args = parser.parse_args()

    highlights = args.highlights.read_text()
    if not highlights.strip():
        print("error: highlights file is empty", file=sys.stderr)
        return 1

    generated = args.generated.read_text() if args.generated and args.generated.exists() else ""

    notes = build(args.version, highlights, generated, args.repo, args.previous)
    args.out.write_text(notes)
    print(notes)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
