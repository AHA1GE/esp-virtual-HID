#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Harry Sun
# Run idf.py (ESP-IDF v6.1, EIM install) from Git Bash.
# Usage: ./idf.sh <idf.py args...>   e.g. ./idf.sh build  /  ./idf.sh -p COM196 flash monitor
#
# The build runs in this checkout, wherever it lives. Set IDF_PROFILE to use a
# different PowerShell profile for the ESP-IDF environment.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WIN_DIR="$(cygpath -w "$SCRIPT_DIR" 2>/dev/null || printf '%s' "$SCRIPT_DIR")"
PROFILE="${IDF_PROFILE:-C:\\Espressif\\tools\\Microsoft.v6.1.PowerShell_profile.ps1}"

ARGS="$*"
export IDF_SH_DIR="$WIN_DIR"
export IDF_SH_PROFILE="$PROFILE"

# RemoteSigned (not Bypass): the profile is a local script, so it loads, while
# downloaded unsigned scripts stay blocked.
exec powershell -NoProfile -ExecutionPolicy RemoteSigned -Command "
\$ErrorActionPreference = 'Stop';
Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue;
if (Test-Path \$env:IDF_SH_PROFILE) { & \$env:IDF_SH_PROFILE > \$null }
Set-Location \$env:IDF_SH_DIR;
idf.py $ARGS;
exit \$LASTEXITCODE
"
