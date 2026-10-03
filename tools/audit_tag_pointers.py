#!/usr/bin/env python3
"""Type-aware audit: every MemberExpr accessing .address/.name/.definition
where the base expression's static type is struct tag_block / tag_data /
tag_reference (by substring on the immediate child's qualType, which is
reliable because clang always shows the concrete record type there, even
through an ImplicitCastExpr or AddrOf).

Walks the real compile_commands.json (ninja -t compdb linux_cc) so every
file is parsed with its actual defines/include paths, not guessed flags.
"""
import json
import re
import shlex
import subprocess
import sys
from pathlib import Path

ROOT = Path("/home/proot-dev/switch/halo-ce-nx")
COMPDB = Path("/tmp/compdb_linux.json")
TARGET_TYPES = ("struct tag_block", "struct tag_data", "struct tag_reference")
TARGET_FIELDS = ("address", "name", "definition", "name_length", "index")
# these are the accessors themselves / the relocator (expected, not findings)
KNOWN_OK_FILES = {
    "source/tag_files/tag_groups.c",
    "port/linux/src/tag_relocate.c",
}


def load_compdb():
    db = json.loads(COMPDB.read_text())
    by_file = {}
    for entry in db:
        f = entry["file"]
        by_file[f] = entry["command"]
    return by_file


def strip_output_flags(command: str) -> list:
    parts = shlex.split(command)
    out = []
    skip_next = False
    for i, p in enumerate(parts):
        if skip_next:
            skip_next = False
            continue
        if p in ("-MF", "-o"):
            skip_next = True
            continue
        if p.startswith("-MMD") or p.startswith("-MD"):
            continue
        if p == "-march=native":
            # this proot host is aarch64; clang can't resolve "native" for
            # an i686 target here. Any baseline x86 march is fine - we only
            # need the AST, never codegen.
            out.append("-march=pentium3")
            continue
        out.append(p)
    return out


def base_type_of(inner_child):
    """climbs through ImplicitCastExpr/ParenExpr/UnaryOperator to find the
    nearest node carrying a concrete qualType"""
    node = inner_child
    for _ in range(6):
        if not isinstance(node, dict):
            return None
        qt = node.get("type", {}).get("qualType", "")
        if qt:
            return qt
        children = node.get("inner")
        if not children:
            return None
        node = children[0]
    return None


def offset_to_line(text_bytes, offset):
    return text_bytes.count(b"\n", 0, offset) + 1


def scan_file(relpath, command, findings):
    args = strip_output_flags(command)
    # replace the actual -c compile with ast-dump
    args = [a for a in args if a not in ("-c",)]
    args += ["-Xclang", "-ast-dump=json", "-fsyntax-only"]
    try:
        proc = subprocess.run(args, cwd=ROOT, capture_output=True, timeout=90)
    except subprocess.TimeoutExpired:
        print(f"TIMEOUT: {relpath}", file=sys.stderr)
        return
    if not proc.stdout:
        print(f"NO OUTPUT ({proc.returncode}): {relpath}: {proc.stderr[-300:]}", file=sys.stderr)
        return
    try:
        tree = json.loads(proc.stdout)
    except json.JSONDecodeError as e:
        print(f"BAD JSON: {relpath}: {e}", file=sys.stderr)
        return

    src_bytes = (ROOT / relpath).read_bytes()

    def walk(node):
        if not isinstance(node, dict):
            return
        if node.get("kind") == "MemberExpr" and node.get("name") in TARGET_FIELDS:
            inner = node.get("inner") or []
            base_type = base_type_of(inner[0]) if inner else None
            if base_type and any(t in base_type for t in TARGET_TYPES):
                rng = node.get("range", {}).get("begin", {})
                offset = rng.get("offset")
                if offset is None:
                    offset = rng.get("expansionLoc", {}).get("offset")
                line = offset_to_line(src_bytes, offset) if offset is not None else "?"
                findings.append((relpath, line, node.get("name"), base_type, node.get("isArrow")))
        for v in node.values():
            if isinstance(v, list):
                for c in v:
                    walk(c)
            elif isinstance(v, dict):
                walk(v)

    for decl in tree.get("inner", []):
        walk(decl)


def main():
    by_file = load_compdb()
    # shortlist: only files whose source mentions these types at all
    candidates = []
    for relpath in by_file:
        full = ROOT / relpath
        try:
            text = full.read_text(errors="ignore")
        except Exception:
            continue
        if any(t.split()[-1] in text for t in TARGET_TYPES):
            candidates.append(relpath)
    print(f"{len(by_file)} compiled files, {len(candidates)} mention a target type", file=sys.stderr)

    findings = []
    for i, relpath in enumerate(sorted(candidates)):
        print(f"[{i+1}/{len(candidates)}] {relpath}", file=sys.stderr)
        scan_file(relpath, by_file[relpath], findings)

    print(f"\n{len(findings)} direct field accesses found\n")
    flagged = [f for f in findings if f[0] not in KNOWN_OK_FILES]
    ok = [f for f in findings if f[0] in KNOWN_OK_FILES]
    print(f"{len(ok)} inside the known accessor/relocator files (expected)")
    print(f"{len(flagged)} OUTSIDE them (need review)\n")
    by_file_out = {}
    for relpath, line, name, base_type, is_arrow in flagged:
        by_file_out.setdefault(relpath, []).append((line, name, base_type, is_arrow))
    for relpath in sorted(by_file_out):
        print(f"{relpath}:")
        for line, name, base_type, is_arrow in sorted(by_file_out[relpath]):
            op = "->" if is_arrow else "."
            print(f"  :{line}  {op}{name}   (base: {base_type})")

    out_path = Path(__file__).resolve().parent / "tag_pointer_audit.json"
    out_path.write_text(json.dumps(findings, indent=1))
    print(f"\nraw findings written to {out_path}", file=sys.stderr)


if __name__ == "__main__":
    main()
