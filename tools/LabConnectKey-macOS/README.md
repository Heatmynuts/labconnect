# LabConnect Key pour macOS

LabConnect Key lit les mesures reçues depuis LabConnect Bluetooth et peut les
saisir automatiquement dans Excel, Word, Numbers ou une autre application.

## Construire l’application

Dans Terminal :

```bash
cd /Users/alexandreperou/Documents/labconnect/tools/LabConnectKey-macOS
chmod +x build.sh
./build.sh
```

L’application est créée ici :

```text
build/LabConnect Key.app
```

## Préparer LabConnect Bluetooth

1. Maintenir le bouton de LabConnect pendant 2,5 secondes.
2. Attendre que le voyant bleu clignote.

Ne pas chercher l’appareil dans **Réglages Système → Bluetooth** : LabConnect
Key le détecte et s’y connecte directement.

## Premier test

1. Ouvrir **LabConnect Key**.
2. Choisir **Mode Bluetooth**.
3. Cliquer sur **Actualiser**.
4. Sélectionner le nom se terminant par `-Mac`.
5. Cliquer sur **Connecter**.
6. Vérifier que les mesures apparaissent dans le moniteur.

Pour tester la saisie automatique :

1. activer **Activer la saisie automatique** ;
2. dans la fenêtre proposée au démarrage, cliquer sur
   **Ouvrir les réglages** ;
3. activer LabConnect Key dans
   **Réglages Système → Confidentialité et sécurité → Accessibilité** ;
4. revenir dans LabConnect Key ;
5. choisir l’action effectuée après chaque mesure ;
6. placer le curseur dans une cellule Excel ou Numbers.

Chaque nouvelle mesure est saisie dans l’application active.

La zone **Informations jointes** permet d’ajouter, au choix, l’ID de la
balance, sa localisation, l’utilisateur, son numéro de série, sa marque et son
modèle. Chaque information possède un numéro d’ordre. Choisir une position déjà
utilisée échange automatiquement les deux positions.

Le séparateur peut être une tabulation, une virgule, un point-virgule, un
espace, un retour à la ligne, une barre verticale ou une valeur personnalisée.
La **Pesée** est toujours active, mais sa position peut être choisie comme
celle des autres informations. Elle correspond à la ligne brute reçue de la
balance, sans parsing. Par défaut, elle occupe la première position.

Avec **Tabulation**, LabConnect Key simule une véritable touche Tab entre les
valeurs afin de les placer dans des cellules successives. Avec **Retour à la
ligne**, il simule une véritable touche Entrée. Les autres séparateurs sont
insérés comme du texte.

Les champs non cochés ou vides sont ignorés. L’ordre, le séparateur, les cases
et toutes les valeurs sont mémorisés.

La modale propose également :

- **Défaut** pour rétablir la pesée en première position, désactiver les
  informations optionnelles et choisir la tabulation ;
- **Sauvegarder…** pour exporter un profil `.json` ;
- **Charger…** pour restaurer les valeurs, les cases, l’ordre et le séparateur
  d’un profil.

Le bouton **Effacer** vide l’historique affiché dans le moniteur sans modifier
la connexion ni les réglages.

LabConnect Key transmet chaque ligne exactement comme elle est produite par la
balance, sans extraire ni modifier la valeur. Il mémorise les choix de la
dernière session : saisie automatique, action après mesure, mode de connexion,
dernier appareil, dernier câble, dernière commande et terminaison.

## Commande de test

Le champ situé sous le moniteur permet d’envoyer une commande à la balance.
Pour une balance A&D, saisir `Q`, conserver la terminaison `CRLF`, puis cliquer
sur **Envoyer**.

## Après une mise en veille

LabConnect Key tente automatiquement de rétablir la liaison. Si elle ne
revient pas, maintenir le bouton pendant 2,5 secondes, puis cliquer sur
**Actualiser** et **Connecter**.

Deux modes sont disponibles :

- **Mode Bluetooth** pour un adaptateur LabConnect ;
- **Mode filaire** pour un câble RS-232/USB reconnu par macOS.
