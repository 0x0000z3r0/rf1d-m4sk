#!/usr/bin/env bash
set -euo pipefail

if ! command -v python &> /dev/null; then
    ln -sf /usr/bin/python3 /usr/bin/python || true
fi

exec "$@"
