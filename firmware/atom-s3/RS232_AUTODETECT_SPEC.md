# Auto-détection RS232 des balances — spécification

## Objectif

L'AtomS3 doit détecter automatiquement une liaison de balance RS232, obtenir une
valeur de poids sans modifier l'état de la balance, identifier d'abord le
protocole puis, lorsque les réponses le permettent, la marque et éventuellement
le modèle.

Une détection reste utilisable si la marque est inconnue : le résultat minimal
accepté est un protocole de lecture, des paramètres UART confirmés et des poids
valides et répétables.

## Matériel et câblage

- AtomS3 sous Arduino/M5Unified.
- UART balance séparé du port USB de diagnostic.
- GPIO par défaut : RX 5, TX 6. Toujours vérifier l'option d'échange RX/TX.
- Transceiver RS232 3,3 V de type MAX3232 obligatoire. Ne jamais connecter les
  niveaux RS232 directement aux GPIO de l'ESP32.
- Masse de signal commune et liaison bidirectionnelle obligatoires pour le scan
  actif.
- Prévoir le cas DTE/DCE : câble droit ou null-modem. Le firmware peut échanger
  ses affectations GPIO, mais ne peut pas corriger un câblage DB9 incorrect.
- Le scan de base utilise RX, TX et GND. Les équipements exigeant RTS/CTS ne
  peuvent être garantis sans interface matérielle supplémentaire.
- Valider le brochage DB9 de chaque câble : le genre du connecteur ne suffit pas
  à déterminer RX, TX et GND.
- Pour un usage industriel, évaluer isolation galvanique, protection ESD et
  différences de potentiel de masse ; un MAX3232 seul n'assure pas l'isolation.

## Principe de détection

La détection est orientée **protocole d'abord, fabricant ensuite**.

1. Attendre la fin du démarrage de la balance et écouter brièvement une
   éventuelle émission continue.
2. Tester les profils série prioritaires des fabricants, regroupés par
   configuration UART pour éviter les reconfigurations inutiles.
3. Pour chaque profil : arrêter et redémarrer proprement l'UART, purger les
   buffers, laisser la ligne se stabiliser, envoyer une commande de lecture et
   attendre la réponse sans bloquer la boucle principale.
4. Essayer les terminateurs prévus par le profil (`CR`, `LF`, `CRLF`) sans les
   concaténer aveuglément.
5. En l'absence de réponse, répéter une fois puis passer au profil suivant.
6. Lorsqu'une réponse plausible apparaît, conserver la configuration et obtenir
   au moins une seconde réponse cohérente.
7. Classer la réponse avec le parseur du protocole, puis lancer uniquement les
   requêtes d'identification non destructives compatibles.
8. Si les profils prioritaires échouent, proposer un scan étendu puis un scan
   exhaustif optionnel.

Une réponse à `SI` prouve seulement qu'un protocole compatible SICS ou similaire
est actif. Elle ne prouve jamais à elle seule que la balance est une Mettler.

## Profils prioritaires

Les valeurs sont des priorités de scan, pas des vérités universelles. Une marque
peut avoir plusieurs gammes et plusieurs protocoles configurables.

| Fabricant / protocole | Profil prioritaire | Requête non destructive | Confirmation / identification |
| --- | --- | --- | --- |
| A&D standard | 2400/7E1 | `Q` | format A&D avec état, valeur et unité |
| Mettler MT-SICS | 9600/8N1 | `SI` | `I2`, puis `I4` si pris en charge |
| Sartorius SBI | 9600/8O1 | `ESC P` | format fixe SBI |
| Sartorius SICS/miniSICS | 9600/8N1 | `SI` | données d'identité si disponibles |
| Kern KCP | 9600/8N1 | `W` | `I0` ou signature KCP ; `S` en variante stable |
| Ohaus standard | 9600/8N1 | `IP` | signature Ohaus ; `P` en variante |
| Ohaus SICS | 9600/8N1 | `SI` | `I2` et format de réponse |
| Shimadzu | 300/8N1 puis 1200/8N1 | `D05` | réponse de poids Shimadzu |
| Radwag | 9600/8N1 | `SI` | `BN` (type), `NB` (numéro de série) |
| Dini Argeo | 9600/8N1 | `READ` | `REXT` ou `VER` après confirmation |
| Precisa | profils propres à la gamme | `PRT` | format fixe Precisa |
| Bizerba | profils propres à l'indicateur | à documenter par gamme | parseur du protocole configuré |
| Precia Molen A+ | profils propres à l'indicateur | à documenter par gamme | signature A+ et modèle d'indicateur |

La cible comprend donc onze fabricants, mais davantage de onze profils.

## Ordre du scan

### Scan rapide

Regrouper d'abord les essais par configuration :

1. 9600/8N1 : Mettler/SICS, Kern, Ohaus, Radwag, Dini Argeo.
2. 2400/7E1 : A&D.
3. 9600/8O1 : Sartorius SBI.
4. 300/8N1 et 1200/8N1 : Shimadzu.
5. Profils prioritaires confirmés pour Precisa, Bizerba et Precia Molen.

Objectif : résultat usuel en 1 à 15 secondes.

### Scan étendu

- Débits prioritaires : 2400, 4800, 9600 et 19200.
- Débits secondaires : 300, 600, 1200, 38400, 57600 et 115200.
- Formats : 8N1, 7E1, 8O1, 8E1 et 7O1.
- Ajouter les variantes à deux bits d'arrêt documentées.
- Objectif : moins de 45 à 90 secondes selon les timeouts.

### Scan exhaustif

Mode explicite, plafonné à environ deux minutes. Il ne doit pas être lancé à
chaque démarrage si un profil confirmé existe déjà en NVS.

## Temporisations

- Délai initial configurable pour laisser démarrer la balance.
- Stabilisation après reconfiguration UART : environ 50 à 100 ms.
- Timeout de ligne initial : 300 à 500 ms, ajustable par profil.
- Deux tentatives au maximum pendant le scan rapide.
- Une commande à la fois ; ne jamais superposer les fenêtres de réponse.
- Aucune attente longue avec `delay()` : machine d'états pilotée par `millis()`.

## Validation d'une réponse

Quelques octets lisibles ne constituent pas une détection. Le score doit tenir
compte de :

- trame terminée correctement ou longueur fixe reconnue ;
- caractères autorisés par le protocole ;
- présence et validité d'une valeur numérique ;
- unité reconnue ;
- indicateur stable, instable, surcharge ou sous-charge reconnu ;
- préfixes, séparateurs, longueur ou checksum propres au protocole ;
- absence d'erreur de commande ou d'un simple écho ;
- seconde réponse compatible avec la première ;
- identité constructeur ou modèle lorsqu'elle est disponible.

Le parseur doit accepter un poids nul et un poids négatif. Une réponse indiquant
instabilité, surcharge ou sous-charge confirme potentiellement le protocole même
si elle ne fournit pas un poids exploitable.

Niveaux suggérés :

- 100 % : UART, protocole et marque confirmés par plusieurs réponses.
- 80–95 % : UART et protocole confirmés, marque probable.
- 60–79 % : trames de poids cohérentes, marque indéterminée.
- moins de 60 % : ne pas sauvegarder automatiquement.

## Ambiguïtés SICS

Après une réponse valide à `SI` :

1. Classer le protocole comme SICS-compatible, sans affecter encore de marque.
2. Essayer `I2` et, si pertinent, `I4`.
3. Essayer les identifiants Radwag `BN` et `NB` seulement dans cette phase de
   discrimination.
4. Analyser les contenus et formes des réponses.
5. Si la marque reste inconnue, conserver un pilote générique SICS.

Résultat acceptable : « SICS compatible, marque indéterminée, lecture
opérationnelle ».

## Sécurité fonctionnelle

Le scanner n'envoie que des requêtes de lecture ou d'identification. Sont
interdites pendant la détection : tare, zéro, calibration, changement d'unité,
changement de mode, écriture de configuration, redémarrage et extinction.

Chaque commande intégrée doit être revue comme non destructive pour la gamme
concernée. Une même chaîne peut avoir une autre signification dans un autre
protocole ; les commandes de scan doivent donc être courtes, documentées et
ordonnées pour minimiser ce risque.

## Émission continue et handshake

- Écouter avant d'interroger : une trame continue valide peut révéler l'UART et
  le protocole sans commande.
- Ne pas envoyer de commande destinée à désactiver un flux continu pendant le
  scan.
- Ignorer proprement XON/XOFF dans les données et le gérer seulement pour les
  profils qui l'exigent.
- Signaler « handshake matériel requis » si des données cohérentes ne peuvent
  pas être obtenues avec une interface trois fils.

## Persistance et exploitation

Sauvegarder en NVS uniquement après confirmation :

- débit, bits de données, parité et bits d'arrêt ;
- affectation RX/TX retenue ;
- protocole, marque éventuelle et modèle éventuel ;
- commande de lecture, terminateur et timeout ;
- niveau de confiance et date/compteur de dernière validation.

Au démarrage suivant, tester d'abord le dernier profil connu. Après plusieurs
échecs consécutifs, revenir au scan rapide. Un appui long doit permettre de
forcer un nouveau scan et d'oublier le profil sauvegardé après confirmation.

## Interface et diagnostic

L'écran AtomS3 doit présenter :

- phase et progression du scan ;
- configuration UART testée ;
- protocole et marque détectés ;
- niveau de confiance ;
- erreur de câblage ou absence de réponse probable.

Le port USB de diagnostic doit pouvoir afficher les essais, les octets reçus en
hexadécimal et en ASCII échappé, les raisons de rejet et le score, sans exposer
ces détails sur l'écran normal.

## Intégration LabConnect existante

Le firmware actuel et le hub utilisent trois identifiants de marque et une
configuration série partagée. L'implémentation devra :

- étendre les marques sans casser les identifiants A&D, Mettler et Sartorius ;
- ajouter un identifiant de protocole distinct de l'identifiant de marque ;
- représenter une marque inconnue et un protocole générique ;
- transmettre le niveau de confiance et l'état du scan au hub ;
- mettre à jour ultérieurement l'interface du hub et ses profils ;
- appliquer réellement `stopBits` dans la configuration UART ;
- préserver la configuration manuelle comme solution de repli.

## Vérification avant livraison

- Tests unitaires des parseurs avec trames capturées, y compris réponses
  tronquées, bruitées, échos, poids négatifs, surcharge et instabilité.
- Banc de simulation série pour les onze marques et leurs variantes.
- Essais réels avec au moins une balance par protocole disponible.
- Essais câble droit/null-modem et RX/TX échangés.
- Essais à froid, balance débranchée, démarrage lent et émission continue.
- Vérification qu'aucune commande destructive n'est présente dans la table de
  scan.
- Mesure du temps maximal de chaque mode et absence de blocage de la boucle.
- Vérification NVS : profil valide, profil obsolète et remise à zéro.

## Points encore à documenter avant codage final

- Commandes de lecture et paramètres usine exacts des modèles Bizerba ciblés.
- Détail du protocole A+ et paramètres usine des indicateurs Precia Molen ciblés.
- Profils usine des familles Precisa réellement supportées.
- Besoin réel de RTS/CTS parmi les modèles retenus.
- Brochage du module RS232 et type de câble livré avec LabConnect.
- Nécessité d'une isolation galvanique et niveau de protection CEM/ESD attendu.
- Contraintes des balances homologuées : certaines peuvent refuser ou différer
  la transmission d'une valeur instable sans que la liaison soit défectueuse.
