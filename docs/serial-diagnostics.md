# Diagnostic radar dans PlatformIO

Le moniteur série **USB** de l'ESP32-C3 utilise **115200 bauds**. Les radars sont lus séparément sur **UART1 à 256000 bauds**, avec GPIO4 = RX ESP32 (TX radar) et GPIO5 = TX ESP32 (RX radar). Les broches GPIO20/21 ne sont pas utilisées pour le radar dans cette version.

## Lancer le moniteur
Connecter la SuperMini en USB au PC et ouvrir PlatformIO > **Monitor** ou lancer `pio device monitor -e esp32-c3-supermini`. Si la carte est flashée par OTA, le câble USB doit quand même être branché pour lire les logs série. Le filtre PlatformIO `time` ajoute des horodatages.

Le moniteur affiche toutes les 2 secondes des données limitées pour ne pas saturer le port USB.

### LD2410C
```text
[LD2410C] t=12345 ms UART=2180 bytes frames=94 link=OK state=3 presence=YES moving=115 cm (E=72) stationary=98 cm (E=45)
```
`state` : 0 aucune cible, 1 mouvement, 2 stationnaire, 3 les deux. Distances en cm, énergies indicatives.

### LD2450
```text
[LD2450] t=12345 ms UART=3000 bytes frames=100 link=OK targets=2 presence=YES zone=ON
  target1 X=-450 mm Y=2300 mm speed=12 cm/s resolution=120 mm in_zone=YES
  target2 X=1600 mm Y=2800 mm speed=0 cm/s resolution=150 mm in_zone=NO
```

- `UART=0 bytes` : aucune donnée série reçue : vérifier alimentation, masse commune, liaison TX radar vers GPIO4, débit et sélection du radar.
- `UART>0` mais `frames=0` : des octets arrivent, mais aucune trame valide du protocole sélectionné n'est décodée ; vérifier le modèle et le câblage.
- `link=NO_VALID_FRAME` : pas de trame valide dans les 2,5 dernières secondes, même si des trames avaient été reçues auparavant.
- `targets=0` et `link=OK` : communication LD2450 valide, mais aucune cible remontée.
- `in_zone=NO` : cible identifiée mais ignorée par le filtre spatial.
- Si le radar est désactivé, il n'y a pas de journal radar périodique.
- Le compteur de bytes est remis à zéro quand on sélectionne un nouveau capteur.
- Les valeurs de positions sont celles décodées par le firmware, à valider avec le module réel.

Les données sont produites localement par l'ESP32 et affichées sur USB ; ce ne sont pas des logs accessibles par le réseau. L'ouverture du moniteur n'est pas nécessaire au fonctionnement du radar.
