#!/bin/bash
#
# Run SvxReflector Admin API tests
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
LOG_FILE="/var/log/svxlink/svxreflector.log"

echo "=========================================="
echo "SvxReflector Admin API Test Runner"
echo "=========================================="

# Cleanup function
cleanup() {
    echo ""
    echo "Stopping svxreflector..."
    if [ -n "$REFLECTOR_PID" ]; then
        kill $REFLECTOR_PID 2>/dev/null || true
        wait $REFLECTOR_PID 2>/dev/null || true
    fi
    echo "Done."
}
trap cleanup EXIT

# Start svxreflector in background
echo "Starting svxreflector..."
svxreflector --config=/etc/svxlink/svxreflector.conf > "$LOG_FILE" 2>&1 &
REFLECTOR_PID=$!

# Wait a moment for startup
sleep 2

# Check if process is still running
if ! kill -0 $REFLECTOR_PID 2>/dev/null; then
    echo "ERROR: svxreflector failed to start"
    echo "Log output:"
    cat "$LOG_FILE"
    exit 1
fi

echo "svxreflector started (PID: $REFLECTOR_PID)"

# Run Admin API tests
echo ""
echo "Running Admin API tests..."
echo ""

set +e  # Don't exit on test failures
python3 "$SCRIPT_DIR/test_admin_api.py" --wait --report="$SCRIPT_DIR/TEST-RESULT.md"
API_TEST_RESULT=$?
set -e

# Run Client Integration tests
echo ""
echo "=========================================="
echo "Running Client Integration tests..."
echo "=========================================="
echo ""

set +e
python3 "$SCRIPT_DIR/client_test.py" --report="$SCRIPT_DIR/CLIENT-TEST-RESULT.md"
CLIENT_TEST_RESULT=$?
set -e

# Combine results
if [ $API_TEST_RESULT -eq 0 ] && [ $CLIENT_TEST_RESULT -eq 0 ]; then
    TEST_RESULT=0
else
    TEST_RESULT=1
fi

# Always show logs for debugging
echo ""
echo "=========================================="
echo "Server log (last 100 lines):"
echo "=========================================="
tail -100 "$LOG_FILE"

exit $TEST_RESULT
