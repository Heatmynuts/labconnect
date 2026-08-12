# LabConnect CDO — fiche terrain

## Correspondance

| Balance | Identifiant Bluetooth | Port Optimu |
|---|---|---|
| A&D MC-30K | `CDO02` | COM2 |
| A&D MC-6100 | `CDO03` | COM3 |
| Mettler XP504 | `CDO04` | COM4 |
| A&D BA-225 | `CDO05` | COM5 |
| Mettler XP56 | `CDO06` | COM6 |

Le numéro COM est attribué dans Windows. Toujours sélectionner le port
Bluetooth **sortant**.

## Réglages des balances

Toutes : `9600 / 8N1`, aucun contrôle de flux, terminaison `CRLF`.

Les A&D doivent également être réglées sur le **format MT**. Chaque balance
doit contenir son identifiant exact `CDO02` à `CDO06`.

Pour les A&D, l’identifiant série est numérique : seuls les deux derniers
chiffres sont retenus. `000003` et `0000003` deviennent donc `CDO03` ;
`0000000` est refusé car aucune identité n’a été programmée.

## Mise en service

1. Allumer la balance et connecter son ATOM.
2. Attendre que le voyant cesse de clignoter en rouge.
3. Maintenir le bouton 2,5 secondes : le voyant clignote en bleu.
4. Associer `CDOxx` dans les réglages Bluetooth de Windows.
5. Relever le port COM sortant dans le Gestionnaire de périphériques.
6. Lui affecter le numéro du tableau, puis sélectionner ce port dans Optimu.
7. Demander une mesure dans Optimu et comparer avec l’affichage de la balance.

## Voyant

| État | Signification |
|---|---|
| Orange fixe | Recherche de l’identité de la balance |
| Rouge clignotant | Balance absente, mal réglée ou identité invalide |
| Blanc clignotant lent | Prêt, PC non connecté |
| Bleu clignotant | Appairage actif pendant 120 secondes |
| Bleu fixe | Optimu connecté |
| Vert bref | Données échangées |

## Diagnostic

Effectuer un triple clic, puis rejoindre le Wi-Fi `CDOxx-Diag` avec le mot de
passe `labconnect`. Ouvrir `http://192.168.4.1/`.

La page est uniquement un moniteur : elle ne peut ni commander la balance, ni
changer ses réglages. Un nouveau triple clic coupe le Wi-Fi.

## Si l’identité n’est pas trouvée

Vérifier dans cet ordre :

1. alimentation de la balance ;
2. câble et adaptateur RS-232 M5Stack ;
3. réglage `9600/8N1`, sans contrôle de flux, CRLF ;
4. identifiant enregistré dans la balance ;
5. diagnostic Wi-Fi ou console USB de l’ATOM.

L’ATOM réessaie automatiquement et n’annonce aucun nom Bluetooth incorrect.

## Remplacement

Le firmware est identique pour les cinq ATOM. Après remplacement :

1. connecter le nouvel ATOM à la balance ;
2. attendre l’identification automatique ;
3. supprimer l’ancien périphérique Bluetooth dans Windows ;
4. associer le nouveau `CDOxx` ;
5. réaffecter son ancien numéro COM.
