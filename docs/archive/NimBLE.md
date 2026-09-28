Du kannst den Speicherverbrauch von NimBLE durch Anpassungen in der Konfiguration (sdkconfig) oder im Code erheblich senken. Standardmäßig sind viele Puffer und Limits großzügig eingestellt, was zu den beobachteten ~100kB führen kann.
Hier sind die effektivsten Stellschrauben:
## ⚙️ NimBLE-Konfiguration anpassen (menuconfig)
In den Projekt-Einstellungen unter Component config -> Bluetooth -> NimBLE Options kannst du folgende Werte reduzieren:

* Max Connections: Reduziere CONFIG_BT_NIMBLE_MAX_CONN auf die exakte Anzahl deiner benötigten Verbindungen (oft reicht 1).
* Buffer Sizes: Verkleinere CONFIG_BT_NIMBLE_MSYS_1_BLOCK_COUNT. Dies ist oft der größte Hebel für den Heap.
* GATT-Entitäten: Begrenze CONFIG_BT_NIMBLE_MAX_SERVICES, CONFIG_BT_NIMBLE_MAX_ATTRS und CONFIG_BT_NIMBLE_MAX_CHARACTERISTICS auf das absolute Minimum deiner App.
* Stack-Größe: Prüfe CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE. Standardmäßig liegt dieser oft bei 4-5kB; je nach Komplexität deiner Callbacks reichen eventuell 3kB. [1] 

------------------------------
## 🛠️ Code-Optimierungen

* Unnötige Features deaktivieren: Schalte im [sdkconfig](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/performance/ram-usage.html) Features wie "Privacy support" (CONFIG_BT_NIMBLE_HS_PVCY) oder "L2CAP COC support" aus, falls du sie nicht nutzt.
* Statische vs. Dynamische Allokation: Nutze const für GATT-Profile, damit diese im Flash (statt im RAM) verbleiben.
* Deinitialisierung: Wenn du Bluetooth nicht dauerhaft benötigst, kannst du den Stack mit nimble_port_stop() und nimble_port_deinit() komplett entladen, um den RAM für andere Aufgaben freizugeben. [2, 3, 4] 

------------------------------
## 💾 Hardware-Lösungen

* PSRAM nutzen: Falls dein ESP32 über externen PSRAM verfügt, kannst du NimBLE-Puffer dorthin auslagern. Aktiviere dafür CONFIG_SPIRAM_USE_MALLOC und stelle sicher, dass NimBLE den externen Speicher nutzt.
* Single-Core Modus: Falls möglich, nutze den ESP32 im Single-Core Betrieb. Dies erlaubt es oft, Teile des IRAM als regulären 8-Bit Heap zu verwenden (CONFIG_ESP32_IRAM_AS_8BIT_ACCESSIBLE_MEMORY), was mehr Platz für den Stack schafft. [5, 6] 

💡 Pro-Tipp: Nutze esp_get_free_heap_size() vor und nach der Initialisierung von NimBLE, um den Erfolg deiner Änderungen punktgenau zu messen.
Möchtest du eine Beispiel-Konfiguration (sdkconfig) sehen, die auf minimale Speichernutzung getrimmt ist?

[1] [https://docs.espressif.com](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/performance/ram-usage.html)
[2] [https://docs.espressif.com](https://docs.espressif.com/projects/esp-idf/en/v5.1-rc2/esp32/api-guides/performance/ram-usage.html)
[3] [https://github.com](https://github.com/espressif/esp-idf/blob/master/components/bt/host/nimble/Kconfig.in)
[4] [https://docs.espressif.com](https://docs.espressif.com/projects/esp-idf/en/v5.2/esp32c2/api-guides/performance/ram-usage.html)
[5] [https://docs.espressif.com](https://docs.espressif.com/projects/esp-idf/en/v4.4.6/esp32/api-guides/performance/ram-usage.html)
[6] [https://github.com](https://github.com/h2zero/NimBLE-Arduino/issues/107)

