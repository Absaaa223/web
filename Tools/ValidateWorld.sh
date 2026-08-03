#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# DUST // Beyond the Horizon
# Runs the world validator and the census. Intended for CI.
#
# Exit codes:
#   0  world passes every design rule
#   1  one or more failures - see Saved/WorldBake/WorldValidation.txt
#
# The important check is the interest-desert sweep: if more than 2% of the
# world has nothing within the guaranteed interest radius, the build fails.
# That is how "you never walk for the sake of walking" stops being a matter of
# discipline and becomes a property the build enforces.
# ---------------------------------------------------------------------------
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUTPUT_DIR="${PROJECT_ROOT}/Saved/WorldBake"

echo "=== DUST world validation ==="

"${PROJECT_ROOT}/Tools/RunCommandlet.sh" DustWorldBake \
	-mode=validate \
	-out="${OUTPUT_DIR}"

VALIDATION_EXIT=$?

echo
echo "=== DUST world census ==="

"${PROJECT_ROOT}/Tools/RunCommandlet.sh" DustWorldBake \
	-mode=stats \
	-out="${OUTPUT_DIR}"

if [[ -f "${OUTPUT_DIR}/WorldStatistics.txt" ]]; then
	echo
	cat "${OUTPUT_DIR}/WorldStatistics.txt"
fi

if [[ ${VALIDATION_EXIT} -ne 0 ]]; then
	echo
	echo "WORLD VALIDATION FAILED. See ${OUTPUT_DIR}/WorldValidation.txt" >&2
	exit 1
fi

echo
echo "World validation passed."
