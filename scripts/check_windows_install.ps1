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
$archives = @(Get-ChildItem -LiteralPath $Distribution -Filter '*.zip' -File)
if ($archives.Count -ne 1) { throw 'Expected one verified development ZIP' }
$expected = @{}
$archive = [IO.Compression.ZipFile]::OpenRead($archives[0].FullName)
try {
    foreach ($entry in $archive.Entries) {
        if (!$entry.Name -or !$entry.FullName.StartsWith('Fluid-Juicy-SF2/')) { continue }
        $relative = $entry.FullName.Substring('Fluid-Juicy-SF2/'.Length)
        if ($relative.StartsWith('Fluid_Juicy_SF2.vst3/')) {
            $target = Join-Path $plugin ($relative.Substring('Fluid_Juicy_SF2.vst3/'.Length).Replace('/', [IO.Path]::DirectorySeparatorChar))
        } elseif ($relative -in @('Fluid_Juicy_SF2.exe', 'LICENSE.txt', 'build-record.json', 'binary-verification.json')) {
            $target = Join-Path $app $relative
        } elseif ($relative -like 'Fluid-Juicy-SF2-Installation-*.pdf') {
            $target = Join-Path (Join-Path $app 'Documentation') $relative
        } else { continue }
        $stream = $entry.Open()
        try { $expected[$target] = (Get-FileHash -InputStream $stream).Hash }
        finally { $stream.Dispose() }
    }
} finally { $archive.Dispose() }
$required = @(
    (Join-Path $app 'Fluid_Juicy_SF2.exe'),
    (Join-Path $plugin 'Contents\x86_64-win\Fluid_Juicy_SF2.vst3'),
    (Join-Path $app 'Documentation\Fluid-Juicy-SF2-Installation-EN.pdf'),
    (Join-Path $app 'Documentation\Fluid-Juicy-SF2-Installation-HU.pdf'),
    (Join-Path $app 'LICENSE.txt')
)
foreach ($file in $required) {
    if (!$expected.ContainsKey($file)) { throw "Missing expected ZIP entry: $file" }
}
$uninstaller = Join-Path $app 'unins000.exe'
foreach ($phase in @('install', 'reinstall')) {
    Invoke-Installer $setup (Join-Path $Distribution "windows-$phase.log")
    foreach ($file in $expected.Keys) {
        if (!(Test-Path -LiteralPath $file -PathType Leaf) -or
            (Get-FileHash -LiteralPath $file).Hash -ne $expected[$file]) {
            throw "${phase}: Installed file missing or differs from development ZIP: $file"
        }
    }
    if (!(Test-Path -LiteralPath $uninstaller -PathType Leaf)) { throw 'Missing uninstaller' }
}
Invoke-Installer $uninstaller (Join-Path $Distribution 'windows-uninstall.log')
foreach ($file in @($expected.Keys) + @($uninstaller)) {
    if (Test-Path -LiteralPath $file) { throw "File left after uninstall: $file" }
}
'PASS: install, SHA-256 payload verification, same-version reinstall and uninstall' |
    Set-Content (Join-Path $Distribution 'windows-install-acceptance.txt')
