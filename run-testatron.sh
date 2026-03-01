#!/bin/bash
# Helper script to run testatron with proper Python path

set -e

# Get the absolute path to the script's directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# Ensure virtual environment exists
if [ ! -d "$SCRIPT_DIR/.venv" ]; then
    echo "Virtual environment not found. Running 'uv sync'..."
    uv sync --python 3.12
fi

# Activate virtual environment
source "$SCRIPT_DIR/.venv/bin/activate"

# Set PYTHONPATH to include PyEMTG
export PYTHONPATH="$SCRIPT_DIR/PyEMTG:$PYTHONPATH"

# Run testatron with all arguments passed through
python "$SCRIPT_DIR/testatron/testatron.py" "$@"
