# LabConnect Sunmi

Application Android pour Sunmi V3.

Elle ouvre l'interface du Knob LabConnect Hub en plein ecran via `http://192.168.4.1`.

## Build

```bash
cd apps/labconnect-sunmi
gradle assembleDebug
```

APK genere :

```text
app/build/outputs/apk/debug/app-debug.apk
```

## Installation USB

Connecter le Sunmi en USB avec le debug USB active, puis :

```bash
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

Le Sunmi doit etre connecte au reseau Wi-Fi du Knob avant d'ouvrir l'app.
