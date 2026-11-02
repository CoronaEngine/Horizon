"""Restore the configured toolchain and normalize MSVC dependency lines for Ninja."""

import json
import locale
import os
from pathlib import Path
import subprocess
import sys


def dependency_prefixes(raw_prefix, ansi_encoding):
    if not raw_prefix:
        return []
    prefixes = []
    for source_encoding in ("utf-8", ansi_encoding):
        try:
            text = raw_prefix.decode(source_encoding)
        except UnicodeError:
            continue
        for output_encoding in ("utf-8", ansi_encoding):
            try:
                entry = (text.encode(output_encoding), output_encoding)
            except UnicodeError:
                continue
            if entry not in prefixes:
                prefixes.append(entry)
    return prefixes


def normalize_line(line, prefixes):
    for prefix, encoding in prefixes:
        if line.startswith(prefix):
            try:
                path = line[len(prefix):].lstrip().decode(encoding)
            except UnicodeDecodeError:
                continue
            return b"Note: including file: " + path.encode("utf-8")
    return line


def main():
    environment = dict(os.environ)
    environment.update(json.loads(Path(sys.argv[1]).read_text(encoding="utf-8")))
    prefixes = dependency_prefixes(bytes.fromhex(sys.argv[2]), locale.getencoding())
    with subprocess.Popen(sys.argv[3:], env=environment,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT) as process:
        for line in process.stdout:
            sys.stdout.buffer.write(normalize_line(line, prefixes))
            sys.stdout.buffer.flush()
        return process.wait()


if __name__ == "__main__":
    sys.exit(main())
