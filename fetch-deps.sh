#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

fetch_repo() {
  local url="$1"
  local rel_path="$2"
  local ref="${3:-}"

  local dest="${SCRIPT_DIR}/components/${rel_path}"
  mkdir -p "$(dirname -- "${dest}")"

  # If the destination doesn't look like a git repo yet, clone it; otherwise, update it in place.
  if [[ ! -d "${dest}/.git" ]]; then
    git clone "${url}" "${dest}"
  else
    git -C "${dest}" fetch --tags --prune
  fi

  # If a ref (tag/branch/commit) is provided (and not "-"), check it out.
  if [[ -n "${ref}" && "${ref}" != "-" ]]; then
    git -C "${dest}" checkout "${ref}" 2>/dev/null || git -C "${dest}" checkout -B "${ref}" "origin/${ref}"
  fi
}

fetch_repo "git@github.com:mikaryyn/LovyanGFX.git" "LovyanGFX" "master"
fetch_repo "https://github.com/bytecodealliance/wasm-micro-runtime" "wamr" "WAMR-2.4.4"
fetch_repo "git@github.com:mikaryyn/FastEPD.git" "FastEPD" "main"

git apply patches/wamr.patch
