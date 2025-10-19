# OPC UA HTTP Bridge Test Server Startup Script

Write-Host "=== OPC UA HTTP Bridge Test Server ===" -ForegroundColor Cyan
Write-Host ""

# Set environment variables
$env:OPC_ENDPOINT = "opc.tcp://opcua.umati.app:4840"
$env:SERVER_PORT = "8080"
$env:LOG_LEVEL = "info"

# Optional: Set CORS configuration (allow all origins for testing)
# $env:ALLOWED_ORIGINS = "*"

Write-Host "Configuration:" -ForegroundColor Yellow
Write-Host "  OPC_ENDPOINT: $env:OPC_ENDPOINT" -ForegroundColor White
Write-Host "  SERVER_PORT: $env:SERVER_PORT" -ForegroundColor White
Write-Host "  LOG_LEVEL: $env:LOG_LEVEL" -ForegroundColor White
Write-Host ""

Write-Host "Test Nodes:" -ForegroundColor Yellow
Write-Host "  ns=25;i=59524 (UInt32)" -ForegroundColor White
Write-Host "  ns=25;i=59525 (Double)" -ForegroundColor White  
Write-Host "  ns=18;i=59113 (Int32)" -ForegroundColor White
Write-Host ""

Write-Host "Starting OPC UA HTTP Bridge..." -ForegroundColor Green
Write-Host "Press Ctrl+C to stop the server" -ForegroundColor Gray
Write-Host ""

# Start the server
& "cmake-build-debug/Debug/opcua2http.exe"