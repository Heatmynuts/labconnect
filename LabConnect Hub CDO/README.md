# LabConnect Hub CDO

Firmware universel pour cinq balances reliées chacune à un M5Stack ATOM Lite
par l’adaptateur RS-232 M5Stack. Chaque ATOM crée un port COM Bluetooth
Classic SPP indépendant pour Optimu sous Windows.

Le même firmware est installé sur les cinq ATOM. Il lit l’identifiant enregistré
dans la balance, vérifie qu’il correspond à la flotte CDO, puis utilise cet
identifiant comme nom Bluetooth.

## Affectation CDO

| Identifiant | Balance | Nom Bluetooth | Port souhaité |
|---|---|---|---|
| `CDO02` | A&D MC-30K | `CDO02` | COM2 |
| `CDO03` | A&D MC-6100 | `CDO03` | COM3 |
| `CDO04` | Mettler XP504 | `CDO04` | COM4 |
| `CDO05` | A&D BA-225 | `CDO05` | COM5 |
| `CDO06` | Mettler XP56 | `CDO06` | COM6 |

Windows attribue les numéros de ports COM. Le firmware fournit le nom
Bluetooth, mais ne peut pas imposer le numéro COM.

## Préparation obligatoire des balances

Toutes les balances doivent utiliser les mêmes paramètres physiques :

- 9600 bauds ;
- 8 bits de données ;
- aucune parité ;
- 1 bit d’arrêt ;
- aucun contrôle de flux ;
- terminaison `CRLF`.

Sur les trois A&D, sélectionner également le **format de données MT**. Les
modèles MC-30K, MC-6100 et BA-225 acceptent directement la commande `SI` : le
firmware n’a donc pas besoin de traduire la commande ou la réponse.

Enregistrer enfin l’identifiant exact dans chaque balance : `CDO02`, `CDO03`,
`CDO04`, `CDO05` ou `CDO06` selon le tableau ci-dessus.

## Identification automatique

Au démarrage, l’ATOM travaille en 9600/8N1 et effectue en boucle :

1. `I10` pour une Mettler ;
2. `?ID` si aucune identité Mettler valide n’a été reçue ; une réponse A&D
   numérique est convertie en ne retenant que ses deux derniers chiffres :
   `000003` ou `0000003` deviennent `CDO03`, tandis que `0000000` est rejeté ;
3. validation de l’identifiant et de la cohérence constructeur/identifiant ;
4. démarrage du Bluetooth SPP sous le nom `CDOxx`.

Tant qu’aucune balance autorisée n’est détectée, aucun port Bluetooth n’est
annoncé. Cela empêche un ATOM débranché ou déplacé d’être associé au mauvais
port COM. L’identification recommence automatiquement lorsque la balance finit
de démarrer.

Une fois l’identité validée, le firmware devient un pont strictement
transparent. Il ne parse pas le poids et ne modifie ni les espaces, ni les
décimales, ni les signes, ni les unités, ni les terminaisons CR/LF. Les données
émises sans client SPP sont lues puis jetées afin de ne jamais livrer une
ancienne mesure lors d’une reconnexion.

## Compilation

Sketch Arduino :

`firmware/atom-lite-cdo/LabConnectHubCDO/LabConnectHubCDO.ino`

Configuration validée :

- carte **M5Stack → M5Atom** ;
- paquet de cartes **M5Stack ESP32 3.3.8** ;
- aucune bibliothèque Arduino externe.

En ligne de commande :

```bash
arduino-cli compile --fqbn m5stack:esp32:m5stack_atom \
  "LabConnect Hub CDO/firmware/atom-lite-cdo/LabConnectHubCDO"
```

## Bouton et voyant

### Bouton

- appui long de 2,5 secondes : appairage Bluetooth pendant 120 secondes ;
- triple clic : activation ou arrêt du diagnostic Wi-Fi ;
- clic simple ou double : aucune action.

### Voyant

- orange fixe : identification initiale ;
- rouge clignotant : balance absente, identité invalide ou erreur Bluetooth ;
- blanc clignotant lent : balance identifiée, Bluetooth prêt, PC déconnecté ;
- bleu clignotant : appairage ;
- bleu fixe : Optimu connecté ;
- vert bref : données en transit.

## Diagnostic Wi-Fi en lecture seule

Un triple clic crée temporairement :

- `CDOxx-Diag` après identification ;
- `CDO-DIAG-XXXX` si l’identification n’a pas encore abouti.

Mot de passe : `labconnect`. Ouvrir ensuite `http://192.168.4.1/`.

La page affiche l’identité, le modèle attendu, l’état SPP, les paramètres UART
et les octets échangés en texte et en hexadécimal. Elle ne permet ni d’envoyer
une commande, ni de changer un réglage. Le journal reste uniquement en mémoire
vive. Un nouveau triple clic coupe le Wi-Fi sans interrompre Optimu.

## Installation Windows et Optimu

1. Préparer et allumer les cinq balances.
2. Flasher le même firmware sur chaque ATOM, puis le connecter à sa balance.
3. Vérifier que le voyant quitte l’état d’identification.
4. Maintenir le bouton 2,5 secondes et associer le nom `CDOxx` dans Windows.
5. Dans le Gestionnaire de périphériques, repérer le port COM **sortant** du
   service Bluetooth SPP.
6. Dans ses paramètres avancés, affecter COM2 à COM6 selon le tableau.
7. Sélectionner le port correspondant dans Optimu et effectuer une demande de
   poids.

Avant le déploiement complet, valider une A&D configurée en format MT avec le
PC Optimu cible et comparer la réponse au branchement filaire.

## Utilitaire Windows de contrôle

Le dossier `tools/windows` contient un petit script PowerShell sans dépendance :

`LabConnectCdoCheck.ps1`

Il ouvre les ports COM disponibles en `9600/8N1`, sans contrôle de flux, envoie
une commande terminée par `CRLF`, puis affiche la réponse. Il sert à vérifier
rapidement que Windows, l'ATOM, le lien Bluetooth et la balance répondent avant
d'ouvrir Optimu.

Depuis PowerShell, dans le dossier du projet :

```powershell
powershell -ExecutionPolicy Bypass -File ".\LabConnect Hub CDO\tools\windows\LabConnectCdoCheck.ps1"
```

Commandes utiles :

```powershell
# Tester seulement le port attendu pour CDO02.
powershell -ExecutionPolicy Bypass -File ".\LabConnect Hub CDO\tools\windows\LabConnectCdoCheck.ps1" -Ports COM2

# Tester l'identifiant de la balance, Mettler puis A&D.
powershell -ExecutionPolicy Bypass -File ".\LabConnect Hub CDO\tools\windows\LabConnectCdoCheck.ps1" -Ports COM2,COM3,COM4,COM5,COM6 -Identity

# Envoyer une autre commande de diagnostic.
powershell -ExecutionPolicy Bypass -File ".\LabConnect Hub CDO\tools\windows\LabConnectCdoCheck.ps1" -Ports COM4 -Command I10
```

Un port entrant Bluetooth peut apparaître dans Windows, mais il ne répond
normalement pas dans cette architecture. Optimu doit utiliser le port COM
sortant.

## Remplacement d’un ATOM

Flasher le firmware commun, connecter le nouvel ATOM à la balance et attendre
son identification. Le nouveau matériel possède une autre adresse Bluetooth :
supprimer l’ancien périphérique Windows, associer le nouveau `CDOxx`, puis lui
réaffecter le même numéro COM. Aucun réglage n’est nécessaire dans le firmware.

Après tout déplacement d’un ATOM vers une autre balance, redémarrer l’ATOM pour
forcer une nouvelle identification.
