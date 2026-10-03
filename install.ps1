# Miblo one-step installer for Windows (PowerShell 5.1 or newer).
#
#   irm https://raw.githubusercontent.com/marcus-campos/miblo/main/install.ps1 | iex
#
# Same steps as install.sh: find the Claude Code CLI, add the Miblo marketplace and install the
# plugin, or update both when they are already there. No administrator rights needed or used, and
# no token is ever printed.
#
# Exit codes (when run as a file; with `| iex` they land in $LASTEXITCODE so the window stays open):
#   0  installed or updated
#   1  no Claude Code found
#   2  the Miblo marketplace could not be added or updated
#   3  the plugin could not be installed or updated
#
# Environment (optional): MIBLO_CLAUDE=C:\path\to\claude.exe uses that CLI instead of searching.

$MibloRepoUrl = 'https://github.com/marcus-campos/miblo.git'
$MibloMarketplace = 'miblo'
$MibloPlugin = 'miblo@miblo'

function Test-MibloClaude([string]$Path) {
  if (-not $Path -or -not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $false }
  try {
    & $Path --version *> $null
    return ($LASTEXITCODE -eq 0)
  } catch {
    return $false
  }
}

# The Claude desktop app keeps its own Claude Code in a versioned folder. Its exact place on
# Windows is not documented: these are the folders it is known or expected to use (the macOS
# layout, claude-code\<version>\<hash>\..., under the app's data folders). Newest version first.
function Find-MibloDesktopClaude {
  $roots = @()
  if ($env:APPDATA) { $roots += (Join-Path $env:APPDATA 'Claude\claude-code') }
  if ($env:LOCALAPPDATA) {
    $roots += (Join-Path $env:LOCALAPPDATA 'Claude\claude-code')
    $roots += (Join-Path $env:LOCALAPPDATA 'AnthropicClaude\claude-code')
  }
  $found = @()
  foreach ($root in $roots) {
    if (-not (Test-Path -LiteralPath $root -PathType Container)) { continue }
    foreach ($verDir in Get-ChildItem -LiteralPath $root -Directory -ErrorAction SilentlyContinue) {
      $ver = $null
      if (-not [version]::TryParse($verDir.Name, [ref]$ver)) { continue }
      $exe = Get-ChildItem -LiteralPath $verDir.FullName -Recurse -Depth 4 -Filter 'claude.exe' -File -ErrorAction SilentlyContinue |
        Select-Object -First 1
      if ($exe) { $found += [pscustomobject]@{ Version = $ver; Path = $exe.FullName } }
    }
  }
  foreach ($c in ($found | Sort-Object -Property Version -Descending)) {
    if (Test-MibloClaude $c.Path) { return $c.Path }
  }
  return $null
}

function Find-MibloClaude {
  if ($env:MIBLO_CLAUDE) {
    if (Test-MibloClaude $env:MIBLO_CLAUDE) { return $env:MIBLO_CLAUDE }
    Write-Warning "MIBLO_CLAUDE=$($env:MIBLO_CLAUDE) is not a working Claude Code CLI; searching instead."
  }
  foreach ($cmd in Get-Command claude -CommandType Application, ExternalScript -ErrorAction SilentlyContinue) {
    if (Test-MibloClaude $cmd.Source) { return $cmd.Source }
  }
  $candidates = @()
  if ($env:USERPROFILE) {
    $candidates += (Join-Path $env:USERPROFILE '.local\bin\claude.exe')   # native installer
    $candidates += (Join-Path $env:USERPROFILE '.claude\local\claude.cmd') # older local install
  }
  if ($env:APPDATA) { $candidates += (Join-Path $env:APPDATA 'npm\claude.cmd') } # npm install -g
  foreach ($c in $candidates) {
    if (Test-MibloClaude $c) { return $c }
  }
  return Find-MibloDesktopClaude
}

function ConvertFrom-MibloJson($Lines) {
  try { return ((@($Lines) -join "`n") | ConvertFrom-Json) } catch { return $null }
}

function Install-Miblo {
  Write-Host 'Miblo installer'
  Write-Host '---------------'

  Write-Host "`n==> Looking for Claude Code"
  $claude = Find-MibloClaude
  if (-not $claude) {
    Write-Host ''
    Write-Host 'Claude Code was not found on this computer.'
    Write-Host ''
    Write-Host 'Miblo is a Claude Code plugin, so Claude Code comes first. Either:'
    Write-Host '  - install the Claude desktop app from https://claude.ai/download, open it once,'
    Write-Host '    sign in and open the Code tab; or'
    Write-Host '  - install Claude Code for the terminal: https://docs.claude.com/en/docs/claude-code/setup'
    Write-Host ''
    Write-Host 'Then run this installer again.'
    return 1
  }
  $version = (& $claude --version 2>$null | Select-Object -First 1)
  Write-Host "Found: $claude ($version)"

  Write-Host "`n==> Adding the Miblo marketplace"
  $markets = ConvertFrom-MibloJson (& $claude plugin marketplace list --json 2>$null)
  if (@($markets | Where-Object { $_.name -eq $MibloMarketplace }).Count -gt 0) {
    Write-Host 'Already added; refreshing it.'
    & $claude plugin marketplace update $MibloMarketplace | Out-Host
    if ($LASTEXITCODE -ne 0) { Write-Warning 'Could not refresh the marketplace (offline?). Continuing with the copy on disk.' }
  } else {
    & $claude plugin marketplace add $MibloRepoUrl | Out-Host
    if ($LASTEXITCODE -ne 0) {
      Write-Host ''
      Write-Host 'Could not add the Miblo marketplace. Check your internet connection and try again.'
      Write-Host 'To do it by hand, in Claude Code run: /plugin marketplace add marcus-campos/miblo'
      return 2
    }
  }

  Write-Host "`n==> Installing the Miblo plugin"
  $plugins = ConvertFrom-MibloJson (& $claude plugin list --json 2>$null)
  $mine = @($plugins | Where-Object { $_.id -eq $MibloPlugin }) | Select-Object -First 1
  if ($mine) {
    Write-Host 'Already installed; updating it.'
    & $claude plugin update $MibloPlugin | Out-Host
    if ($LASTEXITCODE -ne 0) {
      Write-Host ''
      Write-Host 'Could not update the plugin. Check your internet connection and try again.'
      return 3
    }
    if ($mine.enabled -eq $false) {
      Write-Host 'It was turned off; turning it back on.'
      & $claude plugin enable $MibloPlugin | Out-Host
      if ($LASTEXITCODE -ne 0) { Write-Warning 'Could not enable it: run /plugin in Claude Code to turn it on.' }
    }
  } else {
    & $claude plugin install $MibloPlugin | Out-Host
    if ($LASTEXITCODE -ne 0) {
      Write-Host ''
      Write-Host 'Could not install the plugin. Check your internet connection and try again.'
      Write-Host 'To do it by hand, in Claude Code run: /plugin install miblo@miblo'
      return 3
    }
  }

  Write-Host ''
  Write-Host 'Miblo is installed.'
  Write-Host ''
  Write-Host 'Next steps:'
  Write-Host '  1. Restart Claude Code: quit and reopen the Claude desktop app (then open the Code tab),'
  Write-Host "     your IDE's Claude Code panel, or the claude command in your terminal."
  Write-Host '  2. Plug in your Miblo and connect it to Wi-Fi (the screen shows a QR code).'
  Write-Host '  3. In Claude Code, type /miblo:pair and enter the 4-digit code from the screen.'
  return 0
}

# Native commands write to Out-Host above, so the function returns only its exit code.
$mibloCode = Install-Miblo
# Run as a file: a real exit code. Through `irm | iex`: exiting would close the PowerShell window,
# so the code is left in $LASTEXITCODE instead.
if ($MyInvocation.MyCommand.Path) { exit $mibloCode } else { $global:LASTEXITCODE = $mibloCode }
