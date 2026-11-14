# Événements hebdomadaires et Wi-Fi de secours — SeeYa 0.6

## Programmateur à événements
SeeYa propose **10 événements**. Chacun comprend :
- Action : Désactivé / Allumer / Éteindre.
- Heure locale HH:MM.
- Jours cochés parmi L M M J V S D, à partir du lundi.

À chaque instant, le dernier événement ON/OFF applicable sur la semaine (y compris la semaine précédente) définit l'état du programmateur. Cet état reste actif jusqu'à l'événement suivant ; les événements désactivés ou sans jour coché sont ignorés. Si deux événements coïncident, **le numéro de slot le plus élevé est prioritaire**.

En mode AUTO : **radarHold OU programmateur ON**. Un événement OFF n'éteint donc pas une lampe si le radar détecte encore une présence ou si sa temporisation court. FORCE_ON/FORCE_OFF restent prioritaires. Avant synchronisation NTP, le programmateur ne commande pas la sortie. Fuseau CET/CEST Europe/Paris.

### Stockage et migration
Nouvelle configuration sauvegardée en NVS sous les clés evt0..evt9. Les anciennes plages quotidiennes v0.5/v0.6 ne sont PAS converties automatiquement, car un ordre d'événements ne reproduit pas nécessairement les anciennes plages et leurs chevauchements. Les anciennes clés demeurent dans la NVS, mais sont ignorées.

## Wi-Fi domestique
- SSID, mot de passe et nom de réseau de secours configurables dans l'interface Web.
- Valeurs initiales du Wi-Fi domestique lues dans `include/secrets.h` si aucune préférence NVS n'existe ; ensuite les données saisies via Web prévalent.
- Le mot de passe déjà enregistré est conservé lorsque le champ mot de passe est laissé vide dans l'interface principale, si le SSID ne change pas. Dans le portail de secours, le mot de passe saisi remplace toujours l'ancien (vide = réseau ouvert).
- Le mot de passe Wi-Fi n'est jamais renvoyé par l'API.
- Si le STA ne se connecte pas dans ~12 secondes, le module démarre un réseau AP+STA de secours. Il retente la connexion STA toutes les ~15 secondes et désactive son AP lorsque celle-ci réussit.
- Sans SSID domestique configuré, le secours s'active immédiatement.
- Nom par défaut : `SeeYa_XXXXXX` avec 6 chiffres hexadécimaux tirés de l'adresse MAC matérielle (suffixe de 24 bits).
- Mot de passe provisoire WPA2 de l'AP : **SeeYaSetup**. À changer avant un déploiement exposé à des utilisateurs non fiables (fonction de changement non encore implémentée).
- L'AP du module répond sur **192.168.4.1** et offre un serveur DNS de capture résolvant toutes les requêtes vers cette IP.
- Les routes de sondes Android, Apple et Windows redirigent HTTP vers `http://192.168.4.1/setup`; la page de paramétrage autorise les clients de l'AP de secours, sans identification admin supplémentaire (le WPA2 AP est requis).
- Certains OS n'ouvrent pas automatiquement la page, notamment si le HTTPS forcé ou les données cellulaires priment : ouvrir `http://192.168.4.1/setup` manuellement.
- Le portail et l'API sont uniquement en HTTP local. Ne pas exposer le module ou le Wi-Fi de secours directement à Internet.

## API
`GET /api/schedule` : `events` de 10 entrées `action` (0 OFF/inactif, 1 ON, 2 OFF), `minute` (0..1439), `days` (masque lundi bit0 ... dimanche bit6).

`POST /api/schedule` : `a0=1&m0=1080&d0=127` etc. pour les 10 slots.

`GET /api/wifi` : `ssid` et `fallbackSsid` sans password.

`POST /api/wifi` : formulaire `ssid`, `password`, `fallbackSsid`, `keepPassword` (true lorsque mot de passe inchangé et même SSID).

## Vérifications à effectuer
1. Sur la version précédente, noter l'ancienne programmation, car elle ne sera pas migrée.
2. Sauvegarder un événement ON sur tous les jours quelques minutes avant l'heure courante et un OFF quelques minutes après ; contrôler `scheduleOn`, `requestedOn` et LED.
3. Vérifier jours différents, deux événements à la même heure, passage minuit, mode AUTO avec radar actif, commandes FORCE_ON/OFF.
4. Éteindre temporairement le routeur Wi-Fi ; après une quinzaine de secondes, détecter `SeeYa_XXXXXX`, s'y connecter avec `SeeYaSetup`, vérifier l'apparition du portail ou ouvrir `http://192.168.4.1/setup`.
5. Configurer le SSID domestique depuis le portail ; remettre le routeur, vérifier reconnexion STA, fermeture AP et accès au SeeYa sur son IP réseau.
6. Renommer le SSID de secours et vérifier sa persistance au redémarrage.
7. Tester sur Android, iOS et Windows (ouverture automatique non garantie par les systèmes).

## Limites de cette version
Pas encore de protection par mot de passe AP unique par appareil, pas de sélection/scanning Wi-Fi sur portail, pas de détection d'erreur de mot de passe STA, et pas de banc de tests matériels automatisés.
