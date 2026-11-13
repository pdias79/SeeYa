# SeeYa — Architecture et spécification initiale

## Objectif
Firmware ESP32-C3 commandant un relais d'éclairage à partir d'un radar mmWave interchangeable (HLK-LD2410C ou HLK-LD2450), avec Wi-Fi local, interface HTTP, planification hebdomadaire et synchronisation NTP.

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
4. `LightingController` : état logique et temporisation.
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
- Au démarrage, relais OFF par défaut ; après redémarrage, retour en AUTO.
- Absence de trames radar pendant une durée configurée : capteur déclaré défaillant ; ignorer l'ancienne présence et ne jamais laisser un relais allumé indéfiniment sur un état périmé.
- Toute transition du relais est calculée par LightingController, jamais directement par l'API ou les capteurs.
- Priorité : FORCE_ON > automatisations et FORCE_OFF > automatisations ; en AUTO, `relayOn = radarRequest || scheduleRequest`. Les commandes manuelles restent en vigueur jusqu'au retour explicite à AUTO.
- Le calendrier et le radar peuvent chacun être désactivés dans la configuration sans désactiver l'autre.

## API HTTP locale proposée
| Méthode | Route | Effet |
|---|---|---|
| GET | `/api/status` | relais, présence, mode, heure, santé radar et Wi-Fi |
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
  "relayActiveHigh": true,
  "offDelaySeconds": 90,
  "timezone": "Europe/Paris",
  "schedules": [
    { "days": [1,2,3,4,5], "start": "18:00", "end": "23:00", "enabled": true }
  ]
}
```
Les jours sont 1=lundi à 7=dimanche. Hors plages horaires, seule la demande du calendrier est OFF ; la détection radar reste active en permanence. Sans horaires configurés, seule la demande radar intervient en AUTO.

## Matériel et précautions
- LD2410C : alimentation 5 V recommandée, UART TTL 3,3 V, sortie OUT 3,3 V (optionnelle).
- LD2450 : alimentation et niveaux logiques à revérifier sur la version réelle du module ; données de suivi via UART.
- GPIO ESP32-C3 : affectation différée jusqu'à identification exacte de la carte, en évitant les broches de boot et celles affectées à l'USB/UART de programmation.
- Relais : alimentation adaptée, transistor/driver et diode de roue libre si nécessaire ; état OFF garanti pendant le boot ; contrôler la polarité réelle de la carte relais.
- Pour une lampe secteur 230 V, employer un module certifié avec isolation et boîtier adaptés; ne pas câbler le secteur sur une breadboard.

## Découpage de réalisation
1. Base PlatformIO, configuration GPIO et relais, tests unitaires de la logique.
2. Pilote LD2410C, test de l'état de présence.
3. Pilote LD2450, même contrat commun.
4. Wi-Fi, NTP, API et interface Web.
5. Programmation hebdomadaire, sauvegarde NVS et sécurité.
6. Tests de redémarrage, absence Wi-Fi/NTP, capteur hors ligne, changement d'heure et minuterie.

## Points à valider sur le matériel
- Référence précise de la carte ESP32-C3 et GPIO disponibles.
- Référence et tension d'activation du module relais ; alimentation générale.
- Programmation confirmée : le calendrier force une demande ON pendant chaque plage, indépendamment de la détection, avec combinaison OU en mode AUTO.
- Politique de sécurité si NTP ou radar ne répondent plus.
