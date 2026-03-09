#!/usr/bin/env bash

set -euo pipefail

sanitize_file() {
  local file="$1"
  local tmp

  tmp="$(mktemp)"
  awk '
    $0 == "#include \"mvm.h\"" { next }
    $0 == "INSTRUMENT;" { next }
    { print }
  ' "$file" >"$tmp"
  mv "$tmp" "$file"
}

for file in "$@"; do
  sanitize_file "$file"
done
