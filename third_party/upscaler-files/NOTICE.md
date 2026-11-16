# Upscaler runtime files

The release workflow unpacks these archives into the `userdata` folder of each zip. GR2port loads
the files from there at run time; the build does not use them.

- `dlss.tar.xz` unpacks to `userdata/dlss` in the Windows zip: `sl.interposer.dll`,
  `sl.common.dll` and `sl.dlss.dll` from `bin/x64` of NVIDIA Streamline 2.14.16, and
  `nvngx_dlss.dll` 310.9.16 (DLSS Super Resolution). Streamline is MIT-licensed
  (`third_party/streamline/license.txt`); `nvngx_dlss.dll` is NVIDIA's, under the license of the
  NVIDIA DLSS SDK.
- `xess.tar.xz` unpacks to `userdata/xess` in the Windows zip: `libxess.dll` from `bin` of the
  Intel XeSS SDK 3.0.2 (XeSS Super Resolution), with the SDK's `LICENSE.txt` as
  `LICENSE-XeSS.txt` and its `third-party-programs.txt`. `libxess.dll` is Intel's, under the Intel
  Simplified Software License in `LICENSE-XeSS.txt`. The XeSS headers in `third_party/xess` are
  MIT-licensed (`third_party/xess/MIT.txt`).
- `fsr4_v07.tar.xz` unpacks to `userdata/fsr4_v07` in both zips: the shaders and model data of
  FSR 4 v07 INT8 that `third_party/fsr-vulkan` runs. `LICENSE-FSR4-v07.txt` in the archive gives
  their origin and MIT license.
