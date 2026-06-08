# LabConnect Print

LabConnect Print cible un combo simple et exploitable :

```text
A&D FZ-5000i
RS-232 2400 7E1
        ↓
AtomS3 / ESP32-S3
Wi-Fi
        ↓
SUNMI V3Mix
App LabConnect + imprimante intégrée
```

## Principes validés

- L'application tourne directement sur le SUNMI V3Mix.
- Le SUNMI imprime avec son imprimante intégrée.
- La balance transmet au SUNMI via un AtomS3 connecté en série à la balance.
- Les actions balance utilisent des commandes série envoyées au travers de l'AtomS3.
- Pour le moment, la ligne brute balance est affichée telle quelle, sans parsing.
- Endpoint AtomS3 par défaut : `ws://192.168.4.1/ws`.
- L'application envoie au WebSocket des commandes lisibles (`tare`, `zero`, `request-weight`, `print`).
- L'AtomS3 traduit ces commandes vers la balance :
  - tare : `T`
  - zero : `RZ`
  - demande poids : `Q`
  - print : `P`
- La balance fonctionne en stream mode : chaque ligne reçue sert à mettre à jour la valeur affichée en temps réel.
- Une ligne reçue de la balance ne déclenche jamais l'impression automatiquement.
- Seule l'application déclenche l'impression, via le bouton d'impression du ticket.
- L'historique local des pesées est conservé dans l'application.
- Le ticket standard supporte 58 mm et 80 mm, avec 80 mm par défaut.
- Le ticket prévoit logo société, lot, opérateur, date, signature, QR code et nombre de copies.

## À brancher côté intégration

- API AtomS3 WebSocket : réception ligne brute et envoi commandes série.
- Stockage local natif ou base embarquée pour remplacer le stockage navigateur.
- Exports CSV et PDF natifs.

## Impression SUNMI V3Mix

- L'imprimante intégrée est appelée depuis Android via le service système `woyou.aidlservice.jiuiv5`.
- Le manifeste déclare la visibilité du package `woyou.aidlservice.jiuiv5` pour éviter le blocage Android 11+.
- LabConnect utilise un bridge Android injecté dans la WebView : `window.LabConnectSunmiPrinter.printTicket(...)`.
- Le bridge utilise des transactions Binder explicites pour respecter l'ordre réel extrait du firmware SUNMI :
  - `printerInit` : transaction 4
  - `lineWrap` : transaction 10
  - `sendRAWData` : transaction 11
  - `setAlignment` : transaction 12
  - `setFontSize` : transaction 14
  - `printText` : transaction 15
