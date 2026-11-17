# Disposable GitHub runner only: exercises system-wide install and uninstall.
param([Parameter(Mandatory=$true)][string]$Distribution)
$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_OS -ne 'Windows') {
    throw 'This test may only run on a disposable Windows GitHub Actions runner.'
}
$setup = Join-Path $Distribution 'Fluid-Juicy-SF2-windows-x64-preview-setup.exe'
$app = Join-Path $env:ProgramFiles 'Fluid Juicy SF2'
$plugin = Join-Path $env:CommonProgramFiles 'VST3\Fluid_Juicy_SF2.vst3'
if ((Test-Path $app) -or (Test-Path $plugin)) { throw 'Existing product installation: refusing test' }
function Invoke-Installer([string]$Executable, [string]$Log) {
    $process = Start-Process -FilePath $Executable -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=`"$Log`"") -Wait -PassThru
    if ($process.ExitCode -ne 0) { throw "Installer failed: $($process.ExitCode), see $Log" }
}
Invoke-Installer $setup (Join-Path $Distribution 'windows-install.log')
$required = @(
    (Join-Path $app 'Fluid_Juicy_SF2.exe'),
    (Join-Path $plugin 'Contents\x86_64-win\Fluid_Juicy_SF2.vst3'),
    (Join-Path $app 'Documentation\Fluid-Juicy-SF2-Installation-EN.pdf'),
    (Join-Path $app 'Documentation\Fluid-Juicy-SF2-Installation-HU.pdf'),
    (Join-Path $app 'LICENSE.txt'),
    (Join-Path $app 'unins000.exe')
)
foreach ($file in $required) { if (!(Test-Path $file)) { throw "Missing installed file: $file" } }
# Same-version reinstall exercises overwrite behavior without requiring an older release.
Invoke-Installer $setup (Join-Path $Distribution 'windows-reinstall.log')
foreach ($file in $required) { if (!(Test-Path $file)) { throw "Missing after reinstall: $file" } }
Invoke-Installer (Join-Path $app 'unins000.exe') (Join-Path $Distribution 'windows-uninstall.log')
foreach ($file in $required) { if (Test-Path $file) { throw "File left after uninstall: $file" } }
'PASS: install, required payload, same-version reinstall and uninstall' |
    Set-Content (Join-Path $Distribution 'windows-install-acceptance.txt')
