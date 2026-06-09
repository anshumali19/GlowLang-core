#!/bin/bash
# GlowLang Deployment Script
# Usage: ./deploy.sh [environment] [app_file]
#
# Examples:
#   ./deploy.sh production examples/web_server.glow
#   ./deploy.sh staging examples/hello.glow

set -euo pipefail

# Configuration
APP_NAME="${APP_NAME:-glowlang-app}"
DEPLOY_ENV="${1:-production}"
APP_FILE="${2:-examples/hello.glow}"
DEPLOY_DIR="${DEPLOY_DIR:-/opt/glowlang}"
LOG_DIR="${LOG_DIR:-/var/log/glowlang}"
PID_FILE="${DEPLOY_DIR}/${APP_NAME}.pid"
BINARY="${DEPLOY_DIR}/bin/glowlang"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log_info()  { echo -e "${GREEN}[INFO]${NC}  $1"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC}  $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# Pre-flight checks
preflight() {
    log_info "Running pre-flight checks..."

    if [ ! -f "bin/glowlang" ]; then
        log_error "GlowLang binary not found. Run 'make release' first."
        exit 1
    fi

    if [ ! -f "${APP_FILE}" ]; then
        log_error "Application file not found: ${APP_FILE}"
        exit 1
    fi

    log_info "Pre-flight checks passed."
}

# Build
build() {
    log_info "Building GlowLang (${DEPLOY_ENV})..."
    make clean
    if [ "${DEPLOY_ENV}" = "production" ]; then
        make release
    else
        make
    fi
    log_info "Build complete."
}

# Deploy files
deploy_files() {
    log_info "Deploying to ${DEPLOY_DIR}..."

    sudo mkdir -p "${DEPLOY_DIR}/bin"
    sudo mkdir -p "${DEPLOY_DIR}/lib"
    sudo mkdir -p "${DEPLOY_DIR}/app"
    sudo mkdir -p "${LOG_DIR}"

    # Copy binary
    sudo cp bin/glowlang "${BINARY}"
    sudo chmod +x "${BINARY}"

    # Copy libraries
    sudo cp -r lib/* "${DEPLOY_DIR}/lib/" 2>/dev/null || true

    # Copy application
    sudo cp "${APP_FILE}" "${DEPLOY_DIR}/app/main.glow"

    log_info "Files deployed."
}

# Stop existing process
stop_app() {
    if [ -f "${PID_FILE}" ]; then
        local pid
        pid=$(cat "${PID_FILE}")
        if kill -0 "${pid}" 2>/dev/null; then
            log_info "Stopping existing process (PID: ${pid})..."
            kill "${pid}"
            sleep 2
            if kill -0 "${pid}" 2>/dev/null; then
                log_warn "Process did not stop gracefully, forcing..."
                kill -9 "${pid}"
            fi
        fi
        sudo rm -f "${PID_FILE}"
    fi
}

# Start application
start_app() {
    log_info "Starting ${APP_NAME} (${DEPLOY_ENV})..."

    export GLOW_ENV="${DEPLOY_ENV}"
    export GLOW_LOG_DIR="${LOG_DIR}"

    nohup "${BINARY}" "${DEPLOY_DIR}/app/main.glow" \
        > "${LOG_DIR}/${APP_NAME}.log" 2>&1 &
    local pid=$!
    echo "${pid}" | sudo tee "${PID_FILE}" > /dev/null

    sleep 1
    if kill -0 "${pid}" 2>/dev/null; then
        log_info "Application started (PID: ${pid})"
    else
        log_error "Application failed to start. Check ${LOG_DIR}/${APP_NAME}.log"
        exit 1
    fi
}

# Health check
health_check() {
    log_info "Running health check..."
    local retries=5
    local count=0
    while [ ${count} -lt ${retries} ]; do
        if curl -sf http://localhost:8080/health > /dev/null 2>&1; then
            log_info "Health check passed!"
            return 0
        fi
        count=$((count + 1))
        log_warn "Health check attempt ${count}/${retries} failed, retrying..."
        sleep 2
    done
    log_warn "Health check did not pass (app may not have HTTP endpoints)."
    return 0
}

# Status check
status() {
    if [ -f "${PID_FILE}" ]; then
        local pid
        pid=$(cat "${PID_FILE}")
        if kill -0 "${pid}" 2>/dev/null; then
            log_info "${APP_NAME} is running (PID: ${pid})"
            return 0
        fi
    fi
    log_warn "${APP_NAME} is not running."
    return 1
}

# Main deployment flow
main() {
    echo "========================================="
    echo "  GlowLang Deployment"
    echo "  Environment: ${DEPLOY_ENV}"
    echo "  Application: ${APP_FILE}"
    echo "========================================="
    echo ""

    preflight
    build
    stop_app
    deploy_files
    start_app
    health_check

    echo ""
    log_info "Deployment complete!"
    echo "========================================="
}

# Handle commands
case "${1:-deploy}" in
    status)
        status
        ;;
    stop)
        stop_app
        log_info "Application stopped."
        ;;
    restart)
        stop_app
        start_app
        health_check
        ;;
    *)
        main
        ;;
esac
