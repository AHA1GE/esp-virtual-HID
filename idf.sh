#!/bin/bash
# Run idf.py (ESP-IDF v6.1, EIM install) from Git Bash.
# Usage: ./idf.sh <idf.py args...>   e.g. ./idf.sh build  /  ./idf.sh -p COM196 flash monitor
ARGS="$*"
exec powershell -NoProfile -ExecutionPolicy Bypass -Command "
Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue;
& 'C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1' > \$null;
Set-Location 'D:\Harry\esp32-virtual-keyboard';
idf.py $ARGS;
exit \$LASTEXITCode
"
