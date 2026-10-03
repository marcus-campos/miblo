# Miblo one-step installer for Windows (PowerShell 5.1 or newer).
#
#   irm https://raw.githubusercontent.com/marcus-campos/miblo/main/install.ps1 | iex
#
#   & ([scriptblock]::Create((irm https://raw.githubusercontent.com/marcus-campos/miblo/main/install.ps1))) -NoPair
#
# Same steps as install.sh: find the Claude Code CLI, add the Miblo marketplace and install the
# plugin, or update both when they are already there; then pair the gadget, so /miblo:pair is not
# needed (find it on the network, type the 4-digit code from its screen). The plugin's launcher
# is a POSIX sh script, so pairing runs it through Git Bash's sh when Git for Windows is there,
# else uses `node` (20 or newer) from PATH; with neither, it points to /miblo:pair, which offers
# to install Node.js. -NoPair (or MIBLO_NO_PAIR=1), or a run with no console to type in, skips
# pairing. No administrator rights needed or used, and no token is ever printed.
#
# Exit codes (when run as a file; with `| iex` they land in $LASTEXITCODE so the window stays open;
# pairing never changes them, the plugin is installed either way):
#   0  installed or updated
#   1  no Claude Code found
#   2  the Miblo marketplace could not be added or updated
#   3  the plugin could not be installed or updated
#
# Environment (optional): MIBLO_CLAUDE=C:\path\to\claude.exe uses that CLI instead of searching;
# MIBLO_NO_PAIR=1 is the same as -NoPair.

param([switch]$NoPair)

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
  if ($NoPair -or $env:MIBLO_NO_PAIR -eq '1') {
    Write-MibloPairLater
  } elseif (-not [Environment]::UserInteractive -or [Console]::IsInputRedirected) {
    Write-Host ''
    Write-Host 'Skipping pairing: there is no console to type the pairing code in.'
    Write-MibloPairLater
  } else {
    $null = Invoke-MibloPair $claude
  }
  return 0
}

# --- Pairing ------------------------------------------------------------------------------------

function Write-MibloPairLater {
  Write-Host ''
  Write-Host 'Next steps:'
  Write-Host '  1. Restart Claude Code: quit and reopen the Claude desktop app (then open the Code tab),'
  Write-Host "     your IDE's Claude Code panel, or the claude command in your terminal."
  Write-Host '  2. Plug in your Miblo and connect it to Wi-Fi (the screen shows a QR code).'
  Write-Host '  3. In Claude Code, type /miblo:pair and enter the 4-digit code from the screen.'
}

function Write-MibloPairFailed {
  Write-Host ''
  Write-Host 'Miblo is installed, but not paired yet. To pair it later, open Claude Code and type'
  Write-Host '/miblo:pair (restart Claude Code first if it was open).'
}

function Get-MibloConfigDir {
  if ($env:CLAUDE_CONFIG_DIR) { return $env:CLAUDE_CONFIG_DIR }
  return (Join-Path $env:USERPROFILE '.claude')
}

# The installed plugin's folder: its installPath from `claude plugin list --json`, else the newest
# version in Claude Code's plugin cache.
function Get-MibloPluginDir([string]$Claude) {
  $plugins = ConvertFrom-MibloJson (& $Claude plugin list --json 2>$null)
  $mine = @($plugins | Where-Object { $_.id -eq $MibloPlugin }) | Select-Object -First 1
  if ($mine -and $mine.installPath -and (Test-Path -LiteralPath (Join-Path $mine.installPath 'bin\miblo-run'))) {
    return $mine.installPath
  }
  $base = Join-Path (Get-MibloConfigDir) 'plugins\cache\miblo\miblo'
  if (-not (Test-Path -LiteralPath $base -PathType Container)) { return $null }
  $found = @()
  foreach ($dir in Get-ChildItem -LiteralPath $base -Directory -ErrorAction SilentlyContinue) {
    $ver = $null
    if ([version]::TryParse($dir.Name, [ref]$ver) -and (Test-Path -LiteralPath (Join-Path $dir.FullName 'bin\miblo-run'))) {
      $found += [pscustomobject]@{ Version = $ver; Path = $dir.FullName }
    }
  }
  return ($found | Sort-Object -Property Version -Descending | Select-Object -First 1).Path
}

# The plugin's data folder, the one Claude Code passes to its hooks as CLAUDE_PLUGIN_DATA:
# <config>\plugins\data\miblo-miblo, the config dir being CLAUDE_CONFIG_DIR or ~\.claude. The
# launcher takes it in any spelling (C:\x, C:/x, /c/x), so Node is set up once, where the hooks look.
function Get-MibloDataDir([string]$Plugin) {
  return (Join-Path (Get-MibloConfigDir) 'plugins\data\miblo-miblo')
}

# Git for Windows' sh, which runs the plugin's launcher (the same one Claude Code's hooks use).
# Never WSL's bash in System32: it would look for Node.js inside Linux.
function Find-MibloSh {
  $candidates = @()
  foreach ($cmd in Get-Command git -CommandType Application -ErrorAction SilentlyContinue) {
    $candidates += (Join-Path (Split-Path (Split-Path $cmd.Source -Parent) -Parent) 'bin\sh.exe')
  }
  if ($env:ProgramFiles) { $candidates += (Join-Path $env:ProgramFiles 'Git\bin\sh.exe') }
  if (${env:ProgramFiles(x86)}) { $candidates += (Join-Path ${env:ProgramFiles(x86)} 'Git\bin\sh.exe') }
  if ($env:LOCALAPPDATA) { $candidates += (Join-Path $env:LOCALAPPDATA 'Programs\Git\bin\sh.exe') }
  foreach ($c in $candidates) {
    if (Test-Path -LiteralPath $c -PathType Leaf) { return $c }
  }
  return $null
}

# Node.js 20 or newer on PATH, or $null.
function Find-MibloNode {
  foreach ($cmd in Get-Command node -CommandType Application -ErrorAction SilentlyContinue) {
    try {
      $v = (& $cmd.Source -v 2>$null | Select-Object -First 1)
      if ($v -match '^v(\d+)\.' -and [int]$Matches[1] -ge 20) { return $cmd.Source }
    } catch { }
  }
  return $null
}

# Finds the gadget to pair, with a scriptblock that runs the CLI. Returns @{Name; Addr} or $null
# when the user skips.
function Select-MibloGadget([scriptblock]$Cli) {
  while ($true) {
    Write-Host 'Searching your network...'
    $list = @(& $Cli discover | Where-Object { $_ -match "`t" } | ForEach-Object {
        $f = $_ -split "`t"
        [pscustomobject]@{ Name = $f[1]; Addr = $f[2] }
      })
    if ($list.Count -eq 1) {
      Write-Host "Found $($list[0].Name) ($($list[0].Addr))."
      return $list[0]
    }
    if ($list.Count -gt 1) {
      Write-Host "Found $($list.Count) Miblos:"
      for ($i = 0; $i -lt $list.Count; $i++) { Write-Host "  $($i + 1)) $($list[$i].Name) ($($list[$i].Addr))" }
      while ($true) {
        $answer = Read-Host 'Which one? Type its number (or q to skip)'
        if ($answer -match '^[qQ]$') { return $null }
        $n = 0
        if ([int]::TryParse($answer, [ref]$n) -and $n -ge 1 -and $n -le $list.Count) { return $list[$n - 1] }
        Write-Host "Type a number from 1 to $($list.Count)."
      }
    }
    Write-Host ''
    Write-Host 'No Miblo found on this network. Check that your Miblo:'
    Write-Host '  - is plugged in and switched on;'
    Write-Host '  - is on the same Wi-Fi as this computer (if its screen shows a QR code, scan it with'
    Write-Host '    your phone to connect it to Wi-Fi first);'
    Write-Host '  - shows a 4-digit pairing code on its screen.'
    $answer = Read-Host 'Press Enter to search again, type the IP address shown at the bottom of its screen, or q to skip'
    if ($answer -match '^[qQ]$') { return $null }
    if ($answer -match '^\d{1,3}(\.\d{1,3}){3}(:\d{1,5})?$') {
      return [pscustomobject]@{ Name = "the Miblo at $answer"; Addr = $answer }
    }
    if ($answer) { Write-Host 'That is not an IP address.' }
  }
}

function Invoke-MibloPair([string]$Claude) {
  Write-Host "`n==> Pairing your Miblo"
  $plugin = Get-MibloPluginDir $Claude
  if (-not $plugin) {
    Write-Warning "Could not find the installed plugin's folder."
    Write-MibloPairFailed
    return
  }
  $data = Get-MibloDataDir $plugin

  $sh = Find-MibloSh
  if ($sh) {
    # The launcher finds its scripts from its own path, written with forward slashes for sh.
    $launcher = (Join-Path $plugin 'bin\miblo-run') -replace '\\', '/'
    $dataArg = $data -replace '\\', '/'
    Write-Host 'Getting Node.js ready (the first time this can take a minute)...'
    $check = (& $sh $launcher --check --data $dataArg | Select-Object -First 1)
    if ("$check" -notlike 'ok*') {
      Write-Host ''
      Write-Host "Miblo needs Node.js 20 or newer and could not set it up ($("$check" -replace '^missing reason=', ''))."
      Write-Host 'Install it from https://nodejs.org, or open Claude Code and type /miblo:pair: it offers'
      Write-Host 'to install Node.js for you, then pairs.'
      return
    }
    $cli = { & $sh $launcher miblo.js --data $dataArg @args }.GetNewClosure()
  } else {
    $node = Find-MibloNode
    if (-not $node) {
      Write-Host ''
      Write-Host 'Pairing from here needs Node.js 20 or newer (or Git for Windows), and neither was found.'
      Write-Host 'Open Claude Code and type /miblo:pair: it offers to install Node.js for you, then pairs.'
      return
    }
    $script = Join-Path $plugin 'bin\miblo.js'
    $cli = { & $node $script --data $data @args }.GetNewClosure()
  }

  $gadget = Select-MibloGadget $cli
  if (-not $gadget) {
    Write-MibloPairFailed
    return
  }
  Write-Host ''
  Write-Host "Look at the screen of $($gadget.Name): it shows a 4-digit pairing code. (No code? Open its"
  Write-Host 'settings page in a browser and click "Show pairing code on the device".)'
  $tries = 0
  while ($tries -lt 3) {
    $code = Read-Host 'Type the 4-digit code (or q to skip)'
    if ($code -match '^[qQ]$') { break }
    if ($code -notmatch '^\d{4}$') {
      Write-Host 'The code is 4 digits, like 4827.'
      continue
    }
    $out = (@(& $cli pair $gadget.Addr $code) -join "`n")
    if ($out -match '^Paired with (.*) \([^()]*\) at ') {
      $name = $Matches[1]
      if ($name -notlike 'Miblo*') { $name = "Miblo $name" }
      Write-Host ''
      Write-Host "$name paired. Open Claude (terminal, desktop app $([char]0x2192) Code, or your IDE) and it will start showing your sessions."
      Write-Host 'If Claude Code is already open, quit and reopen it first. To see your usage limits on'
      Write-Host 'the Miblo too, type /miblo:link-statusline in Claude Code.'
      return
    }
    if ($out -like 'Wrong pairing code.*') {
      $tries++
      if ($tries -lt 3) { Write-Host 'Wrong code. Check the screen and try again.' } else { Write-Host 'Wrong code, 3 times.' }
      continue
    }
    Write-Host $out
    break
  }
  Write-MibloPairFailed
}

# Native commands write to Out-Host above, so the function returns only its exit code.
$mibloCode = Install-Miblo
# Run as a file: a real exit code. Through `irm | iex`: exiting would close the PowerShell window,
# so the code is left in $LASTEXITCODE instead.
if ($MyInvocation.MyCommand.Path) { exit $mibloCode } else { $global:LASTEXITCODE = $mibloCode }
