#!/bin/bash
# Local-only field-plan scenarios 1-7. Real game clients and relay; no
# system-wide firewall changes. See join_link_scenarios.py for fault scope.
set -eu
# Always use a fresh display: clipboard operations must be isolated too.
exec xvfb-run -a -s '-screen 0 1280x800x24' python3 "$(dirname "$0")/join_link_scenarios.py" "$@"
