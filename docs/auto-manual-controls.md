# Commandes éclairage dans SeeYa 0.6.2

L'interface distingue maintenant deux types de commande :

- **Forcer Allumé** : passe en `FORCE_ON` et maintient la lumière allumée.
- **Forcer Éteindre** : passe en `FORCE_OFF` et maintient la lumière éteinte.
- **Auto** : passe en `AUTO`, retire toute commande manuelle ponctuelle et applique l'état courant des automatismes.
- **Allumer / Éteindre** : disponibles **uniquement en AUTO** et produisent une commande ponctuelle sans changer de mode.

## Durée des commandes ponctuelles AUTO

Une commande ponctuelle reste appliquée jusqu'au **prochain changement d'état** de l'une des deux sources d'automatisme :
- `radarHold`, qui intègre la détection radar et la temporisation d'extinction ;
- `scheduleOn`, état calculé par les 10 événements hebdomadaires.

La commande ponctuelle est alors levée, et la sortie reprend la règle `radarHold || scheduleOn`.

Important : une cible qui reste détectée en permanence n'entraîne pas de nouveau changement de `radarHold` ; appuyer sur **Éteindre** alors que `radarHold` est déjà vrai laisse donc la lumière éteinte jusqu'au prochain changement radar ou timer. Cliquer **Auto** réactive immédiatement le résultat des automatismes.

Les endpoints existants `POST /api/light/on` et `/api/light/off` conservent leur signification historique de commandes **FORCE_ON / FORCE_OFF**.

Nouveaux endpoints :
- `POST /api/light/auto/on`
- `POST /api/light/auto/off`

Ils retournent **409 Conflict** si le mode n'est pas AUTO. `GET /api/status` expose `autoManualActive` et `autoManualOn` pour diagnostiquer l'état ponctuel. Les boutons ponctuels sont désactivés dans l'interface dès que `mode != AUTO`.

## Vérification matérielle
1. En AUTO sans radar ni plage active, appuyer sur **Allumer** : LED ON, `mode=AUTO`, `autoManualActive=true`.
2. Produire une présence radar puis attendre la fin de la temporisation : après le changement de `radarHold`, `autoManualActive=false`.
3. En AUTO avec radar présent, appuyer sur **Éteindre** : LED OFF ; à l'expiration de `radarHold` ou au changement `scheduleOn`, reprise automatique.
4. En `FORCE_ON` et `FORCE_OFF`, vérifier que les boutons ponctuels sont grisés et que leurs endpoints retournent 409.
5. Cliquer sur **Auto** depuis un mode forcé : sortie immédiatement recalculée.
6. Vérifier que la configuration Wi-Fi, le radar et les événements sont inchangés.
