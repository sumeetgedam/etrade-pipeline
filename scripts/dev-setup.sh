#!/bin/bash
set -euo pipefail

# Run inside container to create python venv and install requirements

PYTHON_BIN=${PYTHON_BIN:-python3}
VENV_DIR=".venv"
REQ_FILE="python/requirements.txt"

echo "[dev-setup] using python : $(which $PYTHON_BIN 2>/dev/null || echo 'not found')"

if ! command -v "$PYTHON_BIN" >/dev/null 2>&1; then
    echo "[error] $PYTHON_BIN not found, install python3 inside the container" >&2
    exit 1
fi

if [ ! -d "$VENV_DIR" ]; then
    echo "[dev-setup] Creating venv at $VENV_DIR"
    "$PYTHON_BIN" -m venv "$VENV_DIR"
fi

. "$VENV_DIR/bin/activate"

pip install --upgrade pip setuptools wheel
if [ -f "$REQ_FILE" ]; then
    echo "[dev-setup] Installing requirements from $REQ_FILE"
    pip install -r "$REQ_FILE"
else
    echo "[dev-setup] Warning: $REQ_FILE not found, skipping pip install"
fi

echo "[dev-setup] Done. Activat the venv with: . $VENV_DIR/bin/activate"
echo "[dev-setup] To run the FastAPI app : uvicorn python.app.main:app --host 0.0.0.0 --port 8000"