# LabConnect — Design System & UX Guidelines v3

Document de référence pour générer toutes les interfaces de la suite logicielle **LabConnect**.

Ce fichier doit être utilisé par Claude Code comme base produit, UX, UI et design system.

---

# 1. Vision produit

## 1.1 Positionnement

LabConnect est une **plateforme connectée pour instruments et équipements de laboratoire**.

LabConnect ne doit pas être traité comme un simple dashboard.

LabConnect doit se comporter comme un **écosystème d’équipements**, proche de l’expérience Sonos :

- l’utilisateur voit ses équipements ;
- il voit ses groupes ;
- il voit ses zones ;
- il ajoute facilement un équipement ;
- il entre dans le détail en cliquant sur l’objet réel ;
- il contrôle l’équipement depuis une interface claire ;
- il retrouve la même logique sur PC, tablette, iPhone, Android et SUNMI.

## 1.2 Phrase de positionnement

```text
Connected Instruments Platform
```

Phrase produit :

```text
Connecter. Contrôler. Simplifier.
```

## 1.3 Ce que LabConnect doit évoquer

- technologie ;
- simplicité ;
- instruments connectés ;
- écosystème ;
- contrôle en temps réel ;
- supervision claire ;
- fiabilité ;
- expérience premium.

## 1.4 Ce que LabConnect ne doit pas évoquer

- SCADA ancien ;
- ERP industriel ;
- logiciel scientifique gris ;
- dashboard SaaS générique ;
- interface médicale classique ;
- laboratoire cliché avec béchers, molécules ou ADN.

---

# 2. Direction d’expérience : Sonos pour les équipements de laboratoire

## 2.1 Principe principal

LabConnect doit être conçu comme une expérience de gestion d’écosystème.

L’utilisateur ne doit pas d’abord voir des tableaux.

Il doit d’abord voir :

```text
Ses groupes
Ses équipements
Ses zones
Leur état
Les actions disponibles
```

## 2.2 Équivalence Sonos → LabConnect

```text
Pièce Sonos              = Groupe / zone LabConnect
Enceinte Sonos           = Équipement LabConnect
Système Sonos            = Laboratoire / site client
Lecture en cours         = Action ou mesure en cours
Groupement d’enceintes   = Regroupement d’équipements
Ajout d’enceinte         = Ajout d’équipement
```

## 2.3 Accueil recommandé

L’accueil doit montrer :

1. l’état global du laboratoire ;
2. les groupes ;
3. les équipements principaux ;
4. l’activité récente ;
5. les actions rapides.

Structure recommandée :

```text
LabConnect
Laboratoire Central
6 équipements · 3 groupes · Tout est opérationnel

Groupes
[Salle de pesée] [Étiquetage] [Préparation]

Équipements
[Balance] [SUNMI] [Zebra] [Pompe] [Enceinte]

Activité récente
[Pesée enregistrée] [Étiquette imprimée] [Pompe démarrée]
```

---

# 3. Marque et logo

## 3.1 Nom

```text
LabConnect
```

La marque doit être plus visible que dans un simple menu.

Sur desktop, le logo doit être présent dans la sidebar ou la topbar avec une taille suffisante.

## 3.2 Modules

Format :

```text
LabConnect Hub
LabConnect Balance
LabConnect Dose
LabConnect Labeler
LabConnect Vision
LabConnect Capture
LabConnect Monitor
```

Le nom du module peut apparaître comme contexte, mais la marque principale reste LabConnect.

## 3.3 Symbole de marque

Créer un symbole propriétaire autour de :

- nœuds ;
- connexion ;
- cube / réseau ;
- écosystème ;
- flux.

Ne pas utiliser un symbole représentant une balance, une pompe, une imprimante ou un bécher.

## 3.4 Niveau de présence de marque

LabConnect doit être visible à trois niveaux :

1. logo principal ;
2. icône dans l’application mobile / SUNMI ;
3. signature graphique des cartes, groupes et connexions.

---

# 4. Typographie

## 4.1 Police principale

Police recommandée :

```text
Geist
```

Fallback :

```css
font-family: "Geist", "Inter", "SF Pro Display", "Segoe UI", Arial, sans-serif;
```

## 4.2 Hiérarchie

### Titre principal

```css
font-size: 32px;
line-height: 40px;
font-weight: 700;
letter-spacing: -0.04em;
```

### Titre secondaire

```css
font-size: 22px;
line-height: 30px;
font-weight: 650;
letter-spacing: -0.02em;
```

### Titre de carte

```css
font-size: 16px;
line-height: 22px;
font-weight: 650;
```

### Texte courant

```css
font-size: 14px;
line-height: 22px;
font-weight: 400;
```

### Texte secondaire

```css
font-size: 13px;
line-height: 18px;
font-weight: 400;
```

### Valeur principale

```css
font-size: 40px;
line-height: 48px;
font-weight: 700;
letter-spacing: -0.04em;
font-variant-numeric: tabular-nums;
```

Sur mobile / SUNMI :

```css
font-size: 36px;
line-height: 44px;
```

---

# 5. Couleurs

## 5.1 Thème clair par défaut

```css
--background: #F7F8FA;
--surface: #FFFFFF;
--surface-soft: #F9FAFB;
--surface-hover: #F3F6FB;
--border: #E5E7EB;
--border-strong: #CBD5E1;

--text-primary: #0F172A;
--text-secondary: #475569;
--text-muted: #64748B;
--text-disabled: #94A3B8;

--brand: #2563EB;
--brand-hover: #1D4ED8;
--brand-soft: #EFF6FF;
--brand-border: #BFDBFE;

--success: #16A34A;
--success-soft: #ECFDF3;

--warning: #F59E0B;
--warning-soft: #FFFBEB;

--danger: #DC2626;
--danger-soft: #FEF2F2;

--info: #06B6D4;
--info-soft: #ECFEFF;
```

## 5.2 Mode sombre

Le mode sombre est disponible mais secondaire.

```css
--dark-background: #08111F;
--dark-surface: #101827;
--dark-surface-soft: #111F33;
--dark-border: #243247;

--dark-text-primary: #F8FAFC;
--dark-text-secondary: #CBD5E1;
--dark-text-muted: #94A3B8;
```

## 5.3 Couleurs modules

```css
--module-hub: #2563EB;
--module-balance: #2563EB;
--module-dose: #16A34A;
--module-labeler: #F59E0B;
--module-vision: #9333EA;
--module-capture: #06B6D4;
--module-monitor: #DC2626;
```

## 5.4 Usage couleur

Règle :

```text
90 % neutre
10 % couleur active
```

La couleur sert à :

- bouton principal ;
- état actif ;
- module actif ;
- icône sélectionnée ;
- connexion active ;
- badge important.

---

# 6. Layout général

## 6.1 Desktop

Structure :

```text
Sidebar repliable
Contenu principal
Panneau contextuel optionnel
```

La sidebar doit être visible sur PC et repliable.

Largeur ouverte :

```css
width: 260px;
```

Largeur repliée :

```css
width: 72px;
```

## 6.2 Tablette

Structure :

```text
Topbar compacte
Contenu principal
Navigation basse ou sidebar compacte
Panneau contextuel coulissant
```

## 6.3 Mobile / iPhone / Android / SUNMI

Structure :

```text
Topbar mobile
Contenu principal
Actions principales
Navigation basse
```

Règles :

- une colonne ;
- gros boutons ;
- une tâche principale par écran ;
- pas de tableaux complexes ;
- actions terrain visibles.

---

# 7. Architecture adaptative multi-plateforme

LabConnect doit être **adaptive**, pas seulement responsive.

## 7.1 Plateformes cibles

L’interface doit s’adapter à :

```text
Windows
macOS
Android
iOS
iPadOS
SUNMI V3
écrans tactiles industriels
navigateurs modernes
```

## 7.2 Règles

Desktop :

- supervision ;
- configuration ;
- plan du laboratoire ;
- gestion multi-équipements.

Tablette :

- supervision tactile ;
- intervention ;
- validation opérateur.

SUNMI / mobile :

- action rapide ;
- capture ;
- impression ;
- pesée ;
- ajout d’équipement ;
- consultation simplifiée.

---

# 8. Concept central : équipements, groupes et zones

## 8.1 Équipement

Un équipement est un objet central de l’interface.

Chaque équipement doit être représenté par :

- illustration semi-3D ;
- nom ;
- type ;
- statut ;
- valeur principale ;
- ID client ;
- numéro de série ;
- connexion ;
- action rapide.

Exemple :

```text
A&D GX-603A
Balance analytique
BAL-01
SN: 83051248
12.3456 g
Stable
Bluetooth
```

## 8.2 Groupe

Un groupe correspond à une zone fonctionnelle.

Exemples :

```text
Salle de pesée
Étiquetage
Préparation
Contrôle qualité
Stockage
```

Une carte groupe affiche :

- nom ;
- nombre d’équipements ;
- état global ;
- icône ou illustration ;
- accès au détail.

## 8.3 Zone / laboratoire

Un site peut contenir plusieurs zones.

Un mode plan doit permettre de localiser les équipements.

---

# 9. Navigation orientée objet

## 9.1 Principe

Tout objet réel visible doit être cliquable.

Exemples :

- balance ;
- pompe ;
- Zebra ;
- SUNMI ;
- Binder ;
- capteur ;
- hub ;
- connexion ;
- groupe ;
- salle ;
- alarme ;
- tâche.

## 9.2 Clic sur équipement

Au clic sur l’image ou la carte d’un équipement :

```text
ouvrir la page dédiée de l’équipement
```

La page dédiée affiche :

- illustration grand format ;
- état ;
- valeur live ;
- actions ;
- connexions ;
- historique ;
- paramètres ;
- diagnostics.

## 9.3 Clic sur groupe

Au clic sur un groupe :

```text
ouvrir la vue du groupe
```

La vue groupe affiche :

- équipements du groupe ;
- statut global ;
- activité récente ;
- actions groupées.

## 9.4 Clic sur connexion

Au clic sur une connexion :

```text
ouvrir le détail de la liaison
```

Afficher :

- protocole ;
- signal ;
- statut ;
- paramètres ;
- historique ;
- erreurs.

---

# 10. Illustrations produit et Digital Twin

## 10.1 Style d’illustration

Les équipements doivent être représentés comme des jumeaux numériques premium.

Style :

- semi-3D ;
- vue 3/4 ;
- fond transparent ;
- ombre douce ;
- détails simplifiés ;
- perspective cohérente ;
- rendu réaliste mais propre.

## 10.2 Ne pas utiliser

Ne pas utiliser :

- photo brute non harmonisée ;
- icône générique quand l’équipement réel est connu ;
- rendu trop détaillé ou photoréaliste ;
- style cartoon.

## 10.3 Équipements à prévoir

- balance ;
- pompe ;
- imprimante Zebra ;
- terminal SUNMI ;
- enceinte Binder ;
- capteur ;
- automate ;
- module ESP32 / M5Stack ;
- panel PC.

## 10.4 SUNMI et équipements avec écran

Si l’équipement possède un écran, l’écran doit afficher une vraie prévisualisation LabConnect.

Exemple SUNMI en module Balance :

```text
A&D GX-603A
12.3456 g
Bouton Capturer le poids
Actions : Tare, Zéro, Imprimer
```

L’écran ne doit pas rester noir ou vide.

---

# 11. Interface Sonos-like : accueil

## 11.1 Desktop

Écran d’accueil recommandé :

```text
Sidebar
Header : Laboratoire Central
Résumé : 6 équipements · 3 groupes · Tout est opérationnel

Groupes
[Salle de pesée] [Étiquetage] [Préparation]

Équipements
[Balance] [SUNMI] [Zebra] [Pompe] [Binder] [+ Ajouter]

Activité récente
[Pesée enregistrée] [Étiquette imprimée] [Pompe démarrée]
```

## 11.2 Mobile

Écran d’accueil mobile :

```text
LabConnect

Groupes
[Salle de pesée]
[Étiquetage]
[Préparation]

Équipements
[A&D GX-603A]
[SUNMI V3]
[Zebra ZD621]
```

## 11.3 SUNMI

Écran d’accueil SUNMI :

```text
LabConnect

A&D GX-603A
12.3456 g
[Capturer le poids]

[Tare] [Zéro] [Imprimer]

Dernières pesées
```

---

# 12. Plan de laboratoire

## 12.1 Principe

LabConnect doit pouvoir afficher un plan du laboratoire.

Le plan montre :

- salles ;
- zones ;
- équipements ;
- statuts ;
- alarmes ;
- connexions si nécessaire.

## 12.2 Modes d’affichage

Prévoir :

```text
Vue accueil
Vue groupes
Vue équipements
Vue plan
Vue liste
```

## 12.3 Plan

Sur desktop :

- plan large ;
- zoom ;
- déplacement ;
- sélection d’équipement ;
- panneau latéral.

Sur mobile :

- plan simplifié ;
- liste par zone ;
- accès rapide aux équipements.

---

# 13. Plusieurs équipements identiques

## 13.1 Besoin

Un client peut avoir plusieurs équipements identiques.

Exemple :

```text
20 × A&D GX-603A
```

## 13.2 Modes

LabConnect doit proposer :

1. vue individuelle ;
2. vue groupée.

## 13.3 Vue individuelle

Afficher chaque équipement avec :

- nom personnalisé ;
- ID client ;
- numéro de série ;
- localisation ;
- statut ;
- valeur principale.

Exemple :

```text
A&D GX-603A
BAL-01
SN: 83051248
Salle de pesée 1
```

## 13.4 Vue groupée

Afficher :

```text
20 × A&D GX-603A
18 en ligne
1 en veille
1 erreur
```

Le groupe doit être cliquable pour afficher le détail.

---

# 14. Ajout d’équipement — expérience mobile type Sonos

## 14.1 Principe

L’ajout d’un équipement doit être simple, guidé et rassurant.

L’expérience doit se rapprocher d’une modale mobile type Sonos.

Le parcours doit fonctionner sur :

- iPhone ;
- Android ;
- SUNMI ;
- tablette ;
- desktop.

## 14.2 Présentation mobile

Sur mobile, utiliser une **bottom sheet** blanche avec arrière-plan assombri.

Caractéristiques :

```css
border-radius: 28px 28px 0 0;
background: white;
box-shadow: 0 -24px 80px rgba(15, 23, 42, 0.20);
```

La sheet doit être déplaçable visuellement avec une petite poignée en haut.

## 14.3 Étape 1 : choisir la méthode

Titre :

```text
Ajouter un équipement
```

Sous-titre :

```text
Comment souhaitez-vous ajouter un nouvel équipement ?
```

Options :

```text
Recherche Bluetooth
Scanner un code QR
Saisir manuellement
Importer une liste
```

Chaque option est une ligne cliquable avec :

- icône ;
- titre ;
- description courte ;
- chevron.

## 14.4 Étape 2 : recherche Bluetooth

Écran :

```text
Recherche Bluetooth
Recherche en cours...
Assurez-vous que l’équipement est allumé et en mode appairage.
```

Visuel :

- cercle central Bluetooth ;
- anneaux concentriques ;
- équipements détectés autour du cercle ;
- animation douce.

Bouton secondaire :

```text
Besoin d’aide ?
```

## 14.5 Étape 3 : équipement trouvé

Titre :

```text
1 équipement trouvé
```

Carte équipement :

```text
A&D GX-603A
Balance analytique
ID Bluetooth : GX603A_83051248
Numéro de série : 83051248
Firmware : 1.2.7
```

Action principale :

```text
Ajouter cet équipement
```

Action secondaire :

```text
Je ne le vois pas
```

## 14.6 Étape 4 : configuration

Titre :

```text
Configurer l’équipement
```

Afficher l’équipement trouvé :

```text
A&D GX-603A
SN: 83051248
Détecté en Bluetooth
```

Champs :

```text
Affecter à
Nom de l’équipement
ID client
Localisation
```

Groupes proposés :

```text
Salle de pesée
Étiquetage
Préparation
Autre salle...
```

Action principale :

```text
Ajouter à la salle de pesée
```

## 14.7 Étape 5 : confirmation

Titre :

```text
Équipement ajouté
```

Message :

```text
A&D GX-603A a été ajouté à Salle de pesée.
```

Actions :

```text
Voir l’équipement
Ajouter un autre équipement
Terminer
```

## 14.8 QR Code

Le scan QR doit permettre de récupérer :

- modèle ;
- numéro de série ;
- ID client ;
- type équipement ;
- protocole ;
- configuration de base.

## 14.9 Saisie manuelle

Prévoir un formulaire simple :

```text
Type d’équipement
Fabricant
Modèle
Numéro de série
ID client
Protocole
Groupe
Localisation
```

## 14.10 Importer une liste

Prévoir import :

- CSV ;
- XLSX ;
- JSON.

Le mode import est destiné aux installations avec beaucoup d’équipements.

## 14.11 Desktop

Sur desktop, l’ajout d’équipement peut être une modale centrée ou un panneau latéral.

Mais le contenu doit rester identique au parcours mobile.

---

# 15. Connexions et flux

## 15.1 Connexions visibles

Les connexions restent importantes mais ne doivent pas surcharger l’expérience Sonos-like.

Afficher les connexions :

- dans les pages détail ;
- dans le plan ;
- dans la vue topologie ;
- quand elles expliquent une relation métier.

## 15.2 Style

Ligne active :

```css
stroke: var(--brand);
stroke-width: 2px;
stroke-linecap: round;
```

Ligne inactive :

```css
stroke: #CBD5E1;
stroke-dasharray: 4 4;
```

Erreur :

```css
stroke: var(--danger);
```

## 15.3 Protocoles

Afficher quand utile :

```text
Bluetooth
Wi-Fi
USB
RS-232
Ethernet
TCP/IP
MQTT
Modbus TCP
Modbus RTU
```

---

# 16. Modules

## 16.1 LabConnect Hub

Objectif :

```text
Superviser tout le laboratoire ou un site.
```

Accueil :

- groupes ;
- équipements ;
- plan ;
- activité récente ;
- état global.

## 16.2 LabConnect Balance

Objectif :

```text
Afficher, capturer, synchroniser et exploiter les pesées.
```

Accueil :

- balance principale ;
- valeur live ;
- SUNMI associé si présent ;
- actions Tare / Zéro / Imprimer / Capturer ;
- dernières pesées.

## 16.3 LabConnect Dose

Objectif :

```text
Piloter une pompe et une balance pour réaliser un dosage.
```

Accueil :

- recette ;
- pompe ;
- balance ;
- cible ;
- progression ;
- actions Start / Pause / Stop.

## 16.4 LabConnect Labeler

Objectif :

```text
Créer et imprimer des étiquettes.
```

Accueil :

- imprimante ;
- file d’impression ;
- modèle actif ;
- bouton Imprimer ;
- historique.

## 16.5 LabConnect Vision

Objectif :

```text
Analyser images, caméra ou contrôle visuel.
```

## 16.6 LabConnect Monitor

Objectif :

```text
Surveiller des valeurs et alarmes.
```

---

# 17. Composants UI obligatoires

Claude Code doit créer ces composants réutilisables :

```text
AppShell
Topbar
Sidebar
MobileBottomNav
ModuleLogo
BrandMark
GroupCard
EquipmentCard
EquipmentDetailPage
EquipmentGroupCard
ProductHero
ProductScreenPreview
AddDeviceSheet
AddDeviceWizard
BluetoothSearchView
QRCodeScannerView
ManualDeviceForm
ImportDeviceList
ConnectionMap
RoomPlanView
StatusBadge
MetricCard
ActionButton
ActivityFeed
RightPanel
SearchInput
ThemeToggle
```

## 17.1 EquipmentCard

Doit être cliquable.

Props :

```ts
type EquipmentCardProps = {
  id: string;
  name: string;
  type: string;
  model: string;
  image: string;
  status: EquipmentStatus;
  serialNumber?: string;
  customerId?: string;
  location?: string;
  primaryValue?: string;
  primaryUnit?: string;
  connectionType?: string;
  batteryLevel?: number;
  onClick: () => void;
};
```

## 17.2 GroupCard

Props :

```ts
type GroupCardProps = {
  id: string;
  name: string;
  icon: string;
  equipmentCount: number;
  status: "operational" | "warning" | "error" | "offline";
  onClick: () => void;
};
```

## 17.3 AddDeviceWizard

Étapes :

```ts
type AddDeviceStep =
  | "method"
  | "bluetooth-search"
  | "qr-scan"
  | "manual-form"
  | "import-list"
  | "device-found"
  | "configure"
  | "success";
```

## 17.4 Device model

```ts
type EquipmentStatus =
  | "online"
  | "connected"
  | "ready"
  | "running"
  | "standby"
  | "offline"
  | "error"
  | "maintenance"
  | "simulation";

type Equipment = {
  id: string;
  name: string;
  manufacturer: string;
  model: string;
  type: "balance" | "printer" | "pump" | "terminal" | "chamber" | "sensor" | "controller" | "custom";
  serialNumber?: string;
  customerId?: string;
  location?: string;
  groupId?: string;
  image: string;
  status: EquipmentStatus;
  primaryValue?: string;
  primaryUnit?: string;
  connection?: {
    protocol: "bluetooth" | "wifi" | "usb" | "rs232" | "ethernet" | "tcpip" | "mqtt" | "modbus";
    status: "active" | "idle" | "error" | "offline";
    label?: string;
  };
  batteryLevel?: number;
};
```

---

# 18. États système

## 18.1 Statuts

```ts
const statusLabels = {
  online: "En ligne",
  connected: "Connecté",
  ready: "Prêt",
  running: "En fonctionnement",
  standby: "En veille",
  offline: "Hors ligne",
  error: "Erreur",
  maintenance: "Maintenance",
  simulation: "Simulation"
};
```

## 18.2 Statut global

Pour l’accueil :

```text
Tout est opérationnel
Attention requise
Erreur critique
Hors ligne
```

---

# 19. Accessibilité

## 19.1 Contraste

Respecter WCAG AA minimum.

Ne jamais transmettre une information uniquement par couleur.

Toujours associer :

- couleur ;
- texte ;
- icône.

## 19.2 Tactile

Minimum :

```css
min-width: 44px;
min-height: 44px;
```

Sur SUNMI :

```css
min-height: 52px;
```

## 19.3 Focus

```css
outline: 3px solid rgba(37, 99, 235, 0.32);
outline-offset: 2px;
```

---

# 20. Animations

## 20.1 Principes

Animations utiles uniquement.

Utiliser pour :

- recherche Bluetooth ;
- confirmation ajout équipement ;
- synchronisation ;
- flux actif ;
- erreur.

## 20.2 Durées

```css
--motion-fast: 120ms;
--motion-normal: 180ms;
--motion-slow: 280ms;
```

## 20.3 Bottom sheet

La bottom sheet doit avoir une transition douce :

```css
transition: transform 280ms cubic-bezier(0.2, 0.8, 0.2, 1);
```

---

# 21. Langage UI

## 21.1 Ton

Français professionnel, simple, direct.

Exemples :

```text
Ajouter un équipement
Recherche Bluetooth
Scanner un code QR
Saisir manuellement
Importer une liste
Équipement trouvé
Configurer l’équipement
Ajouter à la salle de pesée
Équipement ajouté
```

## 21.2 Messages d’erreur

Structure :

1. problème ;
2. cause si connue ;
3. action possible.

Exemple :

```text
Connexion impossible avec la balance.
Aucune réponse n’a été reçue en Bluetooth.
Vérifiez que l’équipement est allumé et en mode appairage.
```

---

# 22. Règles anti “AI slop”

## 22.1 À éviter

Ne pas générer :

- icônes incohérentes ;
- logos trop génériques ;
- cartes trop similaires à n’importe quel SaaS ;
- dégradés gratuits ;
- effets lumineux inutiles ;
- textes décoratifs ;
- équipements sans identité ;
- interfaces trop chargées ;
- pseudo-3D incohérente.

## 22.2 Ce qui rend LabConnect distinctif

Toujours prioriser :

- marque visible ;
- équipements réels ;
- groupes type Sonos ;
- plan de laboratoire ;
- ajout d’équipement guidé ;
- SUNMI avec interface intégrée ;
- navigation par objet ;
- jumeaux numériques cohérents.

---

# 23. Priorité de développement

Ordre recommandé pour Claude Code :

1. Design tokens.
2. AppShell responsive.
3. Sidebar repliable.
4. Mobile bottom nav.
5. BrandMark + ModuleLogo.
6. GroupCard.
7. EquipmentCard.
8. EquipmentDetailPage.
9. AddDeviceSheet.
10. AddDeviceWizard.
11. BluetoothSearchView.
12. ManualDeviceForm.
13. RoomPlanView.
14. ProductScreenPreview.
15. Pages Hub / Balance.
16. Mode sombre.
17. Adaptation SUNMI.
18. Modules Dose / Labeler.

---

# 24. Résumé des décisions validées

```text
Direction UX : proche Sonos
Positionnement : plateforme connectée
Marque : LabConnect visible
Thème principal : clair
Mode sombre : disponible
Police : sans serif moderne, Geist recommandée
Navigation : objets, groupes, zones
Tout équipement visible est cliquable
Accueil : groupes + équipements + activité
Plan laboratoire : oui
Sidebar PC : visible et repliable
Mobile/SUNMI : expérience adaptée, pas desktop réduit
Ajout device : bottom sheet type Sonos
Méthodes ajout : Bluetooth, QR, manuel, import liste
Équipements identiques : vue individuelle + groupée
Différenciation : ID client, SN, localisation
SUNMI : écran avec interface LabConnect intégrée
Anti-slop : éviter le SaaS générique
```

LabConnect doit donner l’impression d’un système moderne où l’utilisateur reconnaît immédiatement ses équipements, ses zones, ses groupes et les actions disponibles.

---

# 25. Stack technique recommandée pour une évolution unifiée

## 25.1 Objectif

LabConnect doit être développé avec une base de code unifiée.

Le même socle doit pouvoir servir à :

```text
Windows
macOS
Linux
Android
iOS / iPadOS
SUNMI V3
Web admin
```

L'objectif est d'éviter de maintenir plusieurs applications séparées.

## 25.2 Stack recommandée

Stack principale recommandée :

```text
React
TypeScript
Vite
Tailwind CSS
shadcn/ui
Tauri v2
TanStack Query
Zustand ou TanStack Store
```

## 25.3 Rôle de chaque élément

### React

React sert à créer l'interface utilisateur commune.

Toutes les interfaces doivent être conçues en composants réutilisables.

Exemples :

```text
EquipmentCard
GroupCard
AddDeviceSheet
RoomPlanView
ProductHero
StatusBadge
ActionButton
```

### TypeScript

TypeScript est obligatoire.

Tous les équipements, modules, statuts, protocoles et connexions doivent avoir des types partagés.

Exemple :

```ts
type EquipmentType =
  | "balance"
  | "printer"
  | "pump"
  | "terminal"
  | "chamber"
  | "sensor"
  | "controller"
  | "custom";
```

### Vite

Vite est recommandé pour l'application frontend.

Il sert de base rapide pour le développement React.

### Tailwind CSS

Tailwind CSS sert à appliquer le design system LabConnect.

Tous les tokens définis dans ce document doivent être intégrés dans la configuration Tailwind.

### shadcn/ui

shadcn/ui peut être utilisé pour accélérer la création des composants de base :

```text
Button
Dialog
Sheet
Input
Select
Tabs
Dropdown
Toast
Card
```

Les composants doivent être adaptés au style LabConnect.

Ne pas utiliser shadcn/ui tel quel sans personnalisation visuelle.

### Tauri v2

Tauri v2 est recommandé pour produire des applications desktop et mobiles depuis une base web commune.

Tauri v2 permet de cibler Windows, macOS, Linux, Android et iOS à partir d'une base web, avec logique applicative en Rust et intégrations natives possibles en Swift/Kotlin.

### TanStack Query

TanStack Query sert à gérer :

- appels API,
- cache,
- synchronisation,
- rafraîchissement des données,
- états loading/error/success,
- polling des équipements.

### Zustand ou TanStack Store

Un store léger sert à gérer :

- module actif,
- thème,
- utilisateur,
- sélection d'équipement,
- état sidebar,
- filtres,
- préférences locales.

---

# 26. Architecture monorepo recommandée

## 26.1 Structure

Utiliser un monorepo.

Structure recommandée :

```text
labconnect/
├─ apps/
│  ├─ labconnect-tauri/
│  ├─ web-admin/
│  └─ docs/
│
├─ packages/
│  ├─ ui/
│  ├─ design-system/
│  ├─ shared-types/
│  ├─ devices/
│  ├─ protocols/
│  ├─ drivers/
│  ├─ database/
│  ├─ api-client/
│  └─ utils/
│
├─ firmware/
│  ├─ m5stack/
│  ├─ atom-s3/
│  └─ core-s3/
│
└─ docs/
   ├─ design.md
   ├─ api.md
   ├─ devices.md
   └─ protocols.md
```

## 26.2 apps/labconnect-tauri

Application principale.

Cible :

```text
Windows
macOS
Linux
Android
iOS
SUNMI
```

Elle contient uniquement :

- shell applicatif,
- routes,
- intégration Tauri,
- configuration plateforme.

Elle ne doit pas contenir la logique métier profonde.

## 26.3 packages/ui

Contient tous les composants UI réutilisables.

Exemples :

```text
EquipmentCard
GroupCard
AddDeviceSheet
BluetoothSearchView
RoomPlanView
ProductScreenPreview
MetricCard
StatusBadge
```

## 26.4 packages/design-system

Contient :

- tokens,
- couleurs,
- typographie,
- radius,
- spacing,
- animations,
- thèmes clair/sombre,
- mapping des couleurs de modules.

## 26.5 packages/shared-types

Contient tous les types TypeScript communs.

Exemples :

```text
Equipment
EquipmentStatus
EquipmentConnection
Protocol
ModuleName
UserRole
Site
Room
Group
```

## 26.6 packages/devices

Contient les définitions des appareils.

Chaque appareil doit être décrit de manière déclarative.

Exemple :

```ts
export const AD_GX_603A = {
  manufacturer: "A&D",
  model: "GX-603A",
  type: "balance",
  defaultProtocol: "rs232",
  supportedProtocols: ["rs232", "usb", "bluetooth"],
  defaultSerial: {
    baudRate: 2400,
    dataBits: 7,
    parity: "even",
    stopBits: 1,
    terminator: "CRLF"
  },
  capabilities: ["weight", "tare", "zero", "print"]
};
```

## 26.7 packages/protocols

Contient les protocoles de communication.

Exemples :

```text
RS-232
USB
Bluetooth
TCP/IP
MQTT
Modbus TCP
Modbus RTU
ZPL
```

## 26.8 packages/drivers

Contient les drivers d'appareils.

Un driver ne doit pas être mélangé avec l'interface.

Exemples :

```text
andBalanceDriver
zebraPrinterDriver
masterflexPumpDriver
binderChamberDriver
sunmiTerminalDriver
```

## 26.9 packages/database

Contient :

- schéma local,
- migrations,
- modèles,
- stockage local,
- historique,
- logs,
- traçabilité.

## 26.10 packages/api-client

Contient les clients API partagés.

Exemples :

```text
localApiClient
cloudApiClient
deviceApiClient
```

---

# 27. Règles d'architecture logicielle

## 27.1 Séparation stricte

Séparer strictement :

```text
UI
État applicatif
Logique métier
Drivers
Protocoles
Persistance
Plateforme native
```

## 27.2 Interdictions

Ne jamais mettre directement dans un composant React :

- communication série,
- parsing RS-232,
- commande pompe,
- commande Zebra,
- logique Bluetooth,
- accès fichier natif,
- logique de synchronisation,
- logique de sécurité.

Le composant React doit afficher un état et déclencher une action.

La logique doit être dans les packages dédiés.

## 27.3 Modèle d'action

Exemple correct :

```ts
await deviceActions.tare(balanceId);
```

Exemple incorrect :

```ts
serialPort.write("T\r\n");
```

dans un composant React.

## 27.4 Modèle déclaratif des équipements

Chaque appareil doit être ajouté au système via une définition.

Ne pas coder un appareil uniquement dans une page UI.

Correct :

```text
Définition appareil
Driver
Capacités
UI générique
```

Incorrect :

```text
Page spéciale codée uniquement pour GX-603A
```

---

# 28. Gestion multi-plateforme

## 28.1 Principe

La logique métier doit être commune.

Seules les intégrations natives changent selon la plateforme.

## 28.2 Couche plateforme

Créer une couche `platform`.

Exemple :

```ts
type PlatformService = {
  scanBluetooth(): Promise<DeviceScanResult[]>;
  openSerialPort(config: SerialConfig): Promise<SerialConnection>;
  readFile(): Promise<FileData>;
  writeFile(data: FileData): Promise<void>;
  scanQrCode(): Promise<string>;
};
```

## 28.3 Implémentations

Prévoir plusieurs implémentations :

```text
platform-web
platform-tauri-desktop
platform-tauri-android
platform-tauri-ios
platform-sunmi
```

L'interface React ne doit pas savoir directement si elle tourne sur Windows, macOS, Android, iOS ou SUNMI.

Elle doit appeler la même API abstraite.

## 28.4 Exemple

Correct :

```ts
const devices = await platform.scanBluetooth();
```

Incorrect :

```ts
if (isAndroid) {
  // code Android direct dans le composant
}
```

---

# 29. Base de données et synchronisation

## 29.1 Stockage local

LabConnect doit pouvoir fonctionner localement.

Prévoir un stockage local pour :

- équipements,
- groupes,
- zones,
- utilisateurs,
- logs,
- pesées,
- impressions,
- recettes,
- paramètres,
- historiques.

## 29.2 Mode offline-first

L'application doit être pensée pour fonctionner même sans cloud.

Le cloud doit être optionnel.

## 29.3 Synchronisation

Prévoir une couche de synchronisation séparée.

Elle doit gérer :

- données locales,
- exports,
- sauvegardes,
- synchronisation entre SUNMI et PC,
- synchronisation cloud éventuelle.

---

# 30. À éviter absolument côté stack

## 30.1 Ne pas créer plusieurs applications indépendantes

Éviter :

```text
Une app Windows séparée
Une app macOS séparée
Une app Android séparée
Une app iOS séparée
Une app SUNMI séparée
```

Cela rendrait la maintenance trop lourde.

## 30.2 Ne pas dupliquer l'UI

Éviter :

```text
Composants desktop différents des composants mobile
Pages copiées/collées entre plateformes
Design tokens dupliqués
Logique métier dupliquée
```

## 30.3 Ne pas mettre les drivers dans l'interface

Éviter :

```text
src/components/BalanceCard.tsx contient du code RS-232
src/pages/Dose.tsx contient la commande pompe
src/components/ZebraButton.tsx contient du ZPL
```

## 30.4 Ne pas choisir Electron comme base principale

Electron peut être pertinent pour desktop uniquement.

Mais pour LabConnect, il n'est pas optimal comme base principale car il cible Windows, macOS et Linux, pas Android/iOS.

## 30.5 Ne pas choisir React Native comme base principale

React Native est pertinent pour mobile.

Mais pour LabConnect, il complique la partie desktop et la réutilisation directe du design web.

## 30.6 Ne pas choisir Flutter sauf décision stratégique forte

Flutter peut créer une interface multi-plateforme.

Mais il impose un autre écosystème UI que React.

Pour LabConnect, React + TypeScript + Tauri v2 est plus cohérent avec :

- composants web,
- dashboards,
- design system,
- évolution rapide,
- base déjà proche de l'écosystème actuel.

## 30.7 Ne pas mélanger trop de frameworks

Éviter :

```text
React + Vue + Flutter + Electron + React Native
```

Choisir un socle et s'y tenir.

## 30.8 Ne pas dépendre uniquement du navigateur

Une simple PWA ne suffit pas si LabConnect doit accéder à :

- Bluetooth,
- série,
- fichiers,
- impression,
- intégrations natives,
- stockage local robuste.

## 30.9 Ne pas coder les appareils en dur

Éviter :

```text
if model === "GX-603A"
```

répété partout dans l'interface.

Préférer :

```text
capabilities
driver
protocol
device definition
```

---

# 31. Choix recommandé final

Stack recommandée pour LabConnect :

```text
React + TypeScript + Vite + Tailwind CSS + shadcn/ui + Tauri v2
```

Architecture recommandée :

```text
Monorepo
Packages partagés
Drivers séparés
Protocoles séparés
Types partagés
Design system partagé
Couche plateforme abstraite
Mode offline-first
```

Objectif :

```text
Une expérience unifiée.
Un code maintenable.
Des interfaces adaptées.
Une logique métier partagée.
Des appareils extensibles.
```
