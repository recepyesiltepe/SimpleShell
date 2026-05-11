#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

if [[ ! -x "bin/SimpleShell" ]]; then
  make
fi

exec python3 "ui/simpleshell_gui.py"
