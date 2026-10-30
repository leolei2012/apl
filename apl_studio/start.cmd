@echo off
cd /d "%~dp0"
if not exist node_modules (
  echo Local dependencies are missing. See README.md. Nothing was installed.
  pause
  exit /b 1
)
call npm start
if errorlevel 1 pause
