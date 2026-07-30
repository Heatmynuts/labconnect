# LabConnect Hub CDO

Firmware specifique client CDO pour 5 balances reliees chacune a un M5Stack Atom Lite via adaptateur RS232.

## Materiel

- M5Stack Atom Lite, pas AtomS3.
- Adaptateur RS232 M5Stack.
- Balance Mettler ou A&D.
- PC Windows avec Optimu.

## Sketch Arduino

Ouvrir :

`firmware/atom-lite-cdo/LabConnectHubCDO/LabConnectHubCDO.ino`

Board Arduino :

`M5Stack > M5Atom`

## Principe

Au demarrage, l'Atom tente :

1. Mettler `I10` en `9600/8N1`.
2. Si pas de reponse Mettler valide, A&D `?ID` en `2400/7E1`.

L'identifiant detecte devient le nom Bluetooth, par exemple `CDO04`.
Un meme firmware peut donc etre flashe sur tous les Atom Lite.

## Commandes cote Optimu / port COM Bluetooth

- `SI` : demande le poids.
- `T` : tare.
- `Z` : zero.
- `I10` : renvoie l'identifiant normalise, par exemple `I10 A "CDO04"`.
- `?ID` : renvoie l'identifiant A&D-like, par exemple `ID,04`.
- `LCINFO` : diagnostic rapide.

Pour les balances A&D, `SI` est tente puis le firmware bascule automatiquement sur `Q` si necessaire. La reponse est renvoyee dans un format proche Mettler `S S +12.345 g`.

## LED

- Violet : demarrage / identification balance.
- Pret sans client Bluetooth connecte :
  - `CDO03` : bleu.
  - `CDO04` : violet.
  - `CDO05` : vert.
  - `CDO06` : cyan.
  - identifiant inconnu : rouge sombre.
- Bleu sombre : client Bluetooth COM connecte.
- Vert bref : activite commande/reponse.
- Bleu clignotant : mode appairage Bluetooth actif.
- Orange : balance non identifiee ou test local en erreur.

## Bouton Atom

- Clic court : test local `SI` vers la balance, resultat dans la console USB.
- Appui long 3 secondes : active/desactive l'appairage Bluetooth pendant 120 secondes.
- Triple clic : redemarre l'Atom pour relancer l'identification de la balance et renommer le Bluetooth si besoin.
