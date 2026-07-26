# LabConnect BT — ATOM Lite

Firmware de passerelle entièrement transparente entre une balance en RS-232
et Windows ou macOS. Le même firmware fournit un port COM Bluetooth Classic
à Windows et une liaison Bluetooth directe à l’application macOS LabConnect
Key.

Documentation utilisateur complète :
[Manuel utilisateur LabConnect BT](MANUEL_UTILISATEUR.md).

Guide simplifié destiné à l’utilisateur final :
[Guide utilisateur simplifié](GUIDE_UTILISATEUR.md).

## Matériel

- M5Stack ATOM Lite original, basé sur ESP32-PICO-D4
- adaptateur RS-232 M5Stack avec conversion de niveaux
- balance A&D avec interface DB9

L’AtomS3 n’est pas compatible avec ce firmware : l’ESP32-S3 ne prend pas en
charge le Bluetooth Classic SPP nécessaire au port COM Windows.

## Configuration

| Paramètre | Valeur |
|---|---|
| Bluetooth Windows | Classic SPP, `LabConnect-BT-XXXX` |
| Bluetooth macOS | BLE, `LabConnect-BT-XXXX-Mac` |
| Appairage | Secure Simple Pairing NoInputNoOutput, sans PIN fixe |
| UART balance | 2400 bauds, 7E1 |
| RX | GPIO 22 |
| TX | GPIO 19 |
| Contrôle de flux | Aucun |
| Point d’accès Wi-Fi | `LabConnect-BT-XXXX-WiFi` |
| Mot de passe Wi-Fi | `labconnect` |
| Interface web | `http://192.168.4.1/` |
| Activation Wi-Fi | triple clic sur le bouton de l’ATOM |

Le suffixe `XXXX` du nom Bluetooth est dérivé de l’adresse matérielle de
l’ATOM, afin de différencier plusieurs adaptateurs.

## Compilation

Dans Arduino IDE :

1. Ouvrir `LabConnectBT.ino`.
2. Sélectionner la carte **M5Atom**.
3. Compiler puis téléverser par USB-C.

Le firmware n’utilise aucune bibliothèque externe. `BluetoothSerial`, Wi-Fi,
le serveur web et le pilote de LED sont fournis par le cœur Arduino ESP32.

## Utilisation sous macOS

macOS ne crée pas de port série pour le profil Bluetooth Classic de l’ATOM.
Il faut utiliser **LabConnect Key**, qui communique directement avec le canal
Bluetooth Mac intégré au même firmware :

1. maintenir le bouton pendant 2,5 secondes, jusqu’au clignotement bleu ;
2. ouvrir LabConnect Key et choisir **Mode Bluetooth** ;
3. cliquer sur **Actualiser** ;
4. sélectionner `LabConnect-BT-XXXX-Mac`, puis **Connecter**.

Il ne faut pas chercher ni associer ce nom dans les réglages Bluetooth de
macOS. Après une coupure ou une mise en veille, l’application tente de se
reconnecter automatiquement.

## Utilisation sous Windows

1. Mettre sous tension la balance et l’ATOM Lite.
2. Maintenir le bouton de l’ATOM Lite pendant 2,5 secondes. Le voyant bleu
   clignote et l’adaptateur devient détectable pendant deux minutes.
3. Dans les paramètres Bluetooth de Windows, associer
   `LabConnect-BT-XXXX`.
   Si Windows affiche un code de confirmation, accepter côté Windows : aucune
   saisie ni validation n’est nécessaire sur l’ATOM.
4. Relever le port COM Bluetooth attribué dans le Gestionnaire de
   périphériques.
5. Dans RsCom ou RsKey, sélectionner ce port COM.
6. Régler WinCT sur `2400`, parité paire, `7` bits, `1` bit d’arrêt et le
   terminateur attendu par la balance.

Après l’appairage ou après deux minutes, l’adaptateur redevient non
détectable. Un PC déjà appairé peut continuer à se reconnecter normalement.
Le mode d’appairage ne peut pas être lancé lorsqu’un client Bluetooth est déjà
connecté.

Les paramètres du port COM Bluetooth n’altèrent pas le flux SPP. Le firmware
applique physiquement le format série sélectionné dans son interface web sur
l’UART relié à la balance.

## Interface web de diagnostic

Les deux liaisons Bluetooth restent disponibles. Le Wi-Fi est éteint à
chaque démarrage afin que le point d’accès ne soit visible que lorsque cela est
nécessaire.

Depuis un téléphone ou un ordinateur :

1. appuyer rapidement trois fois sur le bouton de l’ATOM Lite pour activer le
   point d’accès ;
2. se connecter au Wi-Fi `LabConnect-BT-XXXX-WiFi` avec le mot de passe
   `labconnect` ;
3. ouvrir `http://192.168.4.1/` dans un navigateur ;
4. inspecter le trafic dans les deux sens en texte et en hexadécimal ;
5. envoyer au besoin une commande en ASCII ou en hexadécimal, avec une
   terminaison `CR`, `LF`, `CRLF` ou sans terminaison.

Trois nouveaux clics rapides coupent immédiatement le serveur web et le point
d’accès. L’arrêt du Wi-Fi ne coupe pas le Bluetooth ni le port COM Windows.

Le bouton **Paramètres** permet de modifier et conserver en mémoire :

- le nom Bluetooth ;
- le nom et le mot de passe du point d’accès Wi-Fi ;
- le débit, les bits de données, la parité et les bits d’arrêt de l’UART ;
- l’inversion logique des lignes RX/TX.

Les profils constructeur préparent rapidement les réglages série usuels :

| Profil | Réglage |
|---|---|
| A&D | 2400 / 7E1 |
| Mettler Toledo | 9600 / 8N1 |
| Sartorius | 9600 / 8O1 |
| KERN | 9600 / 8N1 |
| Precisa | 9600 / 7E1 |
| Radwag | 9600 / 8N1 |
| Ohaus | 9600 / 8N1 |

Ces valeurs peuvent varier selon le modèle ou la configuration enregistrée
dans la balance. Après application d’un profil, tous les champs restent
modifiables afin de les faire correspondre exactement au menu de la balance.
Les GPIO restent fixes à RX 22 et TX 19 pour protéger l’ATOM Lite contre une
configuration de broches dangereuse.

L’enregistrement provoque un redémarrage. Après un changement du nom
Bluetooth, supprimer l’ancien périphérique de Windows, refaire l’appairage et
sélectionner le nouveau port COM sortant dans RsKey ou RsCom. Après un
changement du Wi-Fi, se reconnecter au nouveau réseau avec le nouveau mot de
passe.

Le journal est conservé uniquement en mémoire vive et contient les événements
les plus récents. Il est perdu au redémarrage et n’altère jamais les données
transmises par le pont Bluetooth.

## Voyant

- rouge : aucun PC connecté ;
- bleu clignotant : mode d’appairage actif ;
- bleu : port Bluetooth connecté ;
- vert : données en transit.

## Transparence

Le firmware ne cherche aucune commande et ne parse aucune mesure. Tous les
octets, notamment `CR`, `LF`, ACK, commandes RsCom, résultats de pesée et
rapports GLP, sont transférés tels quels dans les deux sens.

Les données émises par la balance lorsqu’aucun PC n’est connecté sont
volontairement jetées pour éviter de transmettre d’anciennes mesures lors
d’une reconnexion.
