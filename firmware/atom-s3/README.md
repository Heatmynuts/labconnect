# LabConnect AtomS3

## Firmware recommande pour LabConnect Hub

Flasher ce sketch :

```text
firmware/atom-s3/LabConnectAtomS3Node/LabConnectAtomS3Node.ino
```

Ce dossier est autonome et adapte a Arduino IDE. La marque de la balance, les parametres
RS232 et les commandes sont definis ensuite depuis l'interface du knob.

Les sketchs suivants restent disponibles uniquement comme points d'entree de
compatibilite :

```text
firmware/atom-s3/atoms3-node-and/atoms3-node-and.ino
firmware/atom-s3/atoms3-node-mettler/atoms3-node-mettler.ino
```

Ils ne dupliquent plus le code : ils fixent seulement des valeurs par defaut,
puis incluent le meme coeur commun :

```text
firmware/atom-s3/LabConnectAtomS3Node/LabConnectAtomS3NodeCore.h
```

Regle de maintenance : toute correction RS232, parsing, identite balance ou
affichage AtomS3 Hub doit etre faite dans `LabConnectAtomS3NodeCore.h`.

## LabConnect Print

`LabConnectPrintAtomS3` reste pour l'instant le firmware applicatif Print :
il gere son propre point d'acces Wi-Fi, ses WebSockets et ses commandes admin.
Il compile toujours separement. La prochaine mutualisation logique sera
d'extraire le coeur balance/RS232 commun pour que Print et Hub partagent aussi
les memes profils et parseurs.
