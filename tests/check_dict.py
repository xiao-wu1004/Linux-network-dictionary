#!/usr/bin/env python3
"""Validate and optionally normalize the dictionary file."""

from __future__ import annotations

import argparse
import sys
import re
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path

ENTRY_PATTERN = re.compile(r"^(?P<head>\S.*?)(?:\s{2,}|\t+)(?P<definition>\S.*)$")


@dataclass(frozen=True)
class Entry:
    line_number: int
    word: str
    definition: str


def parse_entries(path: Path) -> tuple[list[Entry], list[tuple[int, str]]]:
    entries: list[Entry] = []
    format_errors: list[tuple[int, str]] = []

    for line_number, raw_line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not raw_line.strip():
            continue

        match = ENTRY_PATTERN.match(raw_line)
        if match is None:
            format_errors.append((line_number, raw_line))
            continue

        entries.append(
            Entry(
                line_number=line_number,
                word=match.group("head").strip(),
                definition=match.group("definition").strip(),
            )
        )

    return entries, format_errors


def sort_key(word: str) -> tuple[str, str, str]:
    collapsed = "".join(ch.casefold() for ch in word if ch.isalnum())
    return collapsed, word.casefold(), word


def find_duplicates(entries: list[Entry]) -> dict[str, list[Entry]]:
    entries_by_word: dict[str, list[Entry]] = defaultdict(list)

    for entry in entries:
        entries_by_word[entry.word].append(entry)

    return {
        word: items
        for word, items in sorted(entries_by_word.items())
        if len(items) > 1
    }


def find_order_issues(entries: list[Entry]) -> list[tuple[Entry, Entry]]:
    issues: list[tuple[Entry, Entry]] = []

    for previous, current in zip(entries, entries[1:]):
        if sort_key(previous.word) > sort_key(current.word):
            issues.append((previous, current))

    return issues


def normalize_entries(entries: list[Entry]) -> list[Entry]:
    merged: dict[str, list[str]] = defaultdict(list)

    for entry in entries:
        if entry.definition not in merged[entry.word]:
            merged[entry.word].append(entry.definition)

    normalized_entries = [
        Entry(
            line_number=0,
            word=word,
            definition="；".join(definitions),
        )
        for word, definitions in merged.items()
    ]

    normalized_entries.sort(key=lambda entry: sort_key(entry.word))
    return normalized_entries


def write_entries(path: Path, entries: list[Entry]) -> None:
    content = "".join(f"{entry.word}   {entry.definition}\n" for entry in entries)
    path.write_text(content, encoding="utf-8", newline="\n")


def print_summary(
    path: Path,
    entries: list[Entry],
    format_errors: list[tuple[int, str]],
    duplicates: dict[str, list[Entry]],
    order_issues: list[tuple[Entry, Entry]],
) -> None:
    print(f"Checked {path}")
    print(f"Entries: {len(entries)}")
    print(f"Format errors: {len(format_errors)}")
    print(f"Duplicate words: {len(duplicates)}")
    print(f"Ordering issues: {len(order_issues)}")

    if format_errors:
        print("\nFormat errors:")
        for line_number, raw_line in format_errors:
            print(f"  line {line_number}: {raw_line!r}")

    if duplicates:
        print("\nDuplicate words:")
        for word, items in duplicates.items():
            details = ", ".join(f"line {entry.line_number}" for entry in items)
            print(f"  {word}: {details}")

    if order_issues:
        print("\nOrdering issues:")
        for previous, current in order_issues:
            print(
                "  "
                f"line {previous.line_number} '{previous.word}' "
                f"should not come before line {current.line_number} '{current.word}'"
            )


def check_dictionary(
    path: Path,
) -> tuple[list[Entry], list[tuple[int, str]], dict[str, list[Entry]], list[tuple[Entry, Entry]]]:
    entries, format_errors = parse_entries(path)
    duplicates = find_duplicates(entries)
    order_issues = find_order_issues(entries)
    return entries, format_errors, duplicates, order_issues


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check dictionary ordering, duplicates, and line format."
    )
    parser.add_argument(
        "path",
        nargs="?",
        default="dict.txt",
        help="path to the dictionary file (default: dict.txt)",
    )
    parser.add_argument(
        "--fix",
        action="store_true",
        help="rewrite the dictionary with merged duplicates and sorted entries",
    )
    args = parser.parse_args()

    path = Path(args.path)
    if not path.is_file():
        print(f"Dictionary file not found: {path}", file=sys.stderr)
        return 2

    entries, format_errors, duplicates, order_issues = check_dictionary(path)
    print_summary(path, entries, format_errors, duplicates, order_issues)

    if args.fix:
        if format_errors:
            print("\nRefusing to auto-fix because format errors must be resolved first.", file=sys.stderr)
            return 1

        normalized_entries = normalize_entries(entries)
        write_entries(path, normalized_entries)
        print("\nDictionary rewritten with normalized ordering and merged duplicates.")

        entries, format_errors, duplicates, order_issues = check_dictionary(path)
        print()
        print_summary(path, entries, format_errors, duplicates, order_issues)

    if format_errors or duplicates or order_issues:
        return 1

    print("\nDictionary check passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
