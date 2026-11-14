# Programmation horaire SeeYa — v0.6

Deux plages horaires indépendantes sont disponibles pour chacun des sept jours de la semaine (lundi à dimanche). Par défaut, toutes les plages sont désactivées. Les configurations sont stockées en NVS et conservées au redémarrage.

## Principe AUTO
En mode **AUTO**, la sortie est demandée ON si **radarHold OU scheduleOn** est vrai. Une plage horaire peut donc allumer la lampe même si le radar est désactivé ou ne détecte personne. À la fin de la plage, le radar peut conserver la lampe allumée selon son propre délai d'extinction.

Les modes manuels **FORCE_ON** et **FORCE_OFF** restent prioritaires. Le bouton Auto rétablit les automatismes.

## Horloge
- Synchronisation NTP via Wi-Fi (`pool.ntp.org`, secours `time.google.com`).
- Heure civile `Europe/Paris` avec passage automatique hiver/été CET/CEST.
- Si l'horloge n'est pas encore synchronisée (p. ex. pas de Wi-Fi au démarrage), `clockReady=false` et la programmation horaire ne déclenche rien. Le radar et le contrôle manuel continuent à fonctionner.
- En l'absence de connexion après une synchronisation réussie, l'horloge interne continue à avancer; la précision dépend de la durée de déconnexion.

## Plages
Chacune des deux plages peut être activée séparément. Les plages peuvent se chevaucher et sont combinées par OU. Les horaires déjà enregistrés avec la version 0.5 sont conservés dans la première plage ; la seconde est désactivée par défaut.

Les bornes sont exprimées en heures et minutes locales, début inclus et fin exclue :
- 18:00 à 22:00 : éclairage de 18h jusqu'avant 22h le jour concerné.
- 22:00 à 02:00 : éclairage dès 22h le jour sélectionné et jusqu'à 02h le lendemain (même si le lendemain n'a pas de plage activée).
- Début et fin identiques interdits pour éviter toute ambiguïté sur une plage de 24 heures.

Les changements d'heure sont évalués sur l'heure locale affichée : l'heure répétée en automne est donc répétée, et les minutes inexistantes au printemps sont sautées.

## API
`GET /api/status` inclut `clockReady`, `localTime` et `scheduleOn`.

`GET /api/schedule` renvoie `days`, tableau de 7 objets (`enabled`, `startMin`, `endMin`) classés de lundi à dimanche, ainsi que `secondSlots` avec le même format.

`POST /api/schedule` reçoit tous les jours dans un formulaire x-www-form-urlencoded, pour chaque indice 0..6 : `e0=true|false`, `s0=1080`, `f0=1320` pour la première plage et `e0_2=false`, `s0_2=720`, `f0_2=780` pour la seconde. Les horaires sont des minutes depuis minuit (0..1439). Validation intégrale avant sauvegarde.

## Recette matérielle
1. Vérifier que `clockReady=true`, la date et l'heure locale sont correctes.
2. Désactiver temporairement le radar ou sortir de sa zone ; configurer une plage contenant l'heure courante.
3. En mode AUTO, la LED de l'ESP32 s'allume et `scheduleOn=true`.
4. Modifier la plage pour qu'elle n'inclue plus l'heure courante : l'éclairage s'éteint si `radarHold=false`.
5. Tester FORCE_OFF pendant une plage active, puis revenir à AUTO.
6. Tester une plage traversant minuit et la sauvegarde après redémarrage.

## Limites de cette première version
- Une seule plage par jour (des exceptions/vacances sont prévues pour plus tard).
- Pas de contrôle du serveur NTP depuis l'interface.
- Pas encore de tests automatisés sur le matériel ou aux transitions DST.
