#!/bin/sh
# Read-only: is this checkout ready to be verified? Prints one line per check; exit 1 when something is missing.
cd "$(dirname "$0")/../../../.." || exit 1
bad=0
chk() { if eval "$2" >/dev/null 2>&1; then echo "ok   $1"; else echo "FAIL $1"; bad=1; fi; }
chk "Node 18+ (op.mjs)" 'node -e "process.exit(+process.versions.node.split(\".\")[0] >= 18 ? 0 : 1)"'
chk "Docker running (build, test)" 'docker info'
chk "C compiler (host simulators)" 'command -v cc'
chk "Python 3 (package tools)" 'command -v python3'
if node -e 'require("playwright")' >/dev/null 2>&1 || [ -d "${PW_DIR:-$HOME/.cache/openpocket-pw}/node_modules/playwright" ]; then echo "ok   Playwright (page tests)"; else echo "note Playwright missing: run scripts/pages.sh once, it says how (installs outside the repo)"; fi
git status --short | grep -q . && echo "note working tree has uncommitted changes (they are what you verify)"
exit $bad
