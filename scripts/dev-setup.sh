#!/bin/bash
set -e

# Run inside container to create python venv and install requirements

python3 -m venv .venv
. .venv/bin/activate
pip install --upgrade pip
pip install -r python/requirements.txt

echo "Python venv created at .venv and dependecies Installed."
