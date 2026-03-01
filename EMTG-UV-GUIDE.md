# Using uv with EMTG

This project uses [`uv`](https://docs.astral.sh/uv/) for Python dependency management. This guide explains how to use it.

## Quick Start

### Install uv

```bash
pip install uv
```

Or see [uv installation guide](https://docs.astral.sh/uv/guides/installation/).

### Sync Dependencies

```bash
# Install core dependencies (numpy, scipy, matplotlib, spiceypy, etc.)
uv sync --python 3.12

# With optional GUI dependencies (wxPython)
uv sync --python 3.12 --extra gui

# With dev dependencies (pytest, etc.)
uv sync --python 3.12 --extra dev
```

This creates a `.venv` virtual environment with all dependencies installed.

## Running Testatron Tests

### Set up environment

```bash
# Activate the virtual environment
source .venv/bin/activate  # On Windows: .venv\Scripts\activate

# Set PYTHONPATH so testatron can import PyEMTG
export PYTHONPATH=/workspace/PyEMTG:$PYTHONPATH  # On Windows: set PYTHONPATH=C:\path\to\PyEMTG
```

### Run testatron

```bash
# View all available test options
python testatron/testatron.py -h

# Run a specific test case
python testatron/testatron.py -e /path/to/EMTGv9 -p /workspace/PyEMTG -c tests/transcription_tests/MGALT_EMintercept

# Run all tests in a folder
python testatron/testatron.py -e /path/to/EMTGv9 -p /workspace/PyEMTG -f transcription_tests/

# Run all tests
python testatron/testatron.py -e /path/to/EMTGv9 -p /workspace/PyEMTG -a

# Update truth files after intentional changes
python testatron/testatron.py -e /path/to/EMTGv9 -p /workspace/PyEMTG --update_truths
```

### One-liner with uv

Alternatively, you can use `uv run` to run commands without manually activating:

```bash
uv run --with-editable . python testatron/testatron.py -e /path/to/EMTGv9 -p /workspace/PyEMTG -c tests/transcription_tests/MGALT_EMintercept
```

## Project Structure

- **pyproject.toml** — Project configuration and dependency declarations
- **PyEMTG/** — Python module with trajectory analysis and utilities
- **testatron/** — Regression test framework for EMTG
- **.venv/** — Virtual environment (auto-created by `uv sync`)
- **uv.lock** — Lock file with pinned versions (auto-generated, should be committed)

## Dependency Groups

### Core Dependencies

Always installed by `uv sync`:
- numpy, scipy — Numerical computing
- astropy — Astronomical utilities
- autograd — Automatic differentiation
- matplotlib — Plotting
- spiceypy — CSpice ephemeris library
- psutil — System utilities

### Optional: GUI (`--extra gui`)

For wxPython GUI tools:
- wxPython — Desktop GUI framework

### Optional: Development (`--extra dev`)

For testing and development:
- pytest — Testing framework
- pytest-cov — Code coverage reports

## Tips

1. **Lock file** — After syncing, a `uv.lock` file is created with pinned dependency versions. Commit this to version control for reproducible environments.

2. **Python version** — The project requires Python 3.12+. If you don't have it installed, `uv sync` will automatically download and use it (via `--python 3.12`).

3. **Add dependencies** — To add a new package, edit `pyproject.toml` and run `uv sync` again.

4. **PYTHONPATH** — When running testatron, always set `PYTHONPATH=/workspace/PyEMTG` so Python can find the local modules.

## Troubleshooting

### "ModuleNotFoundError: No module named 'Mission'"

Make sure `PYTHONPATH` includes the PyEMTG directory:

```bash
export PYTHONPATH=/workspace/PyEMTG:$PYTHONPATH
```

### "No module named 'uv'"

Install uv first:

```bash
pip install uv
```

### Virtual environment issues

If you encounter issues with the `.venv`, simply delete it and re-create:

```bash
rm -rf .venv
uv sync --python 3.12
```
