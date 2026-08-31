#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
web=$root/onekvm-recovery-web
config=$(mktemp)
trap 'rm -f "$config"' EXIT

cat >"$config" <<'EOF'
{
  "schema_version": 18,
  "auth": {
    "username": "admin",
    "web_password_hash": "$2a$10$not-a-real-hash",
    "enabled": true,
    "users": [
      {"username": "admin", "password_hash": "$2a$10$not-a-real-hash", "role": "admin"}
    ]
  }
}
EOF

"$web" --reset-user "$config"
grep -F '"username": ""' "$config" >/dev/null
grep -F '"web_password_hash": ""' "$config" >/dev/null
grep -F '"users": []' "$config" >/dev/null
if grep -F '"username": "admin"' "$config" >/dev/null; then
    echo "admin username remained:" >&2
    cat "$config" >&2
    exit 1
fi
echo "reset-user tests passed"
