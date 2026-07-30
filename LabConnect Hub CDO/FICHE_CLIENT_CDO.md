# LabConnect Hub CDO - Fiche terrain

## Principe

Chaque balance est reliee a un Atom Lite par RS232. L'Atom cree un port COM Bluetooth dans Windows. Optimu envoie `SI` sur ce port COM et recoit toujours une reponse compatible Mettler.

## Commandes utiles

| Commande | Effet |
| --- | --- |
| `SI` | Lire la pesee |
| `T` | Tare |
| `Z` | Zero |
| `I10` | Lire l'identifiant CDO |
| `LCINFO` | Diagnostic Atom |

## Identification

| Balance | Type | Bluetooth | Couleur LED repos | COM Windows |
| --- | --- | --- | --- | --- |
| CDO03 | A renseigner | CDO03 | Bleu | COM... |
| CDO04 | A renseigner | CDO04 | Violet | COM... |
| CDO05 | A renseigner | CDO05 | Vert | COM... |
| CDO06 | A renseigner | CDO06 | Cyan | COM... |

## Bouton Atom

| Action | Effet |
| --- | --- |
| Clic court | Test local `SI` |
| Appui long 3 s | Appairage Bluetooth pendant 120 s |
| Triple clic | Redemarrage et re-identification de la balance |

## LED

| Couleur | Signification |
| --- | --- |
| Violet au demarrage | Identification balance |
| Bleu clignotant | Appairage Bluetooth actif |
| Bleu sombre | PC connecte au port COM Bluetooth |
| Vert bref | Activite commande/reponse |
| Orange | Balance non identifiee ou test local en erreur |

## Test rapide Windows

1. Appui long sur l'Atom pour activer l'appairage.
2. Appairer le Bluetooth `CDOxx` dans Windows.
3. Relever le port COM cree par Windows.
4. Ouvrir un terminal serie sur ce COM.
5. Envoyer `SI`.
6. La reponse doit ressembler a `S S +0.1234 g`.
