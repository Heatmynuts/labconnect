# LabConnect Hub - Sunmi V3

Projet propre pour remplacer le Knob par un Sunmi V3.

## Architecture

- `android-sunmi-hub/` : application Android native installee sur le Sunmi V3.
- `firmware/atom-s3-ble-node/` : firmware AtomS3 qui communique avec le Sunmi en BLE.

Le Sunmi devient le Hub. Les AtomS3 annoncent leur presence en BLE, le Sunmi les scanne, se connecte et recoit les valeurs de pesee.

## BLE

Service LabConnect :

```text
6f7d0001-6c63-4c42-4855-422d53554e4d
```

Caracteristiques :

```text
6f7d0002-6c63-4c42-4855-422d53554e4d  poids / notify
6f7d0003-6c63-4c42-4855-422d53554e4d  commande / write
6f7d0004-6c63-4c42-4855-422d53554e4d  info node / read + notify
```

## Build Android

```bash
cd "LabConnect Hub - Sunmi V3/android-sunmi-hub"
gradle assembleDebug
```

APK :

```text
app/build/outputs/apk/debug/app-debug.apk
```

Installation USB :

```bash
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

## Firmware AtomS3

Ouvrir :

```text
firmware/atom-s3-ble-node/LabConnectAtomS3BleNode/LabConnectAtomS3BleNode.ino
```

Board Arduino :

```text
M5AtomS3
```

Pins RS232 via MAX3232 :

```text
RX = GPIO 5
TX = GPIO 6
```
