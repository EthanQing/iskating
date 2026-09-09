@echo off
setlocal DisableDelayedExpansion
title iSkating Local Backend
pushd "%~dp0"
if errorlevel 1 exit /b 1

if exist "server\local-env.cmd" call "server\local-env.cmd"
if not defined ISKATING_DATABASE_URL goto missing_config

set "BACKEND_PYTHON=%CD%\server\.venv\Scripts\python.exe"
if exist "%BACKEND_PYTHON%" goto check_dependencies

echo Creating backend virtual environment...
python -m venv "server\.venv"
if errorlevel 1 goto failed

:check_dependencies
"%BACKEND_PYTHON%" -c "import uvicorn, fastapi, sqlalchemy, psycopg, jwt, passlib, bcrypt" >nul 2>&1
if not errorlevel 1 goto prepare_secret
echo Installing backend dependencies...
"%BACKEND_PYTHON%" -m pip install -r "server\requirements.txt"
if errorlevel 1 goto failed

:prepare_secret
if defined ISKATING_JWT_SECRET goto start_server
for /f "delims=" %%K in ('call "%BACKEND_PYTHON%" -c "import secrets; print(secrets.token_hex(32))"') do set "ISKATING_JWT_SECRET=%%K"
if not defined ISKATING_JWT_SECRET goto failed

:start_server
echo.
echo iSkating API: http://127.0.0.1:8000
echo API docs:    http://127.0.0.1:8000/docs
echo Press Ctrl+C to stop. Source changes reload automatically.
echo.
"%BACKEND_PYTHON%" -m uvicorn app.main:app --app-dir server --host 127.0.0.1 --port 8000 --reload --reload-dir server/app
set "BACKEND_EXIT_CODE=%ERRORLEVEL%"
if not "%BACKEND_EXIT_CODE%"=="0" goto stopped_with_error
popd
exit /b 0

:missing_config
echo Database connection is not configured.
echo Copy server\local-env.cmd.example to server\local-env.cmd
echo and set ISKATING_DATABASE_URL for your local test database.
echo The database and its tables must already exist.
goto failed

:stopped_with_error
echo.
echo Backend exited with code %BACKEND_EXIT_CODE%. Check the log above.
pause
popd
exit /b %BACKEND_EXIT_CODE%

:failed
echo.
echo Backend could not start. Check the error above and README.md.
pause
popd
exit /b 1
