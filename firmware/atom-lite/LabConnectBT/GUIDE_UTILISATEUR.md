# Guide utilisateur — LabConnect BT

## À quoi sert LabConnect BT ?

LabConnect BT permet de relier une balance à un ordinateur sans câble entre
la balance et le poste de travail.

Une fois installé, il fonctionne comme une connexion série classique. Les
logiciels tels que RsKey et RsCom peuvent continuer à être utilisés.

## Contenu nécessaire

Pour utiliser LabConnect BT, vous devez disposer :

- d’une balance compatible ;
- d’un LabConnect BT ;
- du câble adapté à votre balance ;
- d’un ordinateur Windows ou d’un Mac équipé du Bluetooth.

## Première installation

### 1. Brancher LabConnect BT

1. Éteignez la balance.
2. Branchez LabConnect BT sur la prise de communication de la balance.
3. Vérifiez que le raccordement est bien enfoncé.
4. Allumez la balance.

Le voyant rouge indique que LabConnect BT est prêt, mais qu’aucun ordinateur
n’est encore connecté.

### 2. Rendre LabConnect BT visible

Maintenez le bouton de LabConnect BT pendant environ **2,5 secondes**.

Le voyant bleu commence à clignoter. LabConnect BT est alors visible par
l’ordinateur pendant deux minutes.

### 3. Associer LabConnect BT à Windows

1. Ouvrez les **Paramètres** de Windows.
2. Sélectionnez **Bluetooth et appareils**.
3. Cliquez sur **Ajouter un appareil**.
4. Choisissez **Bluetooth**.
5. Sélectionnez le nom commençant par :

   ```text
   LabConnect-BT-
   ```

6. Si Windows affiche un code, acceptez-le.

Lorsque la connexion est établie, le voyant devient bleu fixe.

### 3 bis. Connecter un Mac

1. Ouvrez l’application **LabConnect Key**.
2. Choisissez **Bluetooth direct (Mac)**.
3. Cliquez sur **Actualiser**.
4. Sélectionnez le nom `LabConnect-BT-…-Mac`.
5. Cliquez sur **Connecter**.

Il ne faut pas chercher LabConnect dans les réglages Bluetooth du Mac :
l’application établit elle-même la connexion. Aucun port COM n’est nécessaire.

## Choisir le bon port dans RsKey ou RsCom

Windows crée généralement deux ports : un port entrant et un port sortant.
Il faut utiliser le port **sortant**.

Pour le retrouver :

1. Ouvrez **Paramètres → Bluetooth et appareils → Appareils**.
2. Cliquez sur **Plus de paramètres Bluetooth**.
3. Ouvrez l’onglet **Ports COM**.
4. Repérez la ligne contenant :
   - le nom `LabConnect-BT-...` ;
   - la mention **Sortant**.
5. Notez le numéro affiché, par exemple `COM13`.
6. Sélectionnez ce numéro dans RsKey ou RsCom.

## Utilisation quotidienne

Après la première installation :

1. allumez la balance ;
2. attendez quelques secondes ;
3. sous Windows, ouvrez RsKey, RsCom ou votre logiciel habituel et
   sélectionnez le port COM déjà enregistré ;
4. sur Mac, ouvrez LabConnect Key : il tente de rétablir automatiquement la
   dernière connexion.

Il n’est normalement pas nécessaire de refaire l’association Bluetooth.

Un ordinateur déjà associé peut retrouver LabConnect BT même lorsque celui-ci
n’est pas visible dans la liste des nouveaux appareils.

## Utiliser le bouton

### Appui long

Maintenez le bouton pendant 2,5 secondes pour associer un nouvel ordinateur.

Le voyant bleu clignote pendant la période d’association.

### Trois clics rapides

Appuyez rapidement trois fois pour rendre disponible l’écran de configuration.

Appuyez à nouveau trois fois pour le fermer.

L’écran de configuration est automatiquement fermé après chaque redémarrage.
Cette action ne coupe pas la connexion utilisée par RsKey ou RsCom.

## Signification du voyant

| Couleur | Signification |
|---|---|
| Rouge fixe | LabConnect BT est prêt, mais aucun ordinateur n’est connecté |
| Bleu clignotant | LabConnect BT attend l’association avec un ordinateur |
| Bleu fixe | Un ordinateur est connecté |
| Vert | Une information est en cours de transmission |

## Ouvrir l’écran de configuration

L’écran de configuration sert à choisir le type de balance et à vérifier les
échanges.

### 1. Activer l’accès

Appuyez rapidement trois fois sur le bouton de LabConnect BT.

### 2. Se connecter

Sur un téléphone ou un ordinateur :

1. ouvrez la liste des réseaux Wi-Fi ;
2. sélectionnez le réseau dont le nom se termine par `-WiFi` ;
3. saisissez le mot de passe :

   ```text
   labconnect
   ```

Le téléphone peut indiquer que ce réseau ne donne pas accès à Internet. C’est
normal : ce réseau sert uniquement à régler LabConnect BT.

### 3. Ouvrir la page

Ouvrez votre navigateur et saisissez :

```text
http://192.168.4.1/
```

Lorsque vous avez terminé, appuyez trois fois sur le bouton pour fermer
l’accès.

## Choisir le type de balance

1. Ouvrez l’écran de configuration.
2. Cliquez sur **Paramètres**.
3. Choisissez le fabricant de la balance :
   - A&D ;
   - Mettler Toledo ;
   - Sartorius ;
   - KERN ;
   - Precisa ;
   - Radwag ;
   - Ohaus.
4. Cliquez sur **Appliquer le profil**.
5. Cliquez sur **Enregistrer et redémarrer**.

LabConnect BT redémarre automatiquement. L’accès de configuration est alors
fermé, mais la connexion Bluetooth reste disponible.

Les réglages peuvent varier entre deux balances d’un même fabricant. Si le
profil proposé ne fonctionne pas, contactez votre fournisseur avec la
référence exacte de la balance.

## Modifier le nom de LabConnect BT

Dans **Paramètres**, vous pouvez modifier :

- le nom affiché dans la liste Bluetooth ;
- le nom de l’accès de configuration ;
- le mot de passe de l’accès de configuration.

Après avoir changé le nom Bluetooth :

1. supprimez l’ancien LabConnect BT dans les paramètres Windows ;
2. maintenez le bouton pendant 2,5 secondes ;
3. recommencez l’association ;
4. vérifiez le nouveau numéro de port COM ;
5. sélectionnez ce nouveau port dans RsKey ou RsCom.

## Vérifier les échanges

La page principale affiche les informations qui circulent entre la balance et
l’ordinateur.

Elle permet de vérifier :

- si la balance envoie une mesure ;
- si l’ordinateur envoie une demande ;
- si LabConnect BT transmet les informations dans les deux sens.

Cette page est principalement destinée au diagnostic. Il n’est pas nécessaire
de l’ouvrir pendant l’utilisation normale.

## Restaurer les réglages d’origine

1. Ouvrez l’écran de configuration.
2. Cliquez sur **Paramètres**.
3. Cliquez sur **Valeurs d’origine**.
4. Confirmez la demande.

LabConnect BT redémarre avec :

- son nom d’origine ;
- le mot de passe `labconnect` ;
- le profil A&D ;
- les autres options remises à leur position initiale.

Après cette opération, il peut être nécessaire de refaire l’association dans
Windows.

## En cas de problème

### LabConnect BT n’apparaît pas dans Windows

- Maintenez le bouton pendant 2,5 secondes.
- Vérifiez que le voyant bleu clignote.
- Relancez immédiatement la recherche Bluetooth.
- Vérifiez qu’un autre ordinateur n’est pas déjà connecté.

### LabConnect BT est associé, mais RsKey ne le trouve pas

- Vérifiez le numéro du port COM sortant.
- Fermez RsKey et ouvrez-le à nouveau.
- Vérifiez qu’un autre logiciel n’utilise pas déjà ce port.
- Redémarrez Windows si le port n’apparaît toujours pas.

### Aucune mesure n’arrive

- Vérifiez que le voyant est bleu fixe.
- Vérifiez le port sélectionné dans le logiciel.
- Vérifiez que le câble est correctement branché.
- Vérifiez que le bon fabricant a été choisi dans les paramètres.
- Vérifiez que la balance est réglée pour envoyer ses mesures.

### Les informations reçues sont illisibles

Le réglage de communication ne correspond probablement pas à celui de la
balance. Sélectionnez le bon profil ou contactez votre fournisseur avec la
référence exacte de la balance.

### L’accès de configuration n’apparaît pas

- Appuyez trois fois rapidement sur le bouton.
- Attendez quelques secondes.
- Actualisez la liste des réseaux Wi-Fi.
- Vérifiez que vous avez effectué trois clics courts et non un appui long.

### La page de configuration ne s’ouvre pas

- Vérifiez que vous êtes connecté au réseau LabConnect BT.
- Acceptez de rester connecté même si le téléphone signale l’absence
  d’Internet.
- Saisissez `http://192.168.4.1/` et non une adresse commençant par `https`.

## Aide-mémoire

| Ce que vous voulez faire | Action |
|---|---|
| Associer un ordinateur | maintenir le bouton 2,5 secondes |
| Ouvrir la configuration | trois clics rapides |
| Fermer la configuration | trois clics rapides |
| Ouvrir la page | `http://192.168.4.1/` |
| Mot de passe initial | `labconnect` |
| Utiliser RsKey ou RsCom | choisir le port COM sortant |
