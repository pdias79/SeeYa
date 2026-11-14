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

## Démarrage et indicateur Wi-Fi

Au démarrage du module, la **sortie lumière est à OFF** et le mode reste AUTO. Même si les automatismes indiquent déjà ON à l'initialisation, cette consigne de démarrage OFF persiste jusqu'au prochain changement de `radarHold` ou de `scheduleOn`, ou jusqu'à une commande manuelle explicite. Les modes forcés ne sont jamais restaurés automatiquement au redémarrage.

La **LED d'état GPIO8 clignote** (250 ms allumée / 750 ms éteinte) tant que le Wi-Fi domestique n'est pas connecté, y compris lorsque le point d'accès de secours fonctionne. Lorsque le Wi-Fi revient, la LED indique de nouveau l'état demandé de l'éclairage. Le relais et les commandes HTTP ne sont pas modifiés par le clignotement. Si la LED d'état a été désactivée dans l'interface, elle reste désactivée.

## Vérification matérielle
1. Couper puis remettre l'alimentation : sortie lumière OFF au démarrage, même si une plage horaire était déjà ON. Vérifier qu'elle reprend les automatismes au prochain changement.
2. Déconnecter le Wi-Fi domestique : la LED GPIO8 clignote, sans faire commuter le relais ; la reconnecter et vérifier que la LED reprend son affichage habituel.
3. En AUTO sans radar ni plage active, appuyer sur **Allumer** : LED ON, `mode=AUTO`, `autoManualActive=true`.
4. Produire une présence radar puis attendre la fin de la temporisation : après le changement de `radarHold`, `autoManualActive=false`.
5. En AUTO avec radar présent, appuyer sur **Éteindre** : LED OFF ; à l'expiration de `radarHold` ou au changement `scheduleOn`, reprise automatique.
4. En `FORCE_ON` et `FORCE_OFF`, vérifier que les boutons ponctuels sont grisés et que leurs endpoints retournent 409.
7. Cliquer sur **Auto** depuis un mode forcé : sortie immédiatement recalculée.
8. Vérifier que la configuration Wi-Fi, le radar et les événements sont inchangés.
