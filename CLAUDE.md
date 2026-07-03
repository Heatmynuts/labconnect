# LabConnect — contexte repo (juillet 2026)

## Structure des branches (aucun merge dans main)
- Lignée Hub : `codex/immo` (= `codex/atoms3-rs232-autodetect`, même commit) — firmwares AtomS3, Hub rond ESP32
- Lignée Print : `codex/print-v3-adaptive` (= tête de `origin/codex/print-android` ; la branche locale `codex/print-android` est en retard) — app Android/Tauri Sunmi, monorepo `apps/` + `packages/` + `docs/`
- `main` : commit initial uniquement
- `backup/etat-2026-07-03` : snapshot du working tree du 3 juillet 2026

## GPIO AtomS3 (RS232 balance)
- `BALANCE_RX_PIN = 5`, `BALANCE_TX_PIN = 6` (LabConnectPrintAtomS3.ino:23-24)
- Nœud BLE : `rxPin = 5`, `txPin = 6`, swap RX/TX configurable (LabConnectAtomS3BleNode.ino:52-53)

## Protocoles balance — source : firmware/atom-s3/LabConnectPrintAtomS3/LabConnectPrintAtomS3.ino:53-64
| id | Marque | Série | Cmd poids | Init |
|---|---|---|---|---|
| and | A&D | 2400 / SERIAL_7E1 | "Q" | — |
| mettler | Mettler Toledo | 9600 / SERIAL_8N1 | "S" | — |
| sartorius | Sartorius | 9600 / SERIAL_8O1 | "P" | "CONT" |
| ohaus | Ohaus | 9600 / SERIAL_8N1 | "IP" | — |
| kern | Kern | 9600 / SERIAL_8N1 | "S" | — |
| shimadzu | Shimadzu | 9600 / SERIAL_7E1 | "Q" | — |
| precisa | Precisa | 9600 / SERIAL_8N1 | "SI" | — |
| precia-molen | Precia Molen | 9600 / SERIAL_8N1 | "S" | — |
| bizerba | Bizerba | 9600 / SERIAL_8N1 | "W" | — |
| dini | Dini Argeo | 9600 / SERIAL_8N1 | "S" | — |

⚠️ Shimadzu : la table d'autoscan (ligne 94) utilise 1200 / SERIAL_8N1 / "D05" / fin CR — en contradiction
avec le profil ci-dessus. Non résolu : vérifier sur matériel réel avant de s'appuyer sur l'une des deux valeurs.
Autoscan (lignes 87-99) : A&D aussi sondé à 4800 et 9600/7E1 ; Mettler SICS à 4800 ; Sartorius sondé avec "\x1bP" (ESC+P).

## Réseau (firmware Print)
- AP : SSID base "LabConnect-Print" + suffixe, mot de passe "labconnect" (lignes 16-17)
- WebSocket : `WebSocketsServer webSocket(80, "/ws")` (ligne 122) → ws://192.168.4.1/ws
- Admin via WS, préfixe `admin:` — `admin:brand:<id> | admin:brand | admin:brands | admin:cmd:<raw> | admin:reboot` (ligne 10) ; mêmes commandes sans préfixe via USB Serial

## App Android Sunmi (branche codex/print-v3-adaptive)
- Formats : "v3" (portrait) / "v3mix" (paysage) ; auto-détection par orientation de fenêtre + override admin (`useTerminalLayout`, App.tsx:2357-2368)
- Bridges JS injectés : `LabConnectSunmiPrinter`, `LabConnectScanner` (MainActivity.kt:68-69). NB : pas de bridge `LabConnectDevice` (seulement `LabConnectDeviceAdminReceiver`, admin device Android).
- Package : `com.labconnect.print` ; app Hub Java séparée : `fr.bdp.labconnect.sunmihub` (non commitée hors backup)

## Conventions
- Protocoles côté web : passer par `packages/protocols`, ne pas coder les protocoles dans les composants React (docs/protocols.md)
- `PRODUCT.md` du Hub Sunmi : registre produit pour le skill de design (LabConnect Hub - Sunmi V3/PRODUCT.md)
