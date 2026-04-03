#!/usr/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BINARY="$SCRIPT_DIR/build/remotepad"
LOGFILE="$SCRIPT_DIR/.remotepad.log"

if [ ! -f "$BINARY" ]; then
    echo "Binary not found at $BINARY"
    echo "Build first: cd build && cmake .. && make -j\$(nproc)"
    exit 1
fi

# Pass stop and help directly to the binary
case "${1:-}" in
    stop)
        "$BINARY" stop
        exit $?
        ;;
    help|-h|--help)
        "$BINARY" help
        echo ""
        echo "Launcher usage:"
        echo "  ./remotepad.sh              Select window by clicking"
        echo "  ./remotepad.sh current      Use the focused window"
        echo "  ./remotepad.sh <id>         Use window with given ID"
        echo "  ./remotepad.sh stop         Stop running instance"
        exit 0
        ;;
esac

# Resolve window ID
if [ -z "$1" ]; then
    echo "Click on a window to select it..."
    WID=$(xwininfo -int | grep "Window id:" | awk '{print $4}')
elif [ "$1" = "current" ]; then
    WID=$(xprop -root 2>/dev/null | sed -n '/^_NET_ACTIVE_WINDOW/ s/.* // p')
else
    WID="$1"
fi

if [ -z "$WID" ]; then
    echo "No window selected"
    exit 1
fi

# Read port from .env or default
PORT=$(grep -E '^SERVER_PORT' "$SCRIPT_DIR/.env" 2>/dev/null | sed 's/.*[=:] *//')
PORT="${PORT:-3000}"
IP=$(hostname -I 2>/dev/null | awk '{print $1}')

# Launch in background
cd "$SCRIPT_DIR"
"$BINARY" "$WID" > "$LOGFILE" 2>&1 &
PID=$!

sleep 0.2
if ! kill -0 "$PID" 2>/dev/null; then
    echo "Failed to start. Check $LOGFILE"
    exit 1
fi

echo "remotepad running (pid $PID)"
echo "  Tablet: http://${IP}:${PORT}"
echo "  Log:    $LOGFILE"
echo "  Stop:   remotepad stop"
