## Auto Update assets (AU)

Since AU (#1187) every release carries a compressed twin of each ESP32 app image
(AU-D11) and every build embeds its release tag (AU-D16). Two additions to the flow
above, plus a check.

**1. Export `MC_BUILD_TAG` before the Step 3 builds.** Use the tag exactly as it
will be pushed (`v<VER>.MM.DD[.N]`, for example `v4.40a.10.02`), in the same shell
that runs `pio run`:

```
export MC_BUILD_TAG=<tag>
pio run -e ... (the full env list)
```

- `tools/mc_build_defines.py` turns it into the `MC_BUILD_TAG` macro. Without it the
  image reports an undated version and the reinstall guard (`fwShouldInstall`) cannot
  tell it from any later same-letter release.
- Changing `MC_BUILD_TAG` changes the compiler flags of every translation unit, so
  PlatformIO rebuilds every env completely. Budget the full ~16 min and never
  change the tag between envs of one release.
- Detached builds (`nohup bash -c ...`) inherit the variable only if it is exported
  first; put `export MC_BUILD_TAG=<tag>` inside the `bash -c` string.
- Check one image afterwards: `strings .pio/build/heltec_wifi_lora_32_V3/firmware.bin | grep -F '<tag>'`.

**2. Compress the 27 ESP32 app images in Step 5.** Run it in the staging directory
on the already renamed `.bin` files, so the `.zz` names follow the `.bin` names
(`<env-or-renamed>.bin.zz`: `T-Beam-1W.bin.zz`, `T3_S3_V13.bin.zz`,
`t_connect_pro.bin.zz`, ...). Only the 27 app images, never the support files
(`bootloader*.bin`, `partitions.bin`, `otadata.bin`, `safeboot*.bin`) and never the
nRF52 outputs:

```
python3 tools/make_zz.py --check --manifest au-zz-manifest.json \
  $(ls <staging-dir>/*.bin | grep -v -E '/(bootloader|bootloader-s3|partitions|otadata|safeboot|safeboot-s3)\.bin$')
```

The tool refuses any input whose first byte is not `0xE9`, so a support file in the
list fails loudly instead of being compressed. `--check` re-inflates every output
and compares it byte-exact with its input. Keep `au-zz-manifest.json` out of the
upload; it is for the release notes (sizes, ratios, hashes).

**3. Upload the `.bin.zz` assets next to the `.bin` ones.** Same `gh release
create` / `gh release upload` call as the other assets; the asset name is the
`.bin` name plus `.zz`. The release now has 39 + 27 = 66 assets (update the count
in the Step 7 verify).

**4. Verify before publishing.**

```
ls <staging-dir>/*.bin.zz | wc -l          # must equal the number of ESP32 app .bin (27)
python3 tools/make_zz.py --check <staging-dir>/<app>.bin ...   # again, exit status 0
```

- The count of `.bin.zz` must equal the count of ESP32 app `.bin` files (27); the
  name list stays diff-identical to the previous release apart from the 27 new
  `.zz` names.
- Every `.zz` begins with the bytes `78 da` (`xxd -l2 <file>.zz`).
- Compressed size is about 64 % of the image (Heltec V3: 1,560,864 B to 993,337 B), so
  expect each `.zz` well below its `.bin`; a `.zz` as large as its `.bin` means the
  input was already compressed or wrong.
