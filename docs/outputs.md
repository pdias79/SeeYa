# Sorties d'éclairage — prototype v0.2

## Fonctionnement
- Trois modes : AUTO (OFF tant que radars et horaires non implémentés), FORCE_ON et FORCE_OFF.
- Une seule sortie sélectionnée : désactivée (par défaut), relais GPIO, prise HTTP.
- LED GPIO8 suit l'état **demandé** et non un état physique confirmé ; vérifier le câblage de la SuperMini.
- Type de sortie, GPIO, polarité, paramètres LED, URL ON/OFF et méthode stockés en NVS.
- Mode désactivé par défaut : tester le relais **sans secteur** avant de l'utiliser.
- Modifier le type de sortie remet la demande à OFF. Ne pas s'en servir pour éteindre une prise distante déjà allumée : envoyer d'abord OFF.
- Le mode AUTO reste éteint tant que radars et horaires ne sont pas implémentés.

## GPIO
- GPIO admis : 3, 6, 7, 10. GPIO4/5 réservés au radar, 8 à la LED, 18/19 à l'USB ; éviter les broches de démarrage.
- HIGH actif : niveau 3,3 V à ON. LOW actif : 0 V à ON.
- Le firmware ne garantit pas les états électriques des broches pendant le démarrage ; vérifier le montage, envisager un étage matériel à état sûr.

## HTTP
- La prise doit proposer deux URL locales ON et OFF, GET ou POST sans corps.
- URL HTTP sans chiffrement uniquement pour ce prototype ; aucune prise exigeant TLS ou des headers personnalisés n'est encore prise en charge.
- Émission au changement d'état et jusqu'à trois tentatives. Wi-Fi hors ligne : attente de reconnexion.
- La requête utilise actuellement HTTPClient synchrone (timeout ~700 ms). Prévoir un transport non bloquant avant intégration radar.
- Un code 2xx signifie une commande acceptée et **non** une preuve d'allumage physique.
- Les URL (éventuellement porteuses de secrets) sont volontairement masquées dans la lecture de la configuration.

## Sécurité et limites
- API protégée par HTTP Basic, non chiffré : usage local sur LAN de confiance exclusivement. Ne jamais ouvrir de port depuis Internet.
- Pas encore de protection CSRF renforcée, de vérification cryptographique des binaires OTA, ni de rollback OTA validé. Les correctifs sont nécessaires avant un déploiement permanent.
- Pas de radar, d'horaires, de sondage d'état distant ni de wifi provisioning pour cette version.
- Mot de passe admin et Wi-Fi initialement compilés dans include/secrets.h (ignoré par Git).

## Commandes
- GET /api/status, GET /api/output
- POST /api/output, formulaire: type, gpio, activeHigh, ledEnabled, ledActiveHigh, urlOn, urlOff, httpMethod
- POST /api/light/on, POST /api/light/off, POST /api/light/auto
- POST /update pour le binaire OTA
