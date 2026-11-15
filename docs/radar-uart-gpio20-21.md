# Câblage UART des radars — SeeYa 0.6.6-dev

Les radars LD2410C et LD2450 sont connectés à l'UART1 (256000 bauds) de l'ESP32-C3, routé sur GPIO20/21.

| ESP32-C3 SuperMini | Sens | Radar |
|---|---|---|
| GPIO20 | UART RX, entrée ESP | TX radar |
| GPIO21 | UART TX, sortie ESP | RX radar |
| GND | Masse commune | GND radar |
| Alimentation | Selon les spécifications du module | VCC radar |

GPIO20 et GPIO21 correspondent aux signaux UART0 par défaut du circuit ESP32-C3, mais dans SeeYa ils sont affectés explicitement à **HardwareSerial(1)** via la matrice GPIO. La console `Serial` via USB CDC natif (GPIO18/19) reste distincte avec les paramètres PlatformIO actuels (`ARDUINO_USB_CDC_ON_BOOT=1`).

Attention : ces broches sont également celles de l'UART0 natif et peuvent être utilisées par certains adaptateurs série externes. Ne connectez pas simultanément le radar et un adaptateur UART0 externe sur ces fils. Les messages de démarrage ROM peuvent également être présents sur GPIO21 avant que le firmware ne réaffecte les broches.

## Essais

1. Débrancher l'alimentation avant de déplacer les deux fils du radar.
2. Brancher TX radar sur GPIO20, RX radar sur GPIO21, garder la masse commune.
3. Installer le firmware 0.6.6-dev par USB ou OTA.
4. Lire le diagnostic LD2450 ou LD2410C sur le moniteur USB CDC et vérifier `link=OK`.
5. Vérifier le bon fonctionnement du panneau graphique LD2450 et l'absence de régression des temporisations.
