#!/usr/bin/env python3
"""
Scaffold an AI-first project state document system.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path


ROOT_AGENTS_TEMPLATE = "root-AGENTS.template.md"
TEMPLATE_FILES = {
    "project-index.md": "project-index.template.md",
    "requirements.md": "requirements.template.md",
    "change-decisions.md": "change-decisions.template.md",
    "architecture-milestones.md": "architecture-milestones.template.md",
    "todo.md": "todo.template.md",
    "acceptance-report.md": "acceptance-report.template.md",
    "lessons-learned.md": "lessons-learned.template.md",
    "run-log.md": "run-log.template.md",
}


def skill_root() -> Path:
    return Path(__file__).resolve().parent.parent


def templates_root() -> Path:
    return skill_root() / "assets" / "templates"


def read_template(name: str) -> str:
    return (templates_root() / name).read_text(encoding="utf-8")


def write_if_missing(path: Path, text: str, force: bool) -> None:
    if path.exists() and not force:
        print(f"[SKIP] {path} already exists")
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    print(f"[OK] Wrote {path}")


def create_hard_link(target: Path, link_path: Path, force: bool) -> None:
    if link_path.exists():
        if force:
            if link_path.is_dir():
                raise SystemExit(f"Refusing to replace directory: {link_path}")
            link_path.unlink()
        else:
            print(f"[SKIP] {link_path} already exists")
            return
    try:
        os.link(target, link_path)
        print(f"[OK] Linked {link_path} -> {target}")
    except OSError as exc:
        print(f"[WARN] Could not create hard link: {exc}")
        link_path.write_text(target.read_text(encoding='utf-8'), encoding="utf-8")
        print(f"[OK] Wrote fallback copy to {link_path}")


def render_root_agents(state_dir_name: str) -> str:
    text = read_template(ROOT_AGENTS_TEMPLATE)
    return text.replace("[state-dir]", state_dir_name)


def main() -> int:
    parser = argparse.ArgumentParser(description="Initialize a project state document system.")
    parser.add_argument("project_root", type=Path, help="Project root to scaffold.")
    parser.add_argument(
        "--state-dir",
        default=".agent-os",
        help="Relative state directory under the project root.",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Overwrite existing managed files.",
    )
    args = parser.parse_args()

    project_root = args.project_root.resolve()
    state_dir = project_root / args.state_dir
    state_dir.mkdir(parents=True, exist_ok=True)

    agents_path = project_root / "AGENTS.md"
    claude_path = project_root / "CLAUDE.md"
    write_if_missing(agents_path, render_root_agents(args.state_dir), args.force)
    create_hard_link(agents_path, claude_path, args.force)

    for output_name, template_name in TEMPLATE_FILES.items():
        output_path = state_dir / output_name
        write_if_missing(output_path, read_template(template_name), args.force)

    print("[OK] Project state document system initialized")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
