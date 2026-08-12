# Historique de travail — LabConnect Hub CDO

Date de synthèse : 12 août 2026

## Objectif client

Le client utilise Optimu sous Windows pour générer des rapports de contrôle de
poids de calibration. Optimu communique avec les balances par port série COM.

L’objectif du projet est de supprimer les manipulations de câbles RS-232 en
installant un M5Stack ATOM Lite sur chaque balance. Chaque ATOM crée un port COM
Bluetooth Classic SPP côté Windows, puis relaie strictement les octets entre
Optimu et la balance.

## Parc de balances CDO

| Identifiant | Balance | Nom Bluetooth | Port Windows visé |
|---|---|---|---|
| `CDO02` | A&D MC-30K | `CDO02` | COM2 |
| `CDO03` | A&D MC-6100 | `CDO03` | COM3 |
| `CDO04` | Mettler XP504 | `CDO04` | COM4 |
| `CDO05` | A&D BA-225 | `CDO05` | COM5 |
| `CDO06` | Mettler XP56 | `CDO06` | COM6 |

Windows attribue les ports COM. Le firmware ne peut pas imposer COM2 à COM6 :
ces numéros doivent être affectés manuellement dans Windows après appairage.

## Décisions techniques

- Firmware unique pour les cinq ATOM Lite.
- Configuration série commune : `9600/8N1`, sans contrôle de flux, terminaison
  `CRLF`.
- Les balances A&D sont réglées en format MT afin qu’Optimu puisse utiliser les
  mêmes commandes que pour les Mettler, notamment `SI`.
- L’ATOM identifie d’abord la balance, puis seulement ensuite publie son nom
  Bluetooth.
- Après identification, l’ATOM devient un pont transparent octet par octet.
- Aucune conversion de format, aucune correction de décimales, aucune
  normalisation des espaces, signes, unités ou fins de ligne.
- Les données arrivées de la balance lorsqu’Optimu n’est pas connecté sont
  lues puis jetées pour éviter d’envoyer une ancienne mesure à la reconnexion.

## Identification des balances

Au démarrage, l’ATOM envoie :

1. `I10\r\n` pour identifier une Mettler ;
2. `?ID\r\n` si aucune identité Mettler valide n’est reçue.

Les Mettler peuvent renvoyer un identifiant alphanumérique, par exemple :

```text
I10 A "CDO04"
```

Les A&D renvoient un identifiant numérique. Convention retenue :

- seuls les deux derniers chiffres sont significatifs ;
- `0000003` devient `CDO03` ;
- `0000000000005` devient `CDO05` ;
- `0000000` est refusé car aucun identifiant utile n’est programmé.

Les identifiants sont acceptés uniquement s’ils correspondent à `CDO02` à
`CDO06`, et si le constructeur détecté correspond au modèle attendu.

## Commandes utilisateur sur l’ATOM

| Action | Effet |
|---|---|
| Appui long 2,5 s | Active l’appairage Bluetooth pendant 120 s |
| Triple clic | Active ou coupe le diagnostic Wi-Fi temporaire |
| Clic simple | Aucune action |
| Double clic | Aucune action |

Le clic simple et le double clic sont volontairement sans effet pour éviter les
commandes accidentelles pendant un contrôle.

## Voyants ATOM

| Voyant | Signification |
|---|---|
| Orange fixe | Identification initiale de la balance |
| Rouge clignotant | Balance absente, identité invalide ou erreur Bluetooth |
| Blanc clignotant lent | Balance identifiée, Bluetooth prêt, PC non connecté |
| Bleu clignotant | Appairage Bluetooth actif |
| Bleu fixe | PC / Optimu connecté au port SPP |
| Vert bref | Données en transit |

Le rouge est réservé aux anomalies. L’état “balance reconnue mais PC non
connecté” a été déplacé en blanc clignotant.

## Diagnostic Wi-Fi

Un triple clic crée un point d’accès Wi-Fi temporaire :

- `CDOxx-Diag` si la balance a été identifiée ;
- `CDO-DIAG-XXXX` sinon.

Mot de passe : `labconnect`.

Adresse : `http://192.168.4.1/`.

La page est en lecture seule. Elle affiche :

- identité détectée ;
- modèle attendu ;
- état Bluetooth SPP ;
- paramètres UART ;
- état LED réel publié par l’ATOM ;
- journal RAM texte et hexadécimal dans les deux sens.

L’API `http://192.168.4.1/api/status` expose notamment :

- `id`
- `model`
- `btStarted`
- `btClient`
- `led`
- `ledMeaning`
- `identification`
- `events`

## Utilitaire Windows ajouté

Un outil PowerShell sans dépendance a été ajouté :

```text
LabConnect Hub CDO/tools/windows/LabConnectCdoCheck.ps1
```

Il permet de :

- scanner les ports COM ;
- envoyer `SI` avec terminaison `CRLF` ;
- tester un ou plusieurs ports précis ;
- interroger les identifiants avec `I10` puis `?ID` ;
- afficher un état LED probable depuis le COM ;
- lire l’état LED réel via `/api/status` lorsque le diagnostic Wi-Fi est actif.

Exemples :

```powershell
powershell -ExecutionPolicy Bypass -File ".\LabConnect Hub CDO\tools\windows\LabConnectCdoCheck.ps1" -Ports COM2

powershell -ExecutionPolicy Bypass -File ".\LabConnect Hub CDO\tools\windows\LabConnectCdoCheck.ps1" -Ports COM2,COM3,COM4,COM5,COM6 -Identity

powershell -ExecutionPolicy Bypass -File ".\LabConnect Hub CDO\tools\windows\LabConnectCdoCheck.ps1" -Ports COM4 -Watch -StatusUrl http://192.168.4.1/api/status
```

## Points appris pendant les essais

- Windows crée généralement deux ports pour un périphérique Bluetooth SPP :
  un port sortant et un port entrant.
- Optimu doit utiliser le port sortant.
- L’ATOM ne peut pas empêcher Windows de créer le port entrant.
- L’ATOM ne peut pas choisir le numéro COM Windows.
- Après suppression et réappairage, un redémarrage Windows peut être nécessaire
  pour qu’Optimu voie correctement le port COM.
- La reconnexion automatique dépend surtout de Windows et de l’application :
  le port SPP s’ouvre réellement quand Optimu ouvre le COM sortant.
- Un ATOM déplacé sur une autre balance doit être redémarré pour relancer
  l’identification.
- Un ATOM de remplacement possède une nouvelle adresse Bluetooth : Windows doit
  donc le réappairer et réaffecter le COM.

## Résultats des essais réalisés

- A&D : validation de la convention d’identifiant numérique par derniers
  chiffres.
- Mettler CDO04 : identifiant corrigé avec `I10 "CDO04"`.
- Mettler CDO06 : identifiant corrigé avec `I10 "CDO06"`.
- Optimu : un essai réel a confirmé la réception correcte d’une pesée après
  sélection du bon port COM.
- Le besoin de terminaison `CRLF` côté terminal a été confirmé pour obtenir les
  réponses aux commandes `SI`, `Q` ou `?ID`.

## Artefact marketing

Une image explicative des voyants ATOM a été générée et ajoutée au projet :

```text
LabConnect Hub CDO/assets/guide-voyants-atom-labconnect-cdo.png
```

Elle sert de support visuel pour expliquer les états LED au client.

## Firmware et validation

Sketch principal :

```text
LabConnect Hub CDO/firmware/atom-lite-cdo/LabConnectHubCDO/LabConnectHubCDO.ino
```

Configuration validée :

- carte Arduino : M5Stack M5Atom ;
- core M5Stack ESP32 : 3.3.8 ;
- cible matérielle : M5Stack ATOM Lite ;
- Bluetooth Classic SPP.

Commandes de validation utilisées :

```bash
python3 -m unittest discover -s "LabConnect Hub CDO/tests" -v

arduino-cli compile --fqbn m5stack:esp32:m5stack_atom \
  "LabConnect Hub CDO/firmware/atom-lite-cdo/LabConnectHubCDO"
```

Dernière validation connue :

- tests unitaires : 6/6 OK ;
- compilation ATOM : OK ;
- occupation flash : environ 53 % ;
- RAM globale : environ 21 %.

## Uploads récents

Deux ATOM connectés au Mac ont été reflashés avec le firmware incluant l’état
LED publié dans l’API diagnostic :

| Port macOS | Adresse Bluetooth |
|---|---|
| `/dev/cu.usbserial-85524E0639` | `14:08:08:55:45:7c` |
| `/dev/cu.usbserial-BD52AC1D39` | `00:4b:12:a0:50:78` |

## Commits importants

| Commit | Sujet |
|---|---|
| `9937e6ff` | Firmware universel LabConnect Hub CDO |
| `ad3aa192` | Utilitaire Windows de contrôle COM |
| `f79d7379` | État LED réel publié dans le diagnostic |
| `6c33f75e` | Image marketing des voyants ATOM |

Branche de travail :

```text
codex/labconnect-hub-cdo
```

## Points de vigilance avant livraison client

- Vérifier les cinq balances simultanément avec Optimu.
- Confirmer que les ports COM sortants sont bien affectés à COM2 à COM6.
- Documenter la procédure Windows avec captures si le client doit refaire
  l’appairage seul.
- Tester un redémarrage Windows avec toutes les balances allumées.
- Tester la fermeture et réouverture d’Optimu.
- Conserver une procédure de remplacement d’ATOM : suppression Bluetooth,
  nouvel appairage, réaffectation du port COM.

## Améliorations possibles

- Créer une version PDF déterministe de la fiche voyants avec vraie photo et
  texte vectoriel.
- Ajouter des captures Windows à la fiche d’installation.
- Créer un petit exécutable Windows autour du script PowerShell si le client ne
  doit jamais utiliser la console.
- Préparer une checklist imprimable de recette client pour les cinq balances.
