# Carte temps réel LD2450 — SeeYa 0.6.5-dev

L'interface Web intègre une **vue de dessus** du radar LD2450, sans bibliothèque JavaScript externe.

- Radar à l'origine `X=0, Y=0` en bas au centre.
- Axe X horizontal de −6 m à +6 m ; axe Y devant le radar de 0 m à +6 m.
- Grille métrique, jusqu'à trois cibles affichées par des points numérotés.
- Cibles vertes si retenues par le filtre de zone actuel ; orange si hors zone.
- Le panneau indique les coordonnées X/Y en mètres et la vitesse en cm/s ; un message explicite apparaît si la liaison radar est absente.
- Mise à jour des positions par `GET /api/radar/live` environ toutes les 600 ms lorsque le panneau LD2450 est visible (la page de statut continue d'être actualisée environ toutes les 3 s).
- En cochant **Dessiner la zone sur le plan**, glisser du premier coin au second à la souris ou au doigt. La sélection est arrondie par pas de 100 mm et répercute ses bornes dans les quatre champs numériques.
- **Enregistrer la zone** est nécessaire pour appliquer et enregistrer la modification en NVS. Le rendu graphique est un aperçu local tant que la zone n'est pas enregistrée.
- Les champs numériques permettent de saisir des limites plus précises que le pas de dessin de 100 mm.
- Les actualisations périodiques ne réécrivent pas les bornes en cours d'édition.

## Protocole

`GET /api/radar/live` renvoie un objet JSON de la forme :

```json
{
  "healthy": true,
  "targets": [
    {"valid": true, "xMm": 1200, "yMm": 2600, "speedCmS": 5, "insideZone": true},
    {"valid": false, "xMm": 0, "yMm": 0, "speedCmS": 0, "insideZone": false},
    {"valid": false, "xMm": 0, "yMm": 0, "speedCmS": 0, "insideZone": false}
  ]
}
```

Les cibles dont la liaison radar a expiré sont marquées invalides. L'API respecte la configuration d'authentification HTTP de SeeYa.

## Limites

- Le **LD2410C** ne fournit pas de coordonnées X/Y exploitables par cette carte ; il donne seulement des distances, énergies et états de présence. La carte s'affiche uniquement lorsque le LD2450 est sélectionné.
- La carte représente la zone rectangulaire logicielle de SeeYa, pas les réglages internes de sensibilité du radar.
- Les cibles affichées sont les dernières mesures du radar ; la logique d'allumage continue de respecter les délais configurés (15 s de présence, 15 min d'absence par défaut).
- Le tracé n'implémente pas de suivi historique ou de calibration géométrique du capteur.

## Vérifications sur matériel

1. Avec LD2450 connecté, sélectionner ce radar et ouvrir le panneau ; le point représentant une personne doit se déplacer lorsque celle-ci bouge devant le capteur.
2. Vérifier visuellement l'orientation : X gauche/droite, Y distance devant le radar.
3. Placer la cible dans et hors de la zone, vérifier le changement vert/orange et le texte associé.
4. Dessiner une zone, contrôler les quatre champs numériques puis l'enregistrer. Vérifier la persistance après rechargement et redémarrage.
5. Modifier un des champs numériques à la main et vérifier que le rectangle se met à jour, sans être écrasé par le rafraîchissement.
6. Débrancher temporairement le radar : l'interface doit signaler l'absence de liaison plutôt que d'afficher des cibles périmées.
7. Passer au LD2410C : le panneau de position X/Y doit être masqué.
