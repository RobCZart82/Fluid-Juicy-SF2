## How to install

Fluid Juicy SF2 | English | 15 November 2026

## Before you begin

Development preview: no stable v1.0.0 release is published yet. macOS PKG packaging is in development; Windows EXE packaging is planned. These instructions describe the intended installer workflow, not proof of completed installation testing.

## Downloads and trust

Download only from github.com/RobCZart82/Fluid-Juicy-SF2 Releases (or a development artifact linked by the maintainer). Choose macOS arm64 for Apple Silicon, macOS x86_64 for Intel, or Windows x64. Each installer is intended to contain VST3 and standalone. SoundFonts are not included. Keep the matching source archive and license notices.

## Why a warning appears

Installers do not have a verified publisher signature. macOS apps may have an ad-hoc signature, which does not verify the developer; they are not Apple-notarized. Windows builds are unsigned. A checksum can detect changes against the published value but does not prove that a download is safe.

## macOS: allow this installer

1. Close REAPER and other audio hosts. Open the downloaded PKG.
2. If macOS blocks it because the developer cannot be verified, dismiss the warning.
3. Open System Settings > Privacy & Security. Find the blocked item in Security and choose Open Anyway, if offered.
4. Confirm the exact installer name and authorize only if you trust the download. Follow Installer; system-wide installation requires an administrator. On older macOS the panel may be called System Preferences > Security & Privacy > General. Labels vary by version.

## macOS: launch and scan

The intended locations are /Applications/Fluid_Juicy_SF2.app and /Library/Audio/Plug-Ins/VST3/Fluid_Juicy_SF2.vst3. Open the app from Applications. A separate first-launch warning may require its own Privacy & Security exception. Reopen REAPER and rescan VST3 if needed; select Fluid Juicy SF2 (RobCZart82). Avoid a duplicate older VST3 in ~/Library/Audio/Plug-Ins/VST3/.

## If macOS still refuses

A plug-in may be blocked during the host scan; dismiss the warning, inspect Privacy & Security for the named item, then rescan only after an explicit exception is available. If macOS reports malware, damage, or provides no exception, stop and report the exact message. Do not disable Gatekeeper, SIP or antivirus globally. This guide does not require Terminal quarantine-removal commands.

## Windows installation

Windows x64 | intended EXE workflow

## SmartScreen and UAC

1. Close REAPER and other hosts; open the downloaded installer EXE.
2. If Microsoft Defender SmartScreen says Windows protected your PC, choose More info. If Run anyway is offered, use it only after checking the filename and trusted source.
3. A User Account Control prompt may show Unknown publisher. Approve only for the expected installer if you chose to continue.
4. Follow the installer and include both VST3 and standalone. Windows installer implementation and its exact choices are still pending.

## If continuation is unavailable

Smart App Control, an administrator policy, Windows in S mode or malware protection may prevent execution. There may be no Run anyway option. Do not turn off Defender, SmartScreen or Smart App Control to install this product; ask the device administrator or report the block. A malware detection is different from an unknown-publisher warning.

## After installation

The intended VST3 location is C:\Program Files\Common Files\VST3\Fluid_Juicy_SF2.vst3. Standalone installation location and shortcuts will be finalized with the Windows installer. Reopen REAPER and rescan VST3. If a Visual C++ runtime is required, use Microsoft’s official x64 redistributable; never obtain missing DLLs from download sites.

## First sound

Load your own SF2/SF3 bank using Browse or Banks. Test the standalone with an audio/MIDI device, or load the VST3 in REAPER. For sixteen MIDI parts, route MIDI channels 1-16 into one instance and use 16-part MIDI mode. Start at low listening volume. Save the project and reopen it to check bank recall.

## Support and release limits

Report OS version, CPU architecture, REAPER version, exact build/commit and complete warning text at the repository. Do not post passwords or private files. Clean-machine installation, upgrades and older macOS compatibility still require acceptance tests. These instructions do not guarantee all managed devices can run unsigned software.

## Official references

Apple: https://support.apple.com/en-us/102445
Microsoft: https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation
Project: https://github.com/RobCZart82/Fluid-Juicy-SF2
