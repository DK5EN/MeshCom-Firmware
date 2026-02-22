# How to Create a Release

## 1. Build the Firmware

```bash
pio run -e heltec_wifi_lora_32_V3
```

The binary will be at `.pio/build/heltec_wifi_lora_32_V3/firmware.bin`.

## 2. Copy and Rename the Binary

```bash
cp .pio/build/heltec_wifi_lora_32_V3/firmware.bin heltec_wifi_lora_32_V3.bin
```

## 3. Generate SHA256 Checksum

```bash
shasum -a 256 heltec_wifi_lora_32_V3.bin
```

Save the output hash for the release notes.

## 4. Commit and Push

```bash
git add heltec_wifi_lora_32_V3.bin
git commit -m "Update firmware binary for release"
git push
```

## 5. Create a Tag

```bash
git tag <version>
git push origin <version>
```

Example: `git tag 4.35k.02.19-DK5EN`

## 6. Create the GitHub Release

```bash
gh release create <version> heltec_wifi_lora_32_V3.bin \
  --target lora-improve \
  --title "<version>" \
  --notes "## MeshCom Firmware <version>

Heltec WiFi LoRa 32 V3 firmware binary.

### SHA256 Checksum
\`\`\`
<hash>  heltec_wifi_lora_32_V3.bin
\`\`\`"
```

Replace `<version>` with the release tag and `<hash>` with the SHA256 output from step 3.
