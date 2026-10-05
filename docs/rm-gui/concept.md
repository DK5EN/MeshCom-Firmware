# Concept under review: web GUI for RM (remote management) -- MeshCom firmware, repo /Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main (fork-dev)

USER REQUIREMENT (verbatim intent): a comfortable GUI that enables remote management with ease.
Console `--` commands are NOT an option, because somebody would have to remember them. Humans are
visual and button oriented: they need a supporting GUI, "like a McDonald's menu" (pick from pictured
choices, no typing of command syntax).

CONCEPT (nothing is coded yet):

- New sidebar page "Remote" (own menu entry, route /?page=remote, sub_page_remote()). The current
  "Remote management" card is REMOVED from the Setup page (src/web_functions/web_functions.cpp
  ~3271-3330, JS helpers in the scaffold ~1298-1330, CSS ~1491, endpoints POST /rmsend and GET /rmstatus
  ~lines 538-760). Manual Command box on Setup stays.
- Block "This node": RM on/off switch + status; a dedicated write-only password field with Set / Clear
  (replaces typing `--passwd <pwd>`; password travels in a POST body, request echo suppressed like /rmsend);
  shows only "password set: yes/no".
- Block "Known nodes (max 3)": call sign + password per slot, save/delete. Only the DERIVED KEY
  SHA-256(password) is stored (new small persistent store: ESP32 NVS namespace, nRF52 two alternating
  LittleFS files, saved from the loop task only); password write-only, never sent back to the browser.
  Config export, --info, BLE dumps, logs must not include them.
- Block "Manage another node": dropdown of the 3 saved nodes + ad-hoc entry; for a saved node the browser
  sends only the slot number. Existing command <select> (reboot, status, sendpos, sendtrack, gps/track/
  display/gateway/mesh on|off, led on|off, txpower, setout, sync), Send and Sync-counter buttons, "last
  sent commands" and "last executed commands" tables stay.
- Needs a key-based entry point next to rmSendCommand() (today it takes a clear-text password and keeps
  a derived key 10 min to verify the reply).
- Web GUI runs on ESP32 (WiFi) and nRF52 RAK4631 (Ethernet). Scripts inside injected sub-pages never run
  (innerHTML), so JS must live in the scaffold. HTTP is plaintext; page protected only by node_webpwd if set.
- RM protocol: docs/adr-remote-hmac.md; allowlist src/remote_cmd.cpp allowed() and tools/remote_cmd.py ALLOWLIST;
  runtime src/rm_runtime.cpp; sender side rmSendCommand in src/rm_runtime.cpp / remote_cmd.cpp.

Screenshot facts of the CURRENT card (what the user dislikes): target call text field, target password
text field, command <select> with raw strings ("led on", "txpower", "setout"), Send + Sync counter buttons,
terse error line "target call and password needed", footnote about HTTP/derived key.
