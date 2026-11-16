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
