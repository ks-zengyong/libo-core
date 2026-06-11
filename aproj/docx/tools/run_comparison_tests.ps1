# run_comparison_tests.ps1
# 自动化渲染指令比对测试
# 遍历 tests/ 目录下所有 .docx 文件，生成 LibreOffice 参考输出和 aproj 输出，然后比对
#
# 用法:
#   powershell -ExecutionPolicy Bypass -File tools/run_comparison_tests.ps1
#
# 前置条件:
#   1. LibreOffice 已编译 (instdir/program/soffice.exe)
#   2. aproj/docx 已编译 (build/docx_e2e_test.exe)
#   3. render_diff 已编译 (build/render_diff.exe)

param(
    [string]$TestDir = "aproj/docx/tests",
    [string]$LoExe = "instdir/program/soffice.exe",
    [string]$E2eTest = "aproj/docx/build/Release/docx_e2e_test.exe",
    [string]$RenderDiff = "aproj/docx/build/Release/render_diff.exe",
    [string]$KnownDiffs = "aproj/docx/tests/known_diffs.txt"
)

$ErrorActionPreference = "Stop"

# Resolve paths relative to script directory
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$rootDir = Split-Path -Parent (Split-Path -Parent $scriptDir)

$LoExe = Join-Path $rootDir $LoExe
$E2eTest = Join-Path $rootDir $E2eTest
$RenderDiff = Join-Path $rootDir $RenderDiff
$TestDir = Join-Path $rootDir $TestDir
$KnownDiffs = Join-Path $rootDir $KnownDiffs

# Validate prerequisites
if (!(Test-Path $LoExe)) {
    Write-Error "LibreOffice not found at $LoExe"
    exit 1
}
if (!(Test-Path $E2eTest)) {
    Write-Error "E2E test not found at $E2eTest"
    exit 1
}
if (!(Test-Path $RenderDiff)) {
    Write-Error "render_diff not found at $RenderDiff"
    exit 1
}
if (!(Test-Path $TestDir)) {
    Write-Error "Test directory not found at $TestDir"
    exit 1
}

# Kill existing soffice
Write-Host "Killing existing soffice processes..." -ForegroundColor Yellow
Get-Process -Name "soffice" -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 2

# Results
$results = @()

# Find all .docx files
$docxFiles = Get-ChildItem "$TestDir/*.docx" | Sort-Object Name

if ($docxFiles.Count -eq 0) {
    Write-Warning "No .docx files found in $TestDir"
    exit 0
}

Write-Host "Found $($docxFiles.Count) test files" -ForegroundColor Cyan
Write-Host ""

foreach ($docx in $docxFiles) {
    $name = $docx.BaseName
    Write-Host "=== $name ===" -ForegroundColor Green

    $refFile = Join-Path $TestDir "ref_$name.txt"
    $outFile = Join-Path $TestDir "out_$name.txt"
    $diffFile = Join-Path $TestDir "diff_$name.txt"

    # Step 1: Generate LibreOffice reference
    Write-Host "  [1/3] Generating LibreOffice reference..."
    $env:SW_RENDER_LOG = $refFile
    try {
        $proc = Start-Process -FilePath $LoExe -ArgumentList "--headless", $docx.FullName `
            -NoNewWindow -PassThru -Wait -Timeout 30
    } catch {
        Write-Warning "  LibreOffice timed out for $name"
        $results += [PSCustomObject]@{
            Test = $name
            Status = "TIMEOUT"
            Differences = -1
        }
        continue
    } finally {
        Remove-Item Env:SW_RENDER_LOG -ErrorAction SilentlyContinue
    }

    if (!(Test-Path $refFile)) {
        Write-Warning "  Reference file not generated for $name"
        $results += [PSCustomObject]@{
            Test = $name
            Status = "NO_REF"
            Differences = -1
        }
        continue
    }
    $refSize = (Get-Item $refFile).Length
    Write-Host "  Reference: $refSize bytes"

    # Step 2: Generate aproj output
    Write-Host "  [2/3] Generating aproj output..."
    try {
        & $E2eTest $docx.FullName 2>&1 | Out-Null
        if (Test-Path "render_output.txt") {
            Move-Item "render_output.txt" $outFile -Force
        }
    } catch {
        Write-Warning "  E2E test failed for $name"
        $results += [PSCustomObject]@{
            Test = $name
            Status = "E2E_FAIL"
            Differences = -1
        }
        continue
    }

    if (!(Test-Path $outFile)) {
        Write-Warning "  Output file not generated for $name"
        $results += [PSCustomObject]@{
            Test = $name
            Status = "NO_OUTPUT"
            Differences = -1
        }
        continue
    }
    $outSize = (Get-Item $outFile).Length
    Write-Host "  Output:    $outSize bytes"

    # Step 3: Compare
    Write-Host "  [3/3] Comparing..."
    $diffArgs = @($refFile, $outFile)
    if (Test-Path $KnownDiffs) {
        $diffArgs += "--known-diffs"
        $diffArgs += $KnownDiffs
    }

    $diffOutput = & $RenderDiff @diffArgs 2>&1
    $diffOutput | Out-File $diffFile -Encoding UTF8

    # Parse result
    $pass = ($diffOutput | Select-String "Result: PASS") -ne $null
    $diffCount = 0
    $match = ($diffOutput | Select-String "New differences:\s+(\d+)")
    if ($match) {
        $diffCount = [int]$match.Matches[0].Groups[1].Value
    }

    $status = if ($pass) { "PASS" } else { "FAIL" }
    $color = if ($pass) { "Green" } else { "Red" }
    Write-Host "  Result: $status ($diffCount new differences)" -ForegroundColor $color

    $results += [PSCustomObject]@{
        Test = $name
        Status = $status
        Differences = $diffCount
    }

    Write-Host ""
}

# Summary
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Comparison Test Summary" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
$results | Format-Table -AutoSize

$passCount = ($results | Where-Object { $_.Status -eq "PASS" }).Count
$failCount = ($results | Where-Object { $_.Status -eq "FAIL" }).Count
$totalCount = $results.Count

Write-Host "Total: $totalCount | Pass: $passCount | Fail: $failCount" -ForegroundColor $(if ($failCount -eq 0) { "Green" } else { "Red" })

if ($failCount -gt 0) {
    exit 1
}
