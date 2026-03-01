@echo off
REM Helper script to run testatron with proper Python path on Windows

setlocal enabledelayedexpansion

REM Get the directory this batch file is in
set SCRIPT_DIR=%~dp0

REM Ensure virtual environment exists
if not exist "%SCRIPT_DIR%.venv" (
    echo Virtual environment not found. Running 'uv sync'...
    call uv sync --python 3.12
)

REM Activate virtual environment
call "%SCRIPT_DIR%.venv\Scripts\activate.bat"

REM Set PYTHONPATH to include PyEMTG
set PYTHONPATH=%SCRIPT_DIR%PyEMTG;!PYTHONPATH!

REM Run testatron with all arguments passed through
python "%SCRIPT_DIR%testatron\testatron.py" %*
