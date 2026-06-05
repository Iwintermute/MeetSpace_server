@echo off
setlocal
set "BACKEND_ROOT=%~dp0"
if not exist "%BACKEND_ROOT%node_modules/mediasoup" (
  pushd "%BACKEND_ROOT%"
  where npm >nul 2>nul || (
    echo [mediasoup-backend] npm not found. Install Node.js with npm first.
    popd
    exit /b 1
  )
  if exist "%BACKEND_ROOT%package-lock.json" (
    call npm ci --no-audit --no-fund || (
      echo [mediasoup-backend] npm ci failed.
      popd
      exit /b 1
    )
  ) else (
    call npm install --no-audit --no-fund || (
      echo [mediasoup-backend] npm install failed.
      popd
      exit /b 1
    )
  )
  popd
)
if "%MEDIASOUP_BACKEND_HOST%"=="" set "MEDIASOUP_BACKEND_HOST=127.0.0.1"
if "%MEDIASOUP_BACKEND_PORT%"=="" set "MEDIASOUP_BACKEND_PORT=5001"
if "%MEDIASOUP_BACKEND_PATH%"=="" set "MEDIASOUP_BACKEND_PATH=/ws"
if "%MEDIASOUP_RTC_MIN_PORT%"=="" set "MEDIASOUP_RTC_MIN_PORT=40000"
if "%MEDIASOUP_RTC_MAX_PORT%"=="" set "MEDIASOUP_RTC_MAX_PORT=49999"
if "%MEDIASOUP_ANNOUNCED_IP%"=="" (
  for /f "usebackq delims=" %%I in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "(Invoke-WebRequest -Uri 'https://api.ipify.org' -UseBasicParsing -TimeoutSec 5).Content.Trim()" 2^>nul`) do set "MEDIASOUP_ANNOUNCED_IP=%%I"
)
if "%MEDIASOUP_ANNOUNCED_IP%"=="" (
  for /f "usebackq delims=" %%I in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "(Get-NetIPAddress -AddressFamily IPv4 ^| Where-Object { $_.IPAddress -ne '127.0.0.1' -and $_.PrefixOrigin -ne 'WellKnown' } ^| Select-Object -First 1 -ExpandProperty IPAddress)" 2^>nul`) do set "MEDIASOUP_ANNOUNCED_IP=%%I"
)
if "%MEDIASOUP_ANNOUNCED_IP%"=="" (
  if "%MEDIASOUP_RTC_LISTEN_IP%"=="" set "MEDIASOUP_RTC_LISTEN_IP=127.0.0.1"
) else (
  if "%MEDIASOUP_RTC_LISTEN_IP%"=="" set "MEDIASOUP_RTC_LISTEN_IP=0.0.0.0"
)
if "%MEDIASOUP_PHYSICAL_CORE_COUNT%"=="" (
  for /f "usebackq delims=" %%I in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "$cores=(Get-CimInstance Win32_Processor ^| Measure-Object -Property NumberOfCores -Sum).Sum; if(-not $cores -or $cores -lt 1){$cores=[Math]::Max(1,[Math]::Floor([Environment]::ProcessorCount/2))}; [int]$cores" 2^>nul`) do set "MEDIASOUP_PHYSICAL_CORE_COUNT=%%I"
)
if "%MEDIASOUP_PHYSICAL_CORE_COUNT%"=="" set "MEDIASOUP_PHYSICAL_CORE_COUNT=1"
if "%MEDIASOUP_WORKER_COUNT%"=="" set "MEDIASOUP_WORKER_COUNT=%MEDIASOUP_PHYSICAL_CORE_COUNT%"
if "%MEDIASOUP_MAX_PEERS_PER_WORKER%"=="" set "MEDIASOUP_MAX_PEERS_PER_WORKER=500"
if "%MEDIASOUP_MAX_INFLIGHT_OPERATIONS%"=="" set "MEDIASOUP_MAX_INFLIGHT_OPERATIONS=2000"
if "%MEDIASOUP_MAX_GLOBAL_PENDING_MESSAGES%"=="" set "MEDIASOUP_MAX_GLOBAL_PENDING_MESSAGES=20000"
node "%BACKEND_ROOT%src/server.js"
