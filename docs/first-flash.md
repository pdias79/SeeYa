# Premier flash et OTA (jalon 0.2.0-dev)
1. Installer VS Code et l'extension PlatformIO.
2. Cloner le dépôt et ouvrir le dossier.
3. Copier `include/secrets.example.h` vers `include/secrets.h`. Renseigner le SSID Wi-Fi 2,4 GHz, son mot de passe et un mot de passe d'administration **unique** d'au moins 12 caractères.
4. **Débrancher toute charge secteur et tout relais pour le premier essai** : brancher la SuperMini en USB-C.
5. Depuis PlatformIO : Build puis Upload, et ouvrir le moniteur série à 115200 bauds. Vérifier l'adresse IP depuis le routeur DHCP si besoin.
6. Visiter `http://<ip-esp>/`, se connecter avec l'utilisateur `admin` et le mot de passe configuré.
7. Pour les mises à jour suivantes : PlatformIO Build, puis envoyer `.pio/build/esp32-c3-supermini/firmware.bin` depuis la page Web.

## Limites de ce jalon
- Pas encore de portail de configuration Wi-Fi ; les identifiants réseau sont définis à la compilation initiale.
- Pas encore de radar, d'horaires ou de gestion avancée des zones. Le mode AUTO garde la sortie éteinte. Une prise HTTP simple (GET/POST, sans authentification) peut être configurée pour le prototypage.
- Commandes et mise à jour en HTTP Basic **sans chiffrement** : usage strictement sur un LAN de confiance, ne jamais exposer directement vers Internet. Un équipement hostile sur le réseau peut intercepter les identifiants. Une sécurisation ultérieure est nécessaire.
- OTA est prévue par deux partitions. Le rollback automatique après un boot défaillant n'est pas encore implémenté.
- Flash USB indispensable si erreur de Wi-Fi/firmware, partitionnement à changer, ou appareil inaccessible.
- Sur certaines cartes, la LED GPIO8 peut ne pas être une LED simple ou peut affecter le démarrage. Vérifier la variante matérielle avant usage.
- Sortie désactivée par défaut. Pour tester un relais, sélectionner explicitement GPIO7 (ou un autre GPIO autorisé) et sa polarité depuis l'interface. Vérifier au multimètre avant de connecter une charge.
