#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# DUST // Beyond the Horizon
# Generates IDE project files for the seven modules.
# ---------------------------------------------------------------------------
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT_FILE="${PROJECT_ROOT}/Dust.uproject"

if [[ -z "${UE_ROOT:-}" ]]; then
	for CANDIDATE in \
		"$HOME/UnrealEngine" \
		"/opt/UnrealEngine" \
		"/Users/Shared/Epic Games/UE_5.5"
	do
		if [[ -d "${CANDIDATE}/Engine" ]]; then
			UE_ROOT="${CANDIDATE}"
			break
		fi
	done
fi

if [[ -z "${UE_ROOT:-}" ]]; then
	echo "error: set UE_ROOT to your Unreal Engine 5.5 root." >&2
	exit 1
fi

echo "Generating project files for DUST..."
"${UE_ROOT}/Engine/Build/BatchFiles/Linux/GenerateProjectFiles.sh" \
	-project="${PROJECT_FILE}" \
	-game \
	-engine \
	2>/dev/null \
|| "${UE_ROOT}/Engine/Build/BatchFiles/Mac/GenerateProjectFiles.sh" \
	-project="${PROJECT_FILE}" \
	-game \
	-engine

echo "Done. Open the generated workspace, or run Tools/RunCommandlet.sh DustBootstrap first."
