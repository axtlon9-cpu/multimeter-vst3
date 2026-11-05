# Multimeter VST3 (Windows x64)

Stereo meter VST3 built with JUCE. Audio passes through unchanged. The current implementation displays left/right peak, RMS and stereo correlation.

## Build
GitHub Actions builds the Windows x64 VST3 and creates `Multimeter-Setup-x64.exe`. Open the repository's **Actions** tab, select the latest successful run, and download the `Multimeter-Windows-x64` artifact.

## Install
Run `Multimeter-Setup-x64.exe`. It installs the plugin under the system VST3 folder:
`C:\Program Files\Common Files\VST3\Multimeter.vst3`
Then rescan plugins in your DAW.

## Important scope note
This is a clean-room starter meter, not a binary conversion of a Max for Live `.amxd`. It does not reproduce the original device's DSP, controls, spectrum display, loudness standards, or visual design. Those require the original AMXD source/device to inspect and port.
