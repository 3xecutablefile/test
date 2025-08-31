@echo off
REM Usage:
REM   codex-cli "Implement a TS HTTP client with retries and AbortController"
REM   codex-cli rust "Add IOCP-based DeviceIoControl wrapper with timeouts"
set MODEL=codex
set MODE=%1
shift

if /I "%MODE%"=="rust" (
  set SYS=prompts\codex_rust_system.prompt
) else (
  set SYS=prompts\codex_ts_system.prompt
  set MODE=ts
)

REM Replace the command below with your Codex CLI binary/invocation
REM Example uses a hypothetical 'codex' tool
codex ^
  --model %MODEL% ^
  --system "%SYS%" ^
  --input "%*" ^
  --code

