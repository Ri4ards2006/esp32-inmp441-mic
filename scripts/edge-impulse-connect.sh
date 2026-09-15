#!/usr/bin/env bash
# ==============================================================================
# Edge Impulse Connection Helper for ESP32 + INMP441 Microphone
# ==============================================================================
# Usage:
#   ./scripts/edge-impulse-connect.sh [--daemon | --forwarder] [--clean] [--api-key <KEY>]
# ==============================================================================

set -euo pipefail

# Project metadata
DEFAULT_PROJECT_ID="1113935"
DEFAULT_PROJECT_NAME="Ri4iboy-project-1"
DEFAULT_BAUD="115200"
DEFAULT_SENSOR_NAME="microphone"
DEFAULT_FREQ="16000"

# Colors for terminal output
BOLD="\033[1m"
GREEN="\033[0;32m"
YELLOW="\033[1;33m"
CYAN="\033[0;36m"
RED="\033[0;31m"
RESET="\033[0m"

echo -e "${BOLD}${CYAN}======================================================${RESET}"
echo -e "${BOLD}${CYAN}   ESP32 <-> Edge Impulse Cloud Connection Bridge     ${RESET}"
echo -e "${BOLD}${CYAN}======================================================${RESET}"
echo -e "Target Project : ${GREEN}${DEFAULT_PROJECT_NAME}${RESET} (ID: ${DEFAULT_PROJECT_ID})"
echo -e "Default Sensor : ${GREEN}${DEFAULT_SENSOR_NAME}${RESET} @ ${DEFAULT_FREQ} Hz"
echo ""

# Determine runner (host vs container)
RUNNER=""
if command -v flatpak-spawn >/dev/null 2>&1; then
    RUNNER="flatpak-spawn --host"
fi

# Locate CLI binaries
CLI_DIR="$HOME/.npm-global/bin"
DAEMON_BIN="$CLI_DIR/edge-impulse-daemon"
FORWARDER_BIN="$CLI_DIR/edge-impulse-data-forwarder"

if [ ! -f "$DAEMON_BIN" ] || [ ! -f "$FORWARDER_BIN" ]; then
    echo -e "${RED}[ERROR] Edge Impulse CLI binaries not found in $CLI_DIR.${RESET}"
    echo "Please run: npm install -g edge-impulse-cli"
    exit 1
fi

# Detect serial port
DETECTED_PORTS=($($RUNNER ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null || true))
if [ ${#DETECTED_PORTS[@]} -eq 0 ]; then
    echo -e "${RED}[ERROR] No serial devices found (/dev/ttyUSB* or /dev/ttyACM*).${RESET}"
    echo "Please ensure your ESP32-WROOM-32D is plugged into a USB port."
    exit 1
fi

PORT="${DETECTED_PORTS[0]}"
echo -e "${GREEN}[OK] Detected serial port: ${BOLD}${PORT}${RESET}"

# Verify permissions
if ! $RUNNER test -r "$PORT" || ! $RUNNER test -w "$PORT"; then
    echo -e "${YELLOW}[WARNING] Insufficient permissions on ${PORT}.${RESET}"
    echo "Run: sudo usermod -aG uucp \$USER (and log back in)."
fi

# Determine whether to run daemon or data-forwarder
MODE="forwarder"
EXTRA_ARGS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --daemon)
            MODE="daemon"
            shift
            ;;
        --forwarder)
            MODE="forwarder"
            shift
            ;;
        *)
            EXTRA_ARGS+=("$1")
            shift
            ;;
    esac
done

if [ "$MODE" = "daemon" ]; then
    echo -e "${CYAN}Launching Edge Impulse Daemon...${RESET}"
    echo "Note: edge-impulse-daemon connects to boards running AT-command firmware."
    exec $RUNNER "$DAEMON_BIN" --baud-rate "$DEFAULT_BAUD" "${EXTRA_ARGS[@]}"
else
    echo -e "${CYAN}Launching Edge Impulse Data Forwarder...${RESET}"
    echo "Streaming serial sensor data to project ${DEFAULT_PROJECT_NAME}..."
    exec $RUNNER "$FORWARDER_BIN" \
        --baud-rate "$DEFAULT_BAUD" \
        --frequency "$DEFAULT_FREQ" \
        "${EXTRA_ARGS[@]}"
fi

