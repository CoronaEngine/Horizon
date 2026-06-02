#!/usr/bin/env python3
"""
Allocate the next typed item ID for a project state document set.
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path

ID_PATTERN = re.compile(r"\b([A-Z]{2,4})-(\d{3,})\b")


def collect_max_ids(root: Path) -> dict[str, int]:
    max_ids: dict[str, int] = {}
    for path in root.rglob("*.md"):
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            text = path.read_text(encoding="utf-8", errors="replace")
        for prefix, number_text in ID_PATTERN.findall(text):
            number = int(number_text)
            current = max_ids.get(prefix, 0)
            if number > current:
                max_ids[prefix] = number
    return max_ids


def main() -> int:
    parser = argparse.ArgumentParser(description="Allocate the next project item ID.")
    parser.add_argument("prefix", help="Item prefix such as TD, MS, REQ, EV")
    parser.add_argument(
        "--root",
        type=Path,
        default=Path("."),
        help="Project root or state directory to scan.",
    )
    parser.add_argument(
        "--width",
        type=int,
        default=3,
        help="Zero-pad width for the numeric part.",
    )
    args = parser.parse_args()

    prefix = args.prefix.strip().upper()
    if not re.fullmatch(r"[A-Z]{2,4}", prefix):
        raise SystemExit("Prefix must be 2 to 4 uppercase letters.")

    max_ids = collect_max_ids(args.root)
    next_number = max_ids.get(prefix, 0) + 1
    print(f"{prefix}-{next_number:0{args.width}d}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
