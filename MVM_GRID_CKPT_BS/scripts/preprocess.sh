#!/usr/bin/env bash

if [ $# -ne 1 ]; then
  echo "Usage: $0 input.c" >&2
  exit 2
fi

python3 - "$1" <<'PY' || exit 1
import re
import sys

INSTR = "INSTRUMENT;\n"
INDENT_STEP = "    "

IF_FOR_WHILE_RE = re.compile(r"^\s*(if|for|while)\b")
ELSE_RE = re.compile(r"^\s*}?\s*else\b\s*(\{)?")
CASE_RE = re.compile(r"^\s*case\b\s+[^:]+:")
COMMENT_RE = re.compile(r"^\s*//.*")
PTR_OPS_RE = re.compile(r".*=.*(?:&|->).*;\s*$")


def instrument_c_code(filename):
    with open(filename) as f:
        lines = f.readlines()

    out = []
    macro_inserted = False
    i = 0
    n = len(lines)

    def insert(indent):
        nonlocal macro_inserted
        if macro_inserted == False:
            out.append(indent + INSTR)
            macro_inserted = True

    while i < n:
        line = lines[i]
        stripped = line.strip()
        indent = re.match(r"\s*", line).group()

        # ---------- SINGLE LINE COMMENT ----------
        if COMMENT_RE.match(line):
            out.append(line)
            i += 1
            continue

        if "}" in stripped:
            macro_inserted = False

        # ---------- IF / FOR / WHILE ----------
        if IF_FOR_WHILE_RE.match(stripped):
            if "{" in stripped:
                out.append(line)
                macro_inserted = False
                insert(indent + INDENT_STEP)
            else:
                out.append(line.rstrip() + " {")

                insert(indent + INDENT_STEP)
                i += 1
                out.append(indent + INDENT_STEP + lines[i].lstrip())
                out.append(indent + "}")

            i += 1
            continue

        # ---------- ELSE ----------
        if ELSE_RE.match(stripped):
            if "{" in stripped:
                out.append(line)

                insert(indent + INDENT_STEP)
            else:
                out.append(line.rstrip() + " {")

                insert(indent + INDENT_STEP)
                i += 1
                out.append(indent + INDENT_STEP + lines[i].lstrip())
                out.append(indent + "}")

            i += 1
            continue

        # ---------- CASE ----------
        if CASE_RE.match(stripped):
            out.append(line)
            insert(indent + INDENT_STEP)
            i += 1
            continue

        # ---------- PRINTF / MALLOC ----------
        if re.search(r"\b(fprintf|printf|malloc|memcpy)\s*\(", line):
            insert(indent)
            out.append(line)
            i += 1
            continue

        # ---------- PTR OPS ----------
        if PTR_OPS_RE.match(stripped):
            insert(indent)
            out.append(line)
            i += 1
            continue

        out.append(line)
        macro_inserted = False

        i += 1

    return "".join(out)


filename = sys.argv[1]
instrumented_code = instrument_c_code(filename)

print(instrumented_code)

PY

