# Authentification HTTP optionnelle

Dans le fichier local `include/secrets.h` (ignoré par Git), l'option `SEEYA_ADMIN_PASSWORD` contrôle l'authentification :

```cpp
#define SEEYA_ADMIN_PASSWORD "mon-mot-de-passe"
```

Avec un mot de passe **non vide**, SeeYa utilise HTTP Basic, avec le nom d'utilisateur `admin`.

Pour désactiver **toute authentification HTTP** :

```cpp
#define SEEYA_ADMIN_PASSWORD ""
```

L'absence d'authentification concerne **toutes les pages Web et routes HTTP**, notamment `/api/status`, `/api/light/*`, `/api/schedule`, `/api/radar`, `/api/output`, `/api/wifi` et **`/update` (OTA)**. Le portail Wi-Fi de secours reste accessible sans compte sur son propre réseau.

Le firmware accepte désormais cette chaîne vide au démarrage ; la valeur d'exemple `CHANGE_ME_ADMIN_PASSWORD` reste une erreur de configuration qui empêche le démarrage réseau, jusqu'à remplacement par un mot de passe ou par `""`.

**Important :** la désactivation de l'authentification HTTP donne à toute personne capable d'accéder au serveur la possibilité d'allumer/éteindre le relais, modifier les réglages ou installer un nouveau firmware OTA. Ceci est particulièrement sensible lorsque le point d'accès Wi-Fi de secours **ouvert** est actif. N'exposez jamais l'appareil directement à Internet dans cette configuration.

## Vérifications

1. Définir `SEEYA_ADMIN_PASSWORD ""` dans **le fichier local**, compiler et flasher le firmware.
2. Depuis un navigateur privé (sans anciens identifiants Basic), vérifier l'ouverture directe de la page Web et de `GET /api/status`.
3. Depuis un client HTTP sans identifiants, appeler `POST /api/light/auto`, puis `POST /api/light/auto/on` ; vérifier un code 200.
4. Vérifier que le formulaire OTA accepte un transfert sans en-tête Authorization, idéalement lors d'une mise à jour explicitement prévue.
5. Restaurer un mot de passe non vide, compiler/flasher et vérifier HTTP 401 et `WWW-Authenticate` sans identifiants pour une route protégée (dont OTA).
6. Vérifier qu'en mot de passe vide aucun code 401 n'est déclenché par le middleware d'authentification.

Le changement du mot de passe admin nécessite actuellement **une recompilation du firmware**, puisque la valeur est définie à la compilation.
