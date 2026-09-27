"""Check maintained Markdown links and the Panda task queue, using only the stdlib.

Supports the repository's inline Markdown links and ATX headings, not all CommonMark.
Does not fetch URLs, read imported legacy source, or infer semantic correctness.
"""

from collections import Counter
from pathlib import Path
import re
import sys
from urllib.parse import unquote, urlsplit


LINK = re.compile(r"\[[^\]\n]*\]\(([^)\n]+)\)")
STATES = {"Planned", "Ready", "In progress", "Blocked", "Review", "Accepted"}


def prose(text: str) -> str:
    """Remove fenced examples, which may contain deliberately non-working links."""
    result = []
    fence = None
    for line in text.splitlines():
        marker = re.match(r"^\s*(`{3,}|~{3,})(.*)$", line)
        if marker:
            token, suffix = marker.groups()
            if fence is None:
                fence = token
            elif token[0] == fence[0] and len(token) >= len(fence) and not suffix.strip():
                fence = None
            continue
        if fence is None:
            result.append(line)
    return "\n".join(result)


def anchors(text: str) -> set[str]:
    counts = Counter()
    result = set()
    for heading in re.findall(r"^#{1,6}\s+(.+?)\s*#*\s*$", prose(text), re.MULTILINE):
        slug = re.sub(r"[^\w\-\s]", "", heading.lower()).replace(" ", "-")
        result.add(f"{slug}-{counts[slug]}" if counts[slug] else slug)
        counts[slug] += 1
    return result


def check_links(path: Path, root: Path) -> list[str]:
    errors = []
    for match in LINK.finditer(prose(path.read_text(encoding="utf-8"))):
        raw = match.group(1).strip()
        if raw.startswith("<"):
            if ">" not in raw:
                errors.append(f"{path.relative_to(root)}: malformed link: {raw}")
                continue
            destination = raw[1:raw.index(">")]
        else:
            destination = raw.split()[0] if raw else ""
        url = urlsplit(destination)
        if url.scheme or url.netloc:
            continue
        target = (path.parent / unquote(url.path)).resolve() if url.path else path.resolve()
        if not target.is_relative_to(root.resolve()):
            errors.append(f"{path.relative_to(root)}: link escapes repository: {destination}")
        elif not target.exists():
            errors.append(f"{path.relative_to(root)}: missing target: {destination}")
        elif url.fragment and target.suffix.lower() == ".md":
            if unquote(url.fragment) not in anchors(target.read_text(encoding="utf-8")):
                errors.append(f"{path.relative_to(root)}: missing heading: {destination}")
    return errors


def check_queue(root: Path) -> list[str]:
    plan = root / "docs/development/PLAN.md"
    cards = (root / "docs/development/tasks").resolve()
    errors = []
    seen = {}
    for line in plan.read_text(encoding="utf-8").splitlines():
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        if len(cells) != 4 or not cells[0].startswith("["):
            continue
        task, phase, state, dependencies = cells
        links = LINK.findall(task)
        if len(links) != 1:
            errors.append(f"Task queue: {task} needs one card link")
            continue
        card = (plan.parent / links[0]).resolve()
        if card.parent != cards or card.suffix != ".md":
            errors.append(f"Task queue: unexpected card path: {links[0]}")
            continue
        if card in seen:
            errors.append(f"Task queue: duplicate card {card.name}")
        if state not in STATES:
            errors.append(f"Task queue: unknown state {state} for {card.name}")
        if not phase:
            errors.append(f"Task queue: missing phase for {card.name}")
        for link in LINK.findall(dependencies):
            dependency = (plan.parent / link).resolve()
            if dependency not in seen:
                errors.append(f"Task queue: {card.name} prerequisite must precede it: {link}")
            elif state in {"Ready", "In progress", "Review", "Accepted"} and seen[dependency] != "Accepted":
                errors.append(f"Task queue: {card.name} prerequisite is not accepted: {link}")
        seen[card] = state
    if not seen:
        errors.append("Task queue: no task rows found")
    for card in cards.glob("*.md"):
        if card.resolve() not in seen:
            errors.append(f"Task card missing from queue: {card.relative_to(root)}")
    return errors


def documents(root: Path) -> list[Path]:
    paths = [root / "README.md", root / "AGENTS.md"]
    for directory in ("docs", ".agents/skills", ".github"):
        paths.extend((root / directory).rglob("*.md"))
    paths.extend(root / name for name in (
        "legacy/README.md", "legacy/panda/README.md", "legacy/drip-engine/README.md"
    ))
    return sorted(paths)


def main() -> int:
    root = Path(__file__).resolve().parents[4]
    paths = documents(root)
    errors = []
    for path in paths:
        if path.is_file():
            errors.extend(check_links(path, root))
        else:
            errors.append(f"Missing documentation entry point: {path.relative_to(root)}")
    errors.extend(check_queue(root))
    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        return 1
    print(f"Checked {len(paths)} Markdown files and the task queue; no structural errors.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
