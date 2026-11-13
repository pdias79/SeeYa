# SeeYa — Architecture et spécification initiale

## Objectif
Firmware ESP32-C3 commandant une sortie d'éclairage (relais local ou prise connectée via HTTP) à partir d'un radar mmWave interchangeable (HLK-LD2410C ou HLK-LD2450), avec Wi-Fi local, interface HTTP, planification hebdomadaire et synchronisation NTP.

## Stack proposée
- Framework Arduino pour ESP32-C3, projet PlatformIO (à confirmer selon la carte exacte).
- UART matériel (256000 bauds par défaut) pour les radars; aucun lien UART avec le relais.
- Serveur HTTP local, fichiers d'interface en LittleFS.
- Préférences persistantes dans NVS (configuration), JSON pour règles horaires si nécessaire.
- Horloge locale Europe/Paris (gestion heure d'été/hiver par TZ POSIX), synchronisation SNTP.
- Boucle non bloquante avec `millis()` pour les délais, sans utiliser `delay()` dans la logique principale.

## Modules logiques
1. `PresenceSensor` : contrat `begin()`, `update()`, `isPresent()`, `isHealthy()`.
2. `LD2410Sensor` : parseur UART et présence fixe/mobile.
3. `LD2450Sensor` : parseur UART, suivi de cibles et filtrage de zones (phase 2).
4. `LightingController` : décision d'allumage et temporisation, sans dépendance du matériel de sortie.
   - `LightOutput` : interface `begin()`, `requestState(bool)`, `update()`, `getStatus()`.
   - `GpioRelayOutput` : relais local, GPIO et polarité de commande paramétrables.
   - `HttpSwitchOutput` : envoi de requêtes ON/OFF vers une prise distante, transport non bloquant.
5. `ScheduleManager` : règles par jour et plage horaire, y compris passage à minuit.
6. `ClockService` : NTP, fuseau horaire, validité de l'heure et resynchronisation.
7. `WebServer` : interface d'administration et API JSON.
8. `ConfigStore` : validation et persistance de configuration.
9. `WifiManager` : connexion STA, reconnexion et mode d'accès provisoire lors de la première installation.

## Sémantique proposée
- Mode AUTO : deux automatismes indépendants calculent leurs demandes d'allumage : (a) le radar avec son délai d'extinction, (b) le calendrier hebdomadaire qui demande ON pendant ses plages horaires. Le relais est ON si et seulement si l'un des deux demande ON.
- Quand la présence disparaît, le relais reste allumé pendant le délai configuré, puis s'éteint. La présence renouvelée réarme le délai.
- Mode FORCE_ON et FORCE_OFF : ordre manuel explicite prioritaire sur radar et calendrier. Retour à AUTO par commande HTTP ou éventuellement expiration de l'override (option future).
- Plages horaires : elles pilotent directement une demande d'allumage, indépendamment du radar. À la fin de la plage, la demande du calendrier passe à OFF, sans empêcher la demande radar de maintenir le relais allumé. Les règles multiples se combinent par OU (une plage active suffit).
- Si l'heure n'est pas fiable au démarrage (NTP indisponible et aucune heure conservée valide), seule la demande du calendrier est désactivée ; la détection radar demeure fonctionnelle ainsi que les commandes manuelles.
- Au démarrage, état demandé OFF par défaut ; après redémarrage, retour en AUTO. Un relais local doit rester physiquement OFF pendant l'initialisation ; attention aux niveaux électriques des GPIO durant le boot.
- Absence de trames radar pendant une durée configurée : capteur déclaré défaillant ; ignorer l'ancienne présence et ne jamais laisser un relais allumé indéfiniment sur un état périmé.
- Toute transition de la sortie d'éclairage est décidée par LightingController, jamais directement par l'API ou les capteurs.
- Priorité : FORCE_ON > automatisations et FORCE_OFF > automatisations ; en AUTO, `lightRequest = radarRequest || scheduleRequest`. Les commandes manuelles restent en vigueur jusqu'au retour explicite à AUTO.
- Le calendrier et le radar peuvent chacun être désactivés dans la configuration sans désactiver l'autre.

## API HTTP locale proposée
| Méthode | Route | Effet |
|---|---|---|
| GET | `/api/status` | état demandé, état observé ou inconnu, mode, présence, heure, santé radar, Wi-Fi et sortie |
| GET | `/api/config` | paramètres non secrets |
| PUT | `/api/config` | modifier durée, type radar, options |
| GET | `/api/schedules` | consulter les plages horaires |
| PUT | `/api/schedules` | remplacer/valider la programmation hebdomadaire |
| POST | `/api/light/on` | forcer ON |
| POST | `/api/light/off` | forcer OFF |
| POST | `/api/light/auto` | retour au mode automatique |

Prévoir des alias GET simples (par ex. `/api/light?state=on`) uniquement si demandé ; GET ne devrait normalement pas changer l'état. Recommandation : jeton ou authentification pour tous les points de modification, uniquement réseau local, jamais d'exposition directe sur Internet.

## Format de configuration (illustratif)
```json
{
  "sensor": "LD2410C",
  "output": {
    "type": "gpio",
    "gpio": 7,
    "activeHigh": false,
    "led": { "enabled": true, "gpio": 8, "activeHigh": false }
  },
  "offDelaySeconds": 90,
  "timezone": "Europe/Paris",
  "schedules": [
    { "days": [1,2,3,4,5], "start": "18:00", "end": "23:00", "enabled": true }
  ]
}
```
Les jours sont 1=lundi à 7=dimanche. Hors plages horaires, seule la demande du calendrier est OFF ; la détection radar reste active en permanence. Sans horaires configurés, seule la demande radar intervient en AUTO.

## Contrôle d'éclairage : choix de sortie
La **demande logique** est commune aux deux sorties : la détection radar, les horaires et les commandes HTTP entrantes ne dépendent pas du moyen de commande de la lumière.

### Relais GPIO local
- Type de sortie `gpio`, broche paramétrable (GPIO7 par défaut sous réserve de confirmation du matériel).
- Polarité paramétrable `activeHigh` : `true` = ON en niveau haut, `false` = ON en niveau bas (relais « active LOW »).
- À chaque changement de configuration, calculer le niveau inactif **avant** de basculer la broche en sortie afin de réduire les impulsions parasites. L'absence d'impulsion lors du boot dépend aussi du schéma électronique et ne peut pas être garantie par logiciel seul.
- Ne pas permettre l'affectation de GPIO réservés à l'USB, au radar ou à une broche de boot risquée sans validation.

### LED embarquée
- Sur de nombreuses cartes ESP32-C3 SuperMini, la LED utilisateur bleue est reliée à GPIO8 en actif bas ; ceci doit être vérifié sur la variante réelle (certaines cartes ont des LED différentes, notamment LED d'alimentation ou RGB).
- Configuration indépendante : `led.enabled`, `led.gpio`, `led.activeHigh`.
- La LED représente la **demande d'allumage** : LED allumée lorsque la sortie est demandée ON, éteinte sinon ; même comportement en mode relais et prise HTTP.
- Attention : GPIO8 est souvent une broche de strapping ; sa réutilisation est limitée à une LED pré-câblée et ne doit pas compromettre le démarrage.
- Pour une prise distante, l'état demandé ne prouve pas que la prise a commuté : l'interface distingue `requestedState`, `lastCommandResult` et `confirmedState` lorsque la prise fournit un retour d'état ; sinon état confirmé inconnu.

### Prise connectée HTTP distante
- Type de sortie `http`, relais local désactivé et placé à son niveau sûr.
- Deux actions configurables séparément : ON et OFF ; chacune possède URL, méthode HTTP (GET/POST/PUT), en-têtes facultatifs et corps facultatif pour les API JSON.
- Identifiants/jetons stockés séparément de la configuration publique, masqués dans les réponses de l'API ; HTTPS avec validation de certificat si utilisé.
- Déclenchement uniquement sur changement de demande (et resynchronisation contrôlée si nécessaire), pas à chaque itération de la boucle principale.
- Timeout borné, nombre de tentatives plafonné avec temporisation/backoff ; aucune attente bloquante qui empêche la lecture UART, le web ou la temporisation.
- Perte de Wi-Fi : enregistrer l'état demandé, déclarer la commande non confirmée et réconcilier avec la prise lors de la reconnexion, sans répéter une commande en continu.
- En cas d'erreur HTTP, ne pas prétendre que la lampe est physiquement dans l'état demandé. Possibilité d'interroger une URL de retour d'état, selon l'API de la prise.
- Accès à une prise compatible HTTP local ; les prises reposant uniquement sur un cloud ou un protocole propriétaire nécessiteront un adaptateur spécifique.

Exemple de configuration pour une sortie HTTP (URL fictives) :
```json
{
  "output": {
    "type": "http",
    "on": {"method": "POST", "url": "http://192.168.1.50/api/on"},
    "off": {"method": "POST", "url": "http://192.168.1.50/api/off"},
    "timeoutMs": 1500,
    "maxRetries": 2,
    "led": {"enabled": true, "gpio": 8, "activeHigh": false}
  }
}
```

## Matériel et précautions
- LD2410C : alimentation 5 V recommandée, UART TTL 3,3 V, sortie OUT 3,3 V (optionnelle).
- LD2450 : alimentation et niveaux logiques à revérifier sur la version réelle du module ; données de suivi via UART.
- GPIO ESP32-C3 : affectation différée jusqu'à identification exacte de la carte, en évitant les broches de boot et celles affectées à l'USB/UART de programmation.
- Relais : alimentation adaptée, transistor/driver et diode de roue libre si nécessaire ; ne pas supposer que le boot garantit OFF sans circuit externe ; contrôler la polarité réelle du module et l'absence d'impulsions au démarrage.
- Pour une lampe secteur 230 V, employer un module certifié avec isolation et boîtier adaptés; ne pas câbler le secteur sur une breadboard.

## Découpage de réalisation
1. Base PlatformIO, configuration GPIO et relais, tests unitaires de la logique.
2. Pilote LD2410C, test de l'état de présence.
3. Pilote LD2450, même contrat commun.
4. Wi-Fi, NTP, API et interface Web.
5. Programmation hebdomadaire, sauvegarde NVS et sécurité.
6. Tests de redémarrage, absence Wi-Fi/NTP, capteur hors ligne, changement d'heure et minuterie.


## Carte de développement retenue (provisoire)
- ESP32-C3 SuperMini (format environ 18 x 23 mm, USB-C), d'après photographie fournie.
- Affectation GPIO proposée : GPIO4 = RX radar (relié à TX du radar), GPIO5 = TX radar (relié à RX du radar), GPIO7 = commande relais.
- Masse commune, alimentation radar à vérifier suivant module; ne pas alimenter la bobine d'un relais directement par le GPIO.
- GPIO2, GPIO8, GPIO9 sont des broches de strapping à éviter pour ce projet ; GPIO8 porte généralement une LED embarquée et GPIO9 le bouton BOOT. GPIO18/19 restent réservées à l'USB natif.
- Les valeurs de tension, la polarité du relais et la disponibilité d'une alimentation 5 V suffisante restent à valider avant branchement.

## Points à valider sur le matériel
- Confirmer le brochage exact de la variante SuperMini en possession (les clones diffèrent).
- Référence et tension d'activation du module relais ; alimentation générale et polarité réglable.
- Confirmer la LED réellement pilotable sur la carte et son niveau actif.
- Définir l'API HTTP de la prise distante retenue, notamment s'il existe un retour d'état.
- Programmation confirmée : le calendrier force une demande ON pendant chaque plage, indépendamment de la détection, avec combinaison OU en mode AUTO.
- Politique de sécurité si NTP ou radar ne répondent plus.
