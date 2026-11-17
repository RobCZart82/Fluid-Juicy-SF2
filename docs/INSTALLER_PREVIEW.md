# Installer preparation

The first release is planned to ship Windows EXE and macOS PKG installers.
Neither installer is release-approved yet. No SoundFonts are bundled.

## macOS unsigned preview

On macOS, extract a verified development ZIP, then run:

```sh
python3 scripts/build_macos_installer.py /path/to/Fluid-Juicy-SF2 \
  --version 3.1.1 --output /path/to/Fluid-Juicy-SF2-preview.pkg
```

Use the version of the input build. The inherited preview is still 3.1.1;
1.0.0 version migration is a separate release task.

The script verifies signatures and matching arm64/x86_64 architecture, stages
both products and builds an unsigned package plus SHA-256 file. It does not
install anything. Component relocation is disabled so installation targets remain
`/Applications/Fluid_Juicy_SF2.app` and
`/Library/Audio/Plug-Ins/VST3/Fluid_Juicy_SF2.vst3`.
Installation requires administrator authorization. Close hosts before upgrading.
Build records and the project license go into
`/Library/Application Support/Fluid Juicy SF2`.

Keep the original development ZIP, corresponding-source.tar.gz and dependency
notices with the installer. Signature checks establish integrity of the signed
bundle, not publisher trust or authenticity of an ad-hoc signature.

Before release: Developer ID signing/notarization; clean-machine install and
upgrade tests; minimum OS and architecture checks; existing user-level plugin
copies and duplicate scan behavior; Windows EXE implementation and runtime
prerequisite handling. The local PKG payload smoke test is not installation or
REAPER acceptance. No release is published by this script.

## Automated native package checks

Both native macOS workflows now run `check_macos_installer.py` on their verified
ZIP before artifact upload. It extracts the package, reads the app version,
builds the PKG and checks that both executable payload paths and the license
are present. The requested architecture must match both products. The PKG,
SHA-256 sidecar and payload listing are uploaded alongside the ZIP and source
archive. No installer runs on the runner yet: these checks establish package
construction and contents, not successful installation, launch or compatibility
with older macOS versions. Windows packaging is unchanged by this step.

## Windows unsigned preview EXE

The Windows CI now compiles `installers/windows/FluidJuicySF2.iss` using the
runner-installed Inno Setup 6 compiler. The build fails explicitly if ISCC is
unavailable; it does not download an unverified installer compiler. The input
is the verified development ZIP. Both product binaries must be x64 PE32+.

The installer includes the standalone under Program Files/Fluid Juicy SF2, the
complete VST3 bundle under Common Files/VST3, both installation PDFs, project
license and build records. It creates Start menu shortcuts and an uninstaller.
No SoundFonts or user settings are removed by an uninstall entry. Close audio
hosts before installing/upgrading. This installer requires administrator rights,
is unsigned and does not bundle or download the Visual C++ runtime. Missing
runtime prerequisites must be installed from Microsoft, as described in the guide.

The EXE and SHA-256 sidecar join the development artifact with corresponding
source. Compilation does not establish clean-machine installation, upgrades,
uninstallation, standalone launch or REAPER compatibility. Those tests remain
release gates, especially on Windows 10 (the hosted build runner is Server 2022).
The inherited preview version is retained; this is not a v1.0.0 publication.

## Disposable Windows installer lifecycle gate

The Windows workflow runs `check_windows_install.ps1` after compiling the EXE.
It refuses non-GitHub/non-Windows environments and existing product installation
paths. It silently installs, checks both product binaries and language PDFs,
reinstalls the same version, then uninstalls and checks that installed files were
removed. Installer logs and a success record are preserved in the artifact;
failure diagnostics include the installer logs.

This does not test a previous-version upgrade, SmartScreen interaction, clean
Windows 10 runtime dependencies or a live REAPER session. The hosted runner has
development tools and runtimes preinstalled. These remain release acceptance
requirements even when this lifecycle gate passes.

User-reported macOS arm64 acceptance (2026-11-16): the preview PKG installed
successfully and both standalone and plugin worked. This is user feedback, not
an independent Intel/Windows or full 16-part acceptance result. Exact artifact
hash/build attribution still needs to be attached to the acceptance record.

Primary real-bank acceptance priorities: Live HQ Natural SoundFont GM V3.0 and
Tyroland Fixedup10. Timbres of Heaven 4.00 remains a secondary comparison bank.
No bank is redistributed by these installers.
