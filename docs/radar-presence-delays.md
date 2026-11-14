# Temporisations de détection radar — SeeYa 0.6.4

La détection radar (LD2410C ou LD2450) est temporisée par deux valeurs indépendantes, enregistrées en NVS depuis l'interface Web.

| Réglage | Défaut | Plage UI | Clé NVS |
|---|---:|---:|---|
| Présence continue avant allumage | 15 secondes | 0 à 3600 secondes | `radOnSec` |
| Absence avant extinction | 15 minutes | 1 à 1440 minutes | `radOffMin` |

Le second réglage est aussi exposé en secondes (`offDelaySeconds`) dans l'API de statut. Le premier est exposé dans `onDelaySeconds`, et `offDelayMinutes` indique le second en minutes.

## Logique

- Le radar doit signaler une **présence sans interruption** pendant la durée configurée. Si la présence disparaît avant le délai, le compteur repart de zéro lors de la présence suivante.
- Une fois la présence validée, `radarHold` devient vrai.
- Lorsqu'il n'y a plus de présence brute, le compte à rebours d'extinction commence. Toute réapparition de présence interrompt et réinitialise le compteur d'absence.
- Après la durée d'absence continue configurée, `radarHold` redevient faux.
- L'éclairage en AUTO continue d'utiliser `radarHold || scheduleOn`. Les événements horaires restent indépendants des délais radar, et les modes forcés restent prioritaires.
- Les commandes ponctuelles AUTO sont levées quand `radarHold` ou `scheduleOn` change.
- Comme précédemment, l'état éclairage est OFF au démarrage, puis reprend les automatismes au prochain changement de `radarHold` ou `scheduleOn`.

## Migration

Les nouveaux paramètres utilisent les clés NVS `radOnSec` et `radOffMin`, avec les nouveaux défauts 15 secondes et 15 minutes. L'ancienne clé `offDelay` (en secondes, par défaut 90) n'est plus lue : les configurations existantes reçoivent donc **15 minutes** d'extinction par défaut lors de la mise à jour. Les valeurs sont ensuite sauvegardées de façon persistante dès leur modification.

## API

`POST /api/radar` reçoit les paramètres de formulaire :
- `type` = `disabled`, `ld2410c`, `ld2450`.
- `onDelay` = nombre de secondes de présence continue.
- `offDelayMin` = nombre de minutes d'absence continue.

La validation des durées précède toute modification des préférences. Les changements de temporisations réinitialisent les chronomètres pour éviter d'appliquer un temps déjà écoulé.

## Scénarios de validation

1. En AUTO, sans timer ON, observer 10 secondes de présence puis une disparition : pas d'allumage. Refaire une présence continue de 15 secondes : allumage.
2. Après l'allumage, retirer la cible 5 minutes puis la réintroduire : la lampe reste allumée et le délai d'absence repart à zéro.
3. Laisser disparaître la cible 15 minutes : extinction si le programmateur est OFF.
4. Modifier les deux durées depuis l'interface, redémarrer, vérifier les valeurs persistées dans `GET /api/status`.
5. Confirmer que `FORCE_ON` et `FORCE_OFF` restent prioritaires et que les événements horaires peuvent allumer indépendamment de la présence.
6. Tester les limites (0, 3600, 1, 1440) et rejets de valeurs invalides avec HTTP 400.
7. Avec le radar désactivé, vérifier `radarHold=false`.

Note : les changements ne sont validés que par compilation CI avant les tests physiques ; le comportement exact d'une perte de liaison radar dépend des fonctions `present()` propres au capteur.
