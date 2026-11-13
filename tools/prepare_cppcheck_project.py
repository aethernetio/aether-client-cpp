#!/usr/bin/env python3
"""Expose SYSTEM dependencies to cppcheck's built-in preprocessor."""

import argparse
import json
from pathlib import Path
import shlex


def prepare(entries):
    for entry in entries:
        arguments = entry.get("arguments")
        if arguments is None:
            arguments = shlex.split(entry["command"])
        # cppcheck 2.21 imports -isystem separately, but only its Clang mode
        # consumes those paths. Change the analysis copy, not compiler flags.
        entry["arguments"] = [
            "-I" + argument[len("-isystem") :]
            if argument.startswith("-isystem")
            else argument
            for argument in arguments
        ]
        entry.pop("command", None)
    return entries


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    entries = json.loads(args.source.read_text(encoding="utf-8"))
    args.destination.write_text(
        json.dumps(prepare(entries), indent=2) + "\n", encoding="utf-8"
    )


if __name__ == "__main__":
    main()
