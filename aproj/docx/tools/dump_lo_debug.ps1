# Dump LibreOffice SwNodes and layout tree using SW_DEBUG mode.
# Usage: .\dump_lo_debug.ps1 "path\to\input.docx"

param(
    [Parameter(Mandatory=$true)]
    [string]$InputFile
)

$ErrorActionPreference = "Stop"

# Resolve paths
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
# scriptDir = aproj/docx/tools, root = 3 levels up
$rootDir = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $scriptDir))
$loDir = Join-Path $rootDir "instdir\program"
$soffice = Join-Path $loDir "soffice.exe"

if (!(Test-Path $soffice)) {
    Write-Error "soffice.exe not found at $soffice"
    exit 1
}

if (!(Test-Path $InputFile)) {
    Write-Error "Input file not found: $InputFile"
    exit 1
}

$InputFile = (Resolve-Path $InputFile).Path

# Kill existing soffice
Write-Host "Killing existing soffice processes..."
Get-Process -Name "soffice" -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 2

# Set up output directory
$outputDir = $scriptDir

# Clean up old dumps
Remove-Item (Join-Path $outputDir "layout.xml") -ErrorAction SilentlyContinue
Remove-Item (Join-Path $outputDir "nodes.xml") -ErrorAction SilentlyContinue

# Start soffice with SW_DEBUG=1
Write-Host "Starting soffice with SW_DEBUG=1..."
$env:SW_DEBUG = "1"

$proc = Start-Process -FilePath $soffice -ArgumentList $InputFile `
    -WorkingDirectory $outputDir `
    -PassThru

# Wait for soffice to open and render
Write-Host "Waiting for soffice to load document..."
Start-Sleep -Seconds 8

# Load Windows Forms for SendKeys
Add-Type -AssemblyName System.Windows.Forms

# Send F12 for layout dump
Write-Host "Sending F12 (layout dump)..."
[System.Windows.Forms.SendKeys]::SendWait("{F12}")
Start-Sleep -Seconds 2

# Send Shift+F12 for nodes dump
Write-Host "Sending Shift+F12 (nodes dump)..."
[System.Windows.Forms.SendKeys]::SendWait("+{F12}")
Start-Sleep -Seconds 2

# Close soffice
Write-Host "Closing soffice..."
$proc.CloseMainWindow() | Out-Null
Start-Sleep -Seconds 2

if (!$proc.HasExited) {
    $proc.Kill()
}

# Check results
$layoutXml = Join-Path $outputDir "layout.xml"
$nodesXml = Join-Path $outputDir "nodes.xml"

if (Test-Path $layoutXml) {
    $size = (Get-Item $layoutXml).Length
    Write-Host "layout.xml created: $size bytes" -ForegroundColor Green
} else {
    Write-Warning "layout.xml not created"
}

if (Test-Path $nodesXml) {
    $size = (Get-Item $nodesXml).Length
    Write-Host "nodes.xml created: $size bytes" -ForegroundColor Green
} else {
    Write-Warning "nodes.xml not created"
}

# Clean up env
Remove-Item Env:SW_DEBUG -ErrorAction SilentlyContinue

Write-Host "Done. Output files in: $outputDir"
