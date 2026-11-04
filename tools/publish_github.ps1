# OpenPocket: publish on GitHub, from this folder (PUBLISH-GITHUB.bat). Modelled on CLIPPER's publish script.
#   1. the source: openpocket.bundle (git history) -> the repository OWNER/REPO (created if it does not exist),
#      branch main, the tag vNN
#   2. the release vNN: the .fwsc packages build.bat made (docs\firmware)
#   3. the web installer on GitHub Pages (branch gh-pages: docs\ with the packages)
# Uses your own Git and GitHub CLI (gh) and your own GitHub login. Nothing is ever forced on main: a repository that
# already has another history stops the script. gh-pages is generated: it is replaced each time.
param([string]$Owner = "", [string]$Repo = "openpocket")
$ErrorActionPreference = 'Continue'
Set-Location (Split-Path $PSScriptRoot)
function Fail([string]$m) { Write-Host ""; Write-Host "ERROR: $m" -ForegroundColor Red; exit 1 }
function Step([string]$m) { Write-Host "== $m" -ForegroundColor Cyan }

$ver = (Select-String -Path "firmware\openpocket.c" -Pattern '#define OP_VERSION_NUM "(\d+)"').Matches[0].Groups[1].Value
if (-not $ver) { Fail "OP_VERSION_NUM not found in firmware\openpocket.c" }
$Tag = "v$ver"
Write-Host ""
Write-Host "  O P E N P O C K E T   $Tag   ->   GitHub" -ForegroundColor White

function Find-Tool([string]$t) {
  if (Get-Command $t -ErrorAction SilentlyContinue) { return $true }
  $env:Path = [Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [Environment]::GetEnvironmentVariable("Path", "User")
  if (Get-Command $t -ErrorAction SilentlyContinue) { return $true }
  $dirs = if ($t -eq "gh") { @("$env:ProgramFiles\GitHub CLI", "$env:LOCALAPPDATA\Programs\GitHub CLI") }
          else { @("$env:ProgramFiles\Git\cmd", "$env:LOCALAPPDATA\Programs\Git\cmd") }
  foreach ($d in $dirs) { if ($d -and (Test-Path (Join-Path $d "$t.exe"))) { $env:Path = "$d;$env:Path"; return $true } }
  return $false
}
$ids = @{ git = "Git.Git"; gh = "GitHub.cli" }
foreach ($t in @("git", "gh")) {
  if (Find-Tool $t) { continue }
  if (Get-Command winget -ErrorAction SilentlyContinue) {
    $a = Read-Host "$t is not installed. Install it now with winget? (Y/N)"
    if ($a -match '^[yYoO]') {
      winget install --id $ids[$t] -e --source winget --accept-package-agreements --accept-source-agreements
      if (Find-Tool $t) { continue }
      Fail "$t is installed but not visible yet: close this window and run PUBLISH-GITHUB.bat again"
    }
  }
  Fail "$t not found. Install it, then run PUBLISH-GITHUB.bat again."
}
gh auth status *> $null
if ($LASTEXITCODE -ne 0) {
  Step "Signing in to GitHub (the browser will open)"
  gh auth login --web --git-protocol https
  if ($LASTEXITCODE -ne 0) { Fail "signing in to GitHub failed" }
}
gh auth setup-git *> $null
if (-not $Owner) { $Owner = "$(gh api user --jq .login)".Trim() }
if (-not $Owner) { Fail "could not read your GitHub user name" }
$full = "$Owner/$Repo"
$pages = "https://$Owner.github.io/$Repo/"
$email = "$Owner@users.noreply.github.com"
$work = Join-Path "build" "publish"
if (Test-Path $work) { Remove-Item -Recurse -Force $work }
New-Item -ItemType Directory -Force $work | Out-Null

# 1. the source
$repoDir = Join-Path $work "repo"
if (-not (Test-Path "openpocket.bundle")) { Fail "openpocket.bundle not found in this folder" }
Step "The source (from openpocket.bundle)"
git clone --quiet --branch main openpocket.bundle $repoDir
if ($LASTEXITCODE -ne 0) { Fail "git clone of the bundle" }
Push-Location $repoDir
gh repo view $full *> $null
if ($LASTEXITCODE -ne 0) {
  Step "Creating the public repository $full"
  gh repo create $full --public --description "OpenPocket: open-source guitar firmware for the Donner Pocket X / M-VAVE BlackBox" --homepage $pages
  if ($LASTEXITCODE -ne 0) { Pop-Location; Fail "creating the repository $full" }
}
git remote remove origin *> $null
git remote add origin "https://github.com/$full.git"
Step "Pushing the main branch to $full"
git push -u origin main
if ($LASTEXITCODE -ne 0) {
  # commits made on GitHub since (e.g. the README edited on the site): put this version on top of them, never force
  Step "GitHub has changes that are not in the bundle (edits made on the site): adding this version on top of them"
  git fetch --quiet origin main
  git -c user.name="$Owner" -c user.email="$email" rebase origin/main
  if ($LASTEXITCODE -ne 0) {
    git rebase --abort *> $null
    Pop-Location; Fail "the changes on GitHub conflict with this version (same lines edited). Nothing was forced."
  }
  git push -u origin main
  if ($LASTEXITCODE -ne 0) { Pop-Location; Fail "git push was refused ($full may have another history). Nothing was forced: PUBLISH-GITHUB.bat -Repo another-name" }
}
if (-not "$(git ls-remote --tags origin "refs/tags/$Tag")".Trim()) {
  git -c user.name="$Owner" -c user.email="$email" tag -a $Tag -m "OpenPocket $Tag"
  git push origin $Tag
}
Pop-Location

# 2. the release
$pk = @(Get-ChildItem "docs\firmware\*.fwsc" -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
if ($pk.Count -eq 0) { Fail "no package in docs\firmware: run build.bat first" }
$notes = Join-Path $work "notes.md"
$text = "OpenPocket $Tag`n`nInstall from the browser (Chrome / Edge): $pages`nPreset editor (v32 and newer): ${pages}editor.html`n`nPackages: PocketX_9$ver.fwsc (Donner Pocket X), BlackBox_9$ver.fwsc (M-VAVE BlackBox)."
[IO.File]::WriteAllText($notes, $text, (New-Object Text.UTF8Encoding $false))
gh release view $Tag --repo $full *> $null
if ($LASTEXITCODE -eq 0) {
  Step "The release $Tag exists: updating its files"
  gh release upload $Tag @pk --repo $full --clobber
} else {
  Step "Creating the release $Tag"
  gh release create $Tag @pk --repo $full --latest --title "OpenPocket $Tag" --notes-file $notes
}
if ($LASTEXITCODE -ne 0) { Fail "the release $Tag" }

# 3. GitHub Pages: the installer (docs\ with the packages)
Step "The web installer on GitHub Pages"
$pg = Join-Path $work "pages"
New-Item -ItemType Directory -Force $pg | Out-Null
Copy-Item -Recurse "docs\*" $pg
New-Item -ItemType File -Force (Join-Path $pg ".nojekyll") | Out-Null
Push-Location $pg
git init --quiet
git checkout --quiet -b gh-pages
git add -A
git -c user.name="$Owner" -c user.email="$email" commit --quiet -m "OpenPocket ${Tag}: the web installer"
git push --force "https://github.com/$full.git" gh-pages
$ok = $LASTEXITCODE -eq 0
Pop-Location
if (-not $ok) { Fail "pushing the gh-pages branch" }
gh api -X POST "repos/$full/pages" -f "source[branch]=gh-pages" -f "source[path]=/" *> $null   # (already on: refused, fine)

Write-Host ""
Write-Host "Repository:  https://github.com/$full" -ForegroundColor Green
Write-Host "Release:     https://github.com/$full/releases/tag/$Tag" -ForegroundColor Green
Write-Host "Installer:   $pages   (the first time, GitHub takes a minute or two)" -ForegroundColor Green
Write-Host "Editor:      ${pages}editor.html" -ForegroundColor Green
