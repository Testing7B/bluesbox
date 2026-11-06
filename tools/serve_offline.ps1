# SPDX-License-Identifier: GPL-3.0-only
# OpenPocket - offline installer: serves the docs\ folder (installer, editor, firmware packages) on this computer only,
# at http://localhost:8765/, and opens it in Chrome or Edge. No internet needed: Web MIDI works on localhost.
# Started by INSTALL-OFFLINE.bat. Close the window to stop it.
param([int]$Port = 8765, [string]$Page = "index.html")
$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\docs")).Path.TrimEnd('\')
$types = @{
    ".html" = "text/html; charset=utf-8"; ".js" = "text/javascript; charset=utf-8"; ".mjs" = "text/javascript; charset=utf-8"
    ".json" = "application/json; charset=utf-8"; ".css" = "text/css; charset=utf-8"; ".md" = "text/plain; charset=utf-8"
    ".png" = "image/png"; ".svg" = "image/svg+xml"; ".ico" = "image/x-icon"
    ".fwsc" = "application/octet-stream"; ".bin" = "application/octet-stream"
}

# a free port from 8765 on (only this computer can connect: localhost)
$listener = $null
for ($p = $Port; $p -lt $Port + 20; $p++) {
    $l = New-Object System.Net.HttpListener
    $l.Prefixes.Add("http://localhost:$p/")
    try { $l.Start(); $listener = $l; $Port = $p; break } catch { $l.Close() }
}
if (-not $listener) { Write-Host "Could not open a local port (8765 to 8784)."; exit 2 }

$url = "http://localhost:$Port/$Page"
$browsers = @(
    "$env:ProgramFiles\Google\Chrome\Application\chrome.exe",
    "${env:ProgramFiles(x86)}\Google\Chrome\Application\chrome.exe",
    "$env:LocalAppData\Google\Chrome\Application\chrome.exe",
    "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe",
    "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe"
) | Where-Object { $_ -and (Test-Path -LiteralPath $_) }
if ($browsers) { Start-Process -FilePath @($browsers)[0] -ArgumentList $url } else { Start-Process $url }

Write-Host ""
Write-Host "  OpenPocket offline installer: $url"
Write-Host "  Web editor:                   http://localhost:$Port/editor.html"
Write-Host "  (Chrome or Edge; this window must stay open while you install. Close it when you are done.)"
Write-Host ""

try {
    while ($listener.IsListening) {
        $ctx = $listener.GetContext()
        $res = $ctx.Response
        try {
            $rel = [Uri]::UnescapeDataString($ctx.Request.Url.AbsolutePath).TrimStart('/')
            if ($rel -eq "") { $rel = $Page }
            $full = [System.IO.Path]::GetFullPath((Join-Path $root ($rel -replace '/', '\')))
            $inside = $full.StartsWith($root + '\', [System.StringComparison]::OrdinalIgnoreCase)
            if ($inside -and (Test-Path -LiteralPath $full -PathType Leaf)) {
                $ext = [System.IO.Path]::GetExtension($full).ToLower()
                if ($types.ContainsKey($ext)) { $res.ContentType = $types[$ext] } else { $res.ContentType = "application/octet-stream" }
                $body = [System.IO.File]::ReadAllBytes($full)
            } else {
                $res.StatusCode = 404
                $res.ContentType = "text/plain; charset=utf-8"
                $body = [System.Text.Encoding]::UTF8.GetBytes("not found: $rel")
            }
            $res.AddHeader("Cache-Control", "no-store")
            $res.ContentLength64 = $body.Length
            $res.OutputStream.Write($body, 0, $body.Length)
            Write-Host ("  {0} {1}" -f $res.StatusCode, $rel)
        } catch {
            Write-Host ("  error: " + $_.Exception.Message)
        } finally {
            try { $res.OutputStream.Close() } catch { }
        }
    }
} finally {
    $listener.Stop()
    $listener.Close()
}
