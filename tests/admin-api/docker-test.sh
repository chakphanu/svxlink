#!/bin/bash
#
# Build and run Admin API tests in Docker
#
# Usage:
#   ./docker-test.sh         # Build and run tests
#   ./docker-test.sh --build # Build only
#   ./docker-test.sh --run   # Run only (assume image exists)
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
IMAGE_NAME="svxreflector-admin-test"

cd "$PROJECT_ROOT"

build_image() {
    echo "=========================================="
    echo "Building Docker image: $IMAGE_NAME"
    echo "=========================================="
    docker build \
        -f tests/admin-api/Dockerfile \
        -t "$IMAGE_NAME" \
        .
}

run_tests() {
    echo "=========================================="
    echo "Running tests in Docker (--network host)"
    echo "=========================================="

    # Create container (don't auto-remove)
    docker run \
        --network host \
        --name svxreflector-test \
        "$IMAGE_NAME"
    TEST_RESULT=$?

    # Copy test reports from container
    docker cp svxreflector-test:/test/TEST-RESULT.md "$SCRIPT_DIR/TEST-RESULT.md" 2>/dev/null || true
    docker cp svxreflector-test:/test/CLIENT-TEST-RESULT.md "$SCRIPT_DIR/CLIENT-TEST-RESULT.md" 2>/dev/null || true

    # Remove container
    docker rm svxreflector-test >/dev/null 2>&1 || true

    # Show Admin API test report
    if [ -f "$SCRIPT_DIR/TEST-RESULT.md" ]; then
        echo ""
        echo "=========================================="
        echo "Admin API Test Report:"
        echo "=========================================="
        cat "$SCRIPT_DIR/TEST-RESULT.md"
    fi

    # Show Client Integration test report
    if [ -f "$SCRIPT_DIR/CLIENT-TEST-RESULT.md" ]; then
        echo ""
        echo "=========================================="
        echo "Client Integration Test Report:"
        echo "=========================================="
        cat "$SCRIPT_DIR/CLIENT-TEST-RESULT.md"
    fi

    return $TEST_RESULT
}

case "${1:-}" in
    --build)
        build_image
        ;;
    --run)
        run_tests
        ;;
    *)
        build_image
        echo ""
        run_tests
        ;;
esac
