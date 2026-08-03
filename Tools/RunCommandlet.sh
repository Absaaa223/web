#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# DUST // Beyond the Horizon
# Runs an editor commandlet against the project.
#
#   Tools/RunCommandlet.sh DustBootstrap
#   Tools/RunCommandlet.sh DustWorldBake -mode=validate
#   Tools/RunCommandlet.sh DustWorldBake -mode=heightmap -resolution=4033
# ---------------------------------------------------------------------------
set -euo pipefail

if [[ $# -lt 1 ]]; then
	echo "usage: $0 <CommandletName> [extra args...]" >&2
	exit 2
fi

COMMANDLET="$1"
shift

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT_FILE="${PROJECT_ROOT}/Dust.uproject"

if [[ ! -f "${PROJECT_FILE}" ]]; then
	echo "error: Dust.uproject not found at ${PROJECT_FILE}" >&2
	exit 1
fi

# UE_ROOT can be set explicitly; otherwise try the usual install locations.
if [[ -z "${UE_ROOT:-}" ]]; then
	for CANDIDATE in \
		"$HOME/UnrealEngine" \
		"/opt/UnrealEngine" \
		"/Users/Shared/Epic Games/UE_5.5" \
		"/c/Program Files/Epic Games/UE_5.5"
	do
		if [[ -d "${CANDIDATE}/Engine" ]]; then
			UE_ROOT="${CANDIDATE}"
			break
		fi
	done
fi

if [[ -z "${UE_ROOT:-}" ]]; then
	echo "error: could not locate Unreal Engine. Set UE_ROOT to your engine root." >&2
	exit 1
fi

# Platform-specific editor binary.
case "$(uname -s)" in
	Linux*)  EDITOR="${UE_ROOT}/Engine/Binaries/Linux/UnrealEditor-Cmd" ;;
	Darwin*) EDITOR="${UE_ROOT}/Engine/Binaries/Mac/UnrealEditor-Cmd" ;;
	*)       EDITOR="${UE_ROOT}/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" ;;
esac

if [[ ! -x "${EDITOR}" ]]; then
	echo "error: editor binary not found or not executable: ${EDITOR}" >&2
	exit 1
fi

echo "Running ${COMMANDLET}..."

# -unattended and -nopause so this is usable from CI without hanging on a prompt.
exec "${EDITOR}" \
	"${PROJECT_FILE}" \
	-run="${COMMANDLET}" \
	-unattended \
	-nopause \
	-nosplash \
	-stdout \
	-fullstdoutlogoutput \
	"$@"
