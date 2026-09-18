#!/bin/bash
# Copy Haiku's syslog out of a run's image copy: syslog.sh <name>  -> work/<name>/syslog/syslog
set -euo pipefail
source "$(dirname "$0")/env.sh"
NAME=${1:?usage: syslog.sh <name>}
exec "$PW_ROOT/scripts/haiku-syslog.sh" "$PW_WORK/$NAME/haiku.img" "$PW_WORK/$NAME/syslog"
