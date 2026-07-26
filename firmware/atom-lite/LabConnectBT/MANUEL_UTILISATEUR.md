# Manuel utilisateur — LabConnect BT

## 1. Présentation

LabConnect BT remplace le câble série entre une balance équipée d’une
interface RS-232 et un ordinateur. Il transmet les données dans les deux sens
sans modifier les commandes, les mesures ou les caractères de terminaison.

L’ordinateur voit LabConnect BT comme un port série Bluetooth. Une interface
web locale permet aussi de surveiller le trafic, d’envoyer une commande et de
modifier la configuration.

Ce manuel correspond au firmware pour **M5Stack ATOM Lite original
(ESP32-PICO-D4)** et adaptateur RS-232 M5Stack.

> L’ATOM S3 n’est pas compatible avec cette version : l’ESP32-S3 ne prend pas
> en charge le Bluetooth Classic SPP utilisé pour créer le port série.

## 2. Consignes importantes

- Utiliser obligatoirement un adaptateur de niveaux RS-232 entre la balance et
  l’ATOM Lite.
- Ne jamais connecter directement une ligne RS-232 à un GPIO de l’ESP32.
- Les broches utilisées par le firmware sont RX GPIO 22 et TX GPIO 19.
- Les paramètres série du LabConnect doivent correspondre à ceux enregistrés
  dans la balance.
- Les profils constructeur sont des valeurs usuelles. Ils peuvent varier
  suivant le modèle ou la configuration de la balance.

## 3. Démarrage rapide sous Windows

1. Relier LabConnect BT au port RS-232 de la balance.
2. Mettre la balance et LabConnect BT sous tension.
3. Maintenir le bouton de l’ATOM pendant 2,5 secondes.
4. Attendre que le voyant bleu clignote.
5. Ouvrir **Paramètres Windows → Bluetooth et appareils → Ajouter un
   appareil → Bluetooth**.
6. Sélectionner le périphérique `LabConnect-BT-XXXX`.
7. Accepter le code de confirmation si Windows en affiche un. Aucune action
   supplémentaire n’est nécessaire sur l’ATOM.
8. Ouvrir **Plus de paramètres Bluetooth → Ports COM**.
9. Relever le port **Sortant** associé au service `ESP32SPP`.
10. Sélectionner ce port COM dans RsKey, RsCom ou le logiciel de la balance.

Exemple :

```text
COM13  Sortant  LabConnect-BT-4B00 'ESP32SPP'
```

Dans cet exemple, il faut sélectionner `COM13`. Le port entrant ne doit pas
être utilisé par RsKey ou RsCom.

## 4. Commandes du bouton

### 4.1 Appui long : mode d’appairage Bluetooth

Maintenir le bouton pendant **2,5 secondes** active le mode d’appairage.

- Le voyant bleu clignote.
- LabConnect BT devient visible dans la recherche Bluetooth.
- Le mode reste actif pendant 120 secondes au maximum.
- Il s’arrête automatiquement dès qu’un ordinateur se connecte.
- Après expiration, l’appareil redevient non détectable.

Un ordinateur déjà appairé peut se reconnecter lorsque LabConnect BT est non
détectable. Le mode d’appairage ne peut pas être lancé lorsqu’un client
Bluetooth est déjà connecté.

### 4.2 Triple clic : interface Wi-Fi

Trois clics rapides activent le point d’accès Wi-Fi et le serveur web.

Trois nouveaux clics rapides les désactivent immédiatement.

Le Wi-Fi est toujours désactivé après un démarrage ou une coupure
d’alimentation. L’activation ou la désactivation du Wi-Fi ne coupe pas le
Bluetooth et n’interrompt pas le port COM.

## 5. Signification du voyant

| Voyant | État |
|---|---|
| Rouge fixe | Aucun ordinateur connecté en Bluetooth |
| Bleu clignotant | Mode d’appairage actif |
| Bleu fixe | Ordinateur connecté au port série Bluetooth |
| Vert | Données en transit |

Le vert apparaît brièvement lors d’un échange entre la balance, le PC ou
l’interface web.

## 6. Modes de fonctionnement

### 6.1 Mode normal

Au démarrage :

- le pont RS-232/Bluetooth est actif ;
- le Wi-Fi est désactivé ;
- le Bluetooth est connectable par les ordinateurs déjà appairés ;
- le Bluetooth n’est pas visible pour les nouveaux ordinateurs ;
- le voyant reste rouge tant qu’aucun client n’est connecté.

### 6.2 Mode d’appairage

Ce mode rend temporairement LabConnect BT visible. Il est destiné à
l’installation initiale ou à l’association avec un nouvel ordinateur.

### 6.3 Mode connecté

Le voyant bleu fixe indique qu’un logiciel peut ouvrir le port COM. Les
données sont alors transférées :

```text
Balance RS-232 ⇄ ATOM Lite ⇄ Bluetooth SPP ⇄ Port COM
```

Les réglages affichés dans les propriétés du port COM Windows ne changent pas
le format électrique appliqué à la balance. Le format réel est celui
sélectionné dans l’interface web de LabConnect BT.

### 6.4 Mode configuration et diagnostic

Le triple clic active temporairement un réseau Wi-Fi local. Ce réseau ne
fournit pas d’accès à Internet. Il sert uniquement à atteindre l’interface de
LabConnect BT.

## 7. Accéder à l’interface web

1. Effectuer trois clics rapides sur le bouton.
2. Ouvrir les réglages Wi-Fi du téléphone ou de l’ordinateur.
3. Se connecter au réseau :

   ```text
   LabConnect-BT-XXXX-WiFi
   ```

4. Saisir le mot de passe par défaut :

   ```text
   labconnect
   ```

5. Ouvrir un navigateur à l’adresse :

   ```text
   http://192.168.4.1/
   ```

Le téléphone peut signaler que ce réseau n’a pas accès à Internet. Il faut
rester connecté à ce réseau pour utiliser l’interface.

Pour fermer l’interface et masquer le Wi-Fi, effectuer trois nouveaux clics
rapides.

## 8. Moniteur de trafic

Le moniteur affiche trois directions :

| Indication | Signification |
|---|---|
| `BAL → PC` | Données reçues de la balance et envoyées au Bluetooth |
| `PC → BAL` | Commandes Bluetooth envoyées à la balance |
| `WEB → BAL` | Commandes injectées depuis l’interface web |

Chaque événement présente :

- le temps écoulé depuis le démarrage ;
- la direction ;
- une représentation texte ;
- la représentation hexadécimale des mêmes octets.

Les commandes disponibles permettent de :

- suspendre ou reprendre l’affichage ;
- filtrer une direction ;
- activer le défilement automatique ;
- effacer uniquement l’écran du navigateur.

Le journal est conservé en mémoire vive. Il est perdu au redémarrage. Effacer
l’écran ne modifie pas les données transmises au PC ou à la balance.

## 9. Envoyer une commande à la balance

L’interface peut injecter directement une commande sur l’UART de la balance.

1. Saisir la commande.
2. Choisir **Texte ASCII** ou **Hexadécimal**.
3. Choisir la terminaison :
   - `CRLF` ;
   - `CR` ;
   - `LF` ;
   - aucune.
4. Cliquer sur **Envoyer**.

Exemple A&D :

```text
Commande : Q
Format : Texte ASCII
Terminaison : CRLF
```

Cette fonction sert au diagnostic. Une commande incorrecte peut modifier
l’état de la balance ; consulter son manuel avant d’envoyer une commande
inconnue.

## 10. Ouvrir les paramètres

1. Activer le Wi-Fi par triple clic.
2. Ouvrir `http://192.168.4.1/`.
3. Cliquer sur **Paramètres**.

Les valeurs enregistrées sont conservées après une coupure d’alimentation.
Cliquer sur **Enregistrer et redémarrer** applique les changements.

Après le redémarrage, le Wi-Fi est de nouveau désactivé.

## 11. Paramètres Bluetooth et Wi-Fi

### 11.1 Nom Bluetooth

- Longueur : 1 à 28 caractères ASCII.
- Valeur par défaut : `LabConnect-BT-XXXX`.
- `XXXX` est dérivé de l’identifiant matériel de l’ATOM.

Après un changement de nom :

1. supprimer l’ancien appareil Bluetooth dans Windows ;
2. redémarrer LabConnect BT ;
3. maintenir le bouton 2,5 secondes ;
4. refaire l’appairage ;
5. relever le nouveau port COM sortant.

### 11.2 Nom du Wi-Fi

- Longueur : 1 à 32 caractères ASCII.
- Valeur par défaut : `LabConnect-BT-XXXX-WiFi`.

### 11.3 Mot de passe Wi-Fi

- Longueur : 8 à 63 caractères.
- Valeur par défaut : `labconnect`.

Après modification, utiliser le nouveau nom de réseau et le nouveau mot de
passe lors de la prochaine activation par triple clic.

## 12. Paramètres série

### 12.1 Débit

Valeurs proposées :

```text
300, 600, 1200, 2400, 4800, 9600,
19200, 38400, 57600, 115200 bauds
```

### 12.2 Bits de données

- 7 bits ;
- 8 bits.

### 12.3 Parité

- aucune, notée `N` ;
- paire, notée `E` ;
- impaire, notée `O`.

### 12.4 Bits d’arrêt

- 1 bit ;
- 2 bits.

La notation `2400/7E1` signifie :

- 2400 bauds ;
- 7 bits de données ;
- parité paire ;
- 1 bit d’arrêt.

### 12.5 Inversion RX/TX

L’option **Inverser RX et TX** échange l’utilisation logique des GPIO 22 et
19. Elle permet de corriger un adaptateur ou un câble dont les lignes sont
croisées.

Les numéros de GPIO restent fixes. Ne modifier cette option que si aucune
donnée n’est reçue avec le câblage normal.

## 13. Profils constructeur

Sélectionner un profil puis cliquer sur **Appliquer le profil**. Vérifier les
valeurs, puis cliquer sur **Enregistrer et redémarrer**.

| Constructeur | Profil proposé |
|---|---|
| A&D | 2400 / 7E1 |
| Mettler Toledo | 9600 / 8N1 |
| Sartorius | 9600 / 8O1 |
| KERN | 9600 / 8N1 |
| Precisa | 9600 / 7E1 |
| Radwag | 9600 / 8N1 |
| Ohaus | 9600 / 8N1 |

Ces profils règlent uniquement le format série de LabConnect BT. Ils ne
modifient pas la configuration interne de la balance et n’envoient aucune
commande constructeur.

Toujours vérifier le débit, les bits, la parité et les bits d’arrêt dans le
manuel ou le menu de la balance.

## 14. Restaurer les valeurs d’origine

Dans **Paramètres**, cliquer sur **Valeurs d’origine**, puis confirmer.

Les valeurs restaurées sont :

| Paramètre | Valeur d’origine |
|---|---|
| Nom Bluetooth | `LabConnect-BT-XXXX` |
| Nom Wi-Fi | `LabConnect-BT-XXXX-WiFi` |
| Mot de passe Wi-Fi | `labconnect` |
| Format série | 2400 / 7E1 |
| Inversion RX/TX | désactivée |

L’appareil redémarre automatiquement. Le Wi-Fi reste désactivé après le
redémarrage.

## 15. Utilisation sous macOS

macOS ne crée pas de port série à partir du service Bluetooth Classic utilisé
par Windows. Le même firmware propose donc un second canal, utilisé
directement par l’application **LabConnect Key**.

1. Mettre LabConnect BT en mode d’appairage par un appui de 2,5 secondes. Le
   voyant bleu clignote.
2. Ouvrir LabConnect Key.
3. Choisir **Bluetooth direct (Mac)**.
4. Cliquer sur **Actualiser**.
5. Sélectionner `LabConnect-BT-XXXX-Mac`.
6. Cliquer sur **Connecter**.

Il n’est pas nécessaire d’associer l’appareil dans les réglages Bluetooth de
macOS, et aucun fichier `/dev/cu.*` n’est attendu. LabConnect Key transmet les
mesures, permet d’envoyer une commande à la balance et tente une reconnexion
automatique après une mise en veille.

RsKey et RsCom restent des logiciels Windows.

## 16. Dépannage

### LabConnect BT n’apparaît pas dans la recherche Bluetooth

- Vérifier que le voyant bleu clignote.
- Maintenir le bouton pendant 2,5 secondes.
- Recommencer la recherche dans les deux minutes.
- Vérifier qu’aucun autre ordinateur n’est déjà connecté.
- Si l’appareil est déjà connu de Windows, le supprimer avant de refaire
  l’appairage.

### L’appareil est appairé, mais aucun port COM n’apparaît

1. Ouvrir **Plus de paramètres Bluetooth**.
2. Ouvrir l’onglet **Ports COM**.
3. Ajouter si nécessaire un port **Sortant**.
4. Choisir LabConnect BT et le service `ESP32SPP`.
5. Redémarrer Windows si le numéro COM n’apparaît toujours pas dans le
   Gestionnaire de périphériques.

### RsKey ou RsCom ne reçoit aucune mesure

- Utiliser le port COM **sortant**.
- Fermer les autres logiciels qui pourraient déjà avoir ouvert ce port.
- Vérifier le profil série de LabConnect BT.
- Vérifier la configuration série de la balance.
- Vérifier le câble et essayer **Inverser RX et TX**.
- Activer le moniteur web pour déterminer si la balance émet des octets.

### Le Wi-Fi n’apparaît pas

- Le Wi-Fi est désactivé par défaut.
- Effectuer trois clics distincts et rapides.
- Attendre quelques secondes, puis relancer la recherche Wi-Fi.
- Ne pas maintenir le bouton : un appui long commande le Bluetooth, pas le
  Wi-Fi.

### L’interface `192.168.4.1` ne s’ouvre pas

- Vérifier que l’appareil utilisé est toujours connecté au Wi-Fi LabConnect.
- Désactiver temporairement les données mobiles si le téléphone quitte le
  réseau sans Internet.
- Saisir exactement `http://192.168.4.1/`, sans `https`.

### Les caractères affichés sont illisibles

Le débit, le nombre de bits ou la parité ne correspondent probablement pas à
la balance. Relever les paramètres de la balance et appliquer exactement les
mêmes valeurs dans LabConnect BT.

### Le nom Bluetooth a changé, mais Windows utilise encore l’ancien

Supprimer l’ancien périphérique Bluetooth et ses ports COM, puis refaire
l’appairage. Windows peut conserver les ports associés à l’ancien nom.

## 17. Fonctionnement transparent

Le firmware ne convertit pas les mesures et ne reconnaît pas les commandes.
Il transfère les octets tels quels, notamment :

- `CR` et `LF` ;
- commandes de pesée, tare et zéro ;
- réponses stables ou instables ;
- rapports GLP ;
- caractères de contrôle.

Les données produites par la balance lorsqu’aucun client Bluetooth n’est
connecté sont volontairement rejetées. Cela évite d’envoyer d’anciennes
mesures lorsqu’un ordinateur se reconnecte.

## 18. Référence rapide

| Action | Commande |
|---|---|
| Activer l’appairage | maintenir le bouton 2,5 secondes |
| Activer le Wi-Fi | trois clics rapides |
| Désactiver le Wi-Fi | trois clics rapides |
| Ouvrir l’interface | `http://192.168.4.1/` |
| Mot de passe Wi-Fi d’origine | `labconnect` |
| Réglage série d’origine | 2400 / 7E1 |
| Port à utiliser sous Windows | port COM sortant `ESP32SPP` |
