# DevGovernor PowerShell Demo Launcher
# Automatically delegates to WSL where gcc, make, and Linux /proc are installed.

Write-Host "Starting DevGovernor Live Demo via WSL..." -ForegroundColor Cyan
wsl make demo
