# Download third-party dependencies for the DOCX core pipeline
# Usage: powershell -ExecutionPolicy Bypass -File download_deps.ps1

$ErrorActionPreference = "Stop"
$tp = Join-Path $PSScriptRoot "third_party"

if (!(Test-Path $tp)) { New-Item -ItemType Directory -Path $tp | Out-Null }

function Download($url, $out) {
    Write-Host "Downloading $out ..."
    Invoke-WebRequest -Uri $url -OutFile (Join-Path $tp $out) -UseBasicParsing
}

# miniz - ZIP library (public domain)
Download "https://raw.githubusercontent.com/richgel999/miniz/master/miniz.h"        "miniz.h"
Download "https://raw.githubusercontent.com/richgel999/miniz/master/miniz.c"        "miniz.c"
Download "https://raw.githubusercontent.com/richgel999/miniz/master/miniz_common.h" "miniz_common.h"
Download "https://raw.githubusercontent.com/richgel999/miniz/master/miniz_tdef.h"   "miniz_tdef.h"
Download "https://raw.githubusercontent.com/richgel999/miniz/master/miniz_tdef.c"   "miniz_tdef.c"
Download "https://raw.githubusercontent.com/richgel999/miniz/master/miniz_tinfl.h"  "miniz_tinfl.h"
Download "https://raw.githubusercontent.com/richgel999/miniz/master/miniz_tinfl.c"  "miniz_tinfl.c"
Download "https://raw.githubusercontent.com/richgel999/miniz/master/miniz_zip.h"    "miniz_zip.h"
Download "https://raw.githubusercontent.com/richgel999/miniz/master/miniz_zip.c"    "miniz_zip.c"

# pugixml - XML DOM parser (MIT)
Download "https://raw.githubusercontent.com/zeux/pugixml/master/src/pugixml.hpp" "pugixml.hpp"
Download "https://raw.githubusercontent.com/zeux/pugixml/master/src/pugixml.cpp" "pugixml.cpp"

# stb libraries - single-header utilities (public domain / MIT)
Download "https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h" "stb_image_write.h"
Download "https://raw.githubusercontent.com/nothings/stb/master/stb_truetype.h"   "stb_truetype.h"
Download "https://raw.githubusercontent.com/nothings/stb/master/stb_image.h"       "stb_image.h"

Write-Host ""
Write-Host "All dependencies downloaded to $tp"
Write-Host "Files:"
Get-ChildItem $tp | ForEach-Object { Write-Host "  $($_.Name) ($($_.Length) bytes)" }
