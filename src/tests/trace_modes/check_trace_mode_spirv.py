"""Inspect entry models and instructions in one isolated Horizon shader cache."""
import struct
import sys
from pathlib import Path

# Core SPIR-V and KHR ray tracing/query opcodes.
EXTENSION, ENTRY, CAPABILITY, TRACE = 10, 15, 17, 4445
QUERY_OPS = {*range(4472, 4480), 5340, *range(6016, 6033)}
MODELS = {"compute": 5, "raygen": 5313, "closesthit": 5316, "miss": 5317}


def literal_string(data, first_word, end_word):
    literal = data[first_word * 4:end_word * 4]
    return literal[:literal.index(0)].decode()


def inspect(path):
    data = path.read_bytes()
    words = struct.unpack(f"<{len(data) // 4}I", data)
    if words[0] != 0x07230203:
        raise ValueError(f"invalid SPIR-V header in {path}")
    ops, caps, extensions, entries = set(), set(), set(), []
    at = 5
    while at < len(words):
        size, opcode = divmod(words[at], 65536)
        if not size or at + size > len(words):
            raise ValueError(f"invalid SPIR-V instruction in {path}")
        ops.add(opcode)
        if opcode == EXTENSION:
            extensions.add(literal_string(data, at + 1, at + size))
        if opcode == ENTRY:
            name = literal_string(data, at + 3, at + size)
            entries.append((words[at + 1], name))
        if opcode == CAPABILITY:
            caps.add(words[at + 1])
        at += size
    return entries, caps, extensions, ops


def check(mode, cache):
    programs = {}
    for path in (cache / "slang_cache").glob("*.spv"):
        program, stage, entry = path.stem.rsplit("_", 2)
        entries, caps, extensions, ops = inspect(path)
        if entries != [(MODELS[stage], entry)]:
            raise ValueError(f"{path.name}: unexpected entry points {entries}")
        if f"_{mode}_" in program:
            programs.setdefault(program, set()).add(stage)
            if mode == "query":
                if ("SPV_KHR_ray_tracing" in extensions or not {4473, 4477} <= ops or
                        4472 not in caps or 4479 in caps or TRACE in ops):
                    raise ValueError(f"invalid ray query compute module {path.name}")
            elif ("SPV_KHR_ray_query" in extensions or 4472 in caps or ops & QUERY_OPS or 4479 not in caps or
                  (TRACE in ops) != (stage == "raygen")):
                raise ValueError(f"invalid native module {path.name}")
        elif "_compute_" not in program or stage != "compute" or caps & {4472, 4479} or ops & (QUERY_OPS | {TRACE}):
            raise ValueError(f"unexpected tracing module {path.name}")
    expected = {"compute"} if mode == "query" else {"raygen", "miss", "closesthit"}
    if not programs or any(stages != expected for stages in programs.values()):
        raise ValueError(f"expected {mode} programs with stages {sorted(expected)}, found {programs}")
    print(f"Verified {len(programs)} {mode} program(s) and their SPIR-V entry points and instructions")


if __name__ == "__main__":
    check(sys.argv[1], Path(sys.argv[2]))
