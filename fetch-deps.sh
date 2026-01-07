#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

fetch_repo() {
  local url="$1"
  local rel_path="$2"
  local ref="${3:-}"
  local with_submodules="${4:-0}"
  local patch_rel="${5:-}"

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
    git -C "${dest}" checkout "${ref}"
  fi

  # If explicitly requested, sync git submodules too.
  if [[ "${with_submodules}" == "1" ]]; then
    git -C "${dest}" submodule update --init --recursive
  fi

  # If a patch path is provided (and not "-"), verify it applies cleanly, then apply it.
  if [[ -n "${patch_rel}" && "${patch_rel}" != "-" ]]; then
    local patch_path="${SCRIPT_DIR}/${patch_rel}"
    git -C "${dest}" apply --check "${patch_path}"
    git -C "${dest}" apply "${patch_path}"
  fi
}

fetch_repo "https://github.com/lovyan03/LovyanGFX.git" "LovyanGFX" "1.2.7"
fetch_repo "https://github.com/bytecodealliance/wasm-micro-runtime" "wamr" "WAMR-2.4.4"
fetch_repo "https://github.com/bitbank2/FastEPD" "FastEPD" "1.4.2"

cp -a patches/LovyanGFX/. components/LovyanGFX/src/lgfx/v1/platforms/esp32/
