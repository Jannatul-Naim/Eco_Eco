#!/usr/bin/env bash
# Runs a current PlatformIO Core (6.x) even when an obsolete one is first on PATH.
#
# Ubuntu's apt package "platformio 4.3.4" (/usr/bin/pio) cannot parse the
# espressif32 platform this project uses and crashes on start. This wrapper
# prefers the Core in ~/.platformio/penv (what the official installer and the
# VS Code extension create) and refuses anything older than 6.
#
# Usage: tools/pio.sh run -e s3_main        (any normal pio arguments)
set -euo pipefail

candidates=(
  "${PLATFORMIO_PIO:-}"
  "$HOME/.platformio/penv/bin/pio"
  "$(command -v pio 2>/dev/null || true)"
)

for pio in "${candidates[@]}"; do
  [[ -n "$pio" && -x "$pio" ]] || continue
  version="$("$pio" --version 2>/dev/null | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1 || true)"
  major="${version%%.*}"
  if [[ -n "$major" && "$major" -ge 6 ]]; then
    exec "$pio" "$@"
  fi
  echo "skipping $pio (version '${version:-unreadable}', need 6.x)" >&2
done

cat >&2 <<'EOF'
No PlatformIO Core 6.x found.
Install it without touching the apt package:
  curl -fsSL -o /tmp/get-platformio.py https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py
  python3 /tmp/get-platformio.py
Then run this script again (it uses ~/.platformio/penv/bin/pio).
EOF
exit 1
