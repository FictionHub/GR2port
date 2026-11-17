# Upscaler runtime files

The release workflow unpacks these archives into the `userdata` folder of each zip. GR2port loads
the files from there at run time; the build does not use them.

- `dlss-windows.tar.xz` and `dlss-linux.tar.xz` unpack to `userdata/dlss` in the Windows and the
  Linux zip: `nvngx_dlss.dll` and `libnvidia-ngx-dlss.so.310.9.16` from `lib/<platform>/rel` of
  the NVIDIA DLSS SDK 310.9.16 (DLSS Super Resolution). They are NVIDIA's, under the NVIDIA RTX
  SDKs license in `third_party/dlss/LICENSE.txt`, which each zip carries as `LICENSE-DLSS.txt`.
  `third_party/dlss` has the SDK's NGX headers and NGX libraries that GR2port links.
- `xess.tar.xz` unpacks to `userdata/xess` in the Windows zip: `libxess.dll` from `bin` of the
  Intel XeSS SDK 3.0.2 (XeSS Super Resolution), with the SDK's `LICENSE.txt` as
  `LICENSE-XeSS.txt` and its `third-party-programs.txt`. `libxess.dll` is Intel's, under the Intel
  Simplified Software License in `LICENSE-XeSS.txt`. The XeSS headers in `third_party/xess` are
  MIT-licensed (`third_party/xess/MIT.txt`).
- `fsr4_v07.tar.xz` unpacks to `userdata/fsr4_v07` in both zips: the shaders and model data of
  FSR 4 v07 INT8 that `third_party/fsr-vulkan` runs. `LICENSE-FSR4-v07.txt` in the archive gives
  their origin and MIT license.
