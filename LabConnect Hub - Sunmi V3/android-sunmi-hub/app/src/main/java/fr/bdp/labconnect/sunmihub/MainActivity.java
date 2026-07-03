package fr.bdp.labconnect.sunmihub;

import android.Manifest;
import android.annotation.SuppressLint;
import android.app.Activity;
import android.app.AlertDialog;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCallback;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattDescriptor;
import android.bluetooth.BluetoothGattService;
import android.bluetooth.BluetoothManager;
import android.bluetooth.BluetoothProfile;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanFilter;
import android.bluetooth.le.ScanResult;
import android.bluetooth.le.ScanSettings;
import android.content.pm.PackageManager;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelUuid;
import android.text.TextUtils;
import android.view.Gravity;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import org.json.JSONObject;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.UUID;

public class MainActivity extends Activity {
    private static final int PICK_NODE_IMAGE = 42;
    private static final UUID SERVICE_UUID = UUID.fromString("6f7d0001-6c63-4c42-4855-422d53554e4d");
    private static final UUID VALUE_UUID = UUID.fromString("6f7d0002-6c63-4c42-4855-422d53554e4d");
    private static final UUID COMMAND_UUID = UUID.fromString("6f7d0003-6c63-4c42-4855-422d53554e4d");
    private static final UUID INFO_UUID = UUID.fromString("6f7d0004-6c63-4c42-4855-422d53554e4d");
    private static final UUID CCCD_UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb");

    private BluetoothAdapter bluetoothAdapter;
    private BluetoothLeScanner scanner;
    private boolean scanning = false;
    private static final long RENDER_THROTTLE_MS = 450;

    private TextView subtitle;
    private LinearLayout list;
    private EditText commandInput;
    private Handler uiHandler;
    private SharedPreferences preferences;
    private String pendingImageNodeAddress;
    private Node activeSettingsNode;
    private long lastScanRenderAt = 0;
    private long lastRenderAt = 0;
    private boolean renderScheduled = false;
    private final Map<String, Node> nodes = new HashMap<>();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        BluetoothManager manager = (BluetoothManager) getSystemService(BLUETOOTH_SERVICE);
        bluetoothAdapter = manager != null ? manager.getAdapter() : null;
        scanner = bluetoothAdapter != null ? bluetoothAdapter.getBluetoothLeScanner() : null;
        uiHandler = new Handler(Looper.getMainLooper());
        preferences = getSharedPreferences("labconnect-sunmi-hub", MODE_PRIVATE);
        buildUi();
        requestBlePermissions();
    }

    private void buildUi() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(rgb(247, 248, 250));

        LinearLayout header = row();
        header.setPadding(dp(16), dp(8), dp(10), dp(8));
        header.setGravity(Gravity.CENTER_VERTICAL);

        LinearLayout titleBox = new LinearLayout(this);
        titleBox.setOrientation(LinearLayout.VERTICAL);
        TextView title = text("LabConnect Hub", 22, rgb(17, 19, 24), true);
        subtitle = text("Sunmi V3 BLE", 13, rgb(105, 112, 125), false);
        titleBox.addView(title);
        titleBox.addView(subtitle);
        header.addView(titleBox, new LinearLayout.LayoutParams(0, dp(56), 1));

        Button scan = primaryButton("Scanner");
        scan.setOnClickListener(v -> toggleScan());
        header.addView(scan, new LinearLayout.LayoutParams(dp(104), dp(44)));

        ScrollView scroll = new ScrollView(this);
        list = new LinearLayout(this);
        list.setOrientation(LinearLayout.VERTICAL);
        list.setPadding(dp(12), dp(8), dp(12), dp(12));
        scroll.addView(list);

        LinearLayout commandBar = row();
        commandBar.setPadding(dp(12), dp(8), dp(12), dp(12));
        commandInput = new EditText(this);
        commandInput.setSingleLine(true);
        commandInput.setHint("Commande ex: SI, ?ID, ?SN, I10");
        commandInput.setText("SI");
        commandInput.setTextSize(14);
        commandInput.setPadding(dp(12), 0, dp(12), 0);
        commandBar.addView(commandInput, new LinearLayout.LayoutParams(0, dp(48), 1));

        Button send = primaryButton("Envoyer");
        send.setOnClickListener(v -> sendCommandToConnected());
        LinearLayout.LayoutParams sendParams = new LinearLayout.LayoutParams(dp(112), dp(48));
        sendParams.leftMargin = dp(8);
        commandBar.addView(send, sendParams);

        root.addView(header, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, dp(72)));
        root.addView(scroll, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1));
        root.addView(commandBar, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, dp(68)));
        setContentView(root);
        render();
    }

    private void requestBlePermissions() {
        if (Build.VERSION.SDK_INT >= 31) {
            requestPermissions(new String[]{
                    Manifest.permission.BLUETOOTH_SCAN,
                    Manifest.permission.BLUETOOTH_CONNECT
            }, 10);
        } else if (Build.VERSION.SDK_INT >= 23) {
            requestPermissions(new String[]{Manifest.permission.ACCESS_FINE_LOCATION}, 10);
        }
    }

    @SuppressLint("MissingPermission")
    private void toggleScan() {
        if (scanner == null) {
            subtitle.setText("Bluetooth indisponible");
            return;
        }
        if (scanning) {
            scanner.stopScan(scanCallback);
            scanning = false;
            subtitle.setText(nodes.size() + " node(s)");
            return;
        }
        List<ScanFilter> filters = new ArrayList<>();
        filters.add(new ScanFilter.Builder().setServiceUuid(new ParcelUuid(SERVICE_UUID)).build());
        ScanSettings settings = new ScanSettings.Builder()
                .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
                .build();
        scanner.startScan(filters, settings, scanCallback);
        scanning = true;
        subtitle.setText("Scan BLE...");
    }

    private final ScanCallback scanCallback = new ScanCallback() {
        @SuppressLint("MissingPermission")
        @Override
        public void onScanResult(int callbackType, ScanResult result) {
            BluetoothDevice device = result.getDevice();
            String address = device.getAddress();
            Node node = nodes.get(address);
            boolean isNewNode = false;
            if (node == null) {
                node = new Node(address, device);
                nodes.put(address, node);
                isNewNode = true;
            }
            node.name = safeName(device);
            node.rssi = result.getRssi();
            node.seenAt = System.currentTimeMillis();
            long now = System.currentTimeMillis();
            if (isNewNode || now - lastScanRenderAt > 900) {
                lastScanRenderAt = now;
                runOnUiThread(() -> {
                    subtitle.setText(nodes.size() + " node(s)");
                    renderNow();
                });
            }
        }
    };

    @SuppressLint("MissingPermission")
    private void connect(Node node) {
        stopScanForInteraction();
        if (node.gatt != null) {
            node.status = "Deconnexion...";
            renderNow();
            node.gatt.disconnect();
            node.gatt.close();
            node.gatt = null;
            node.connected = false;
            node.status = "Hors ligne";
            renderNow();
            return;
        }
        node.status = "Connexion...";
        renderNow();
        NodeGattCallback callback = new NodeGattCallback(node);
        if (Build.VERSION.SDK_INT >= 23) {
            node.gatt = node.device.connectGatt(this, false, callback, BluetoothDevice.TRANSPORT_LE);
        } else {
            node.gatt = node.device.connectGatt(this, false, callback);
        }
    }

    @SuppressLint("MissingPermission")
    private void sendCommandToConnected() {
        String cmd = commandInput.getText().toString().trim();
        if (cmd.isEmpty()) return;
        for (Node node : nodes.values()) {
            if (node.connected && node.commandChar != null && node.gatt != null) {
                byte[] payload = (cmd + "\r\n").getBytes(StandardCharsets.UTF_8);
                node.commandChar.setValue(payload);
                node.gatt.writeCharacteristic(node.commandChar);
            }
        }
    }

    private class NodeGattCallback extends BluetoothGattCallback {
        private final Node node;

        NodeGattCallback(Node node) {
            this.node = node;
        }

        @SuppressLint("MissingPermission")
        @Override
        public void onConnectionStateChange(BluetoothGatt gatt, int status, int newState) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                node.connected = true;
                node.status = status == BluetoothGatt.GATT_SUCCESS ? "Services..." : "Connecte avec erreur " + status;
                if (Build.VERSION.SDK_INT >= 21) {
                    gatt.requestMtu(185);
                } else {
                    gatt.discoverServices();
                }
            } else {
                node.connected = false;
                node.status = status == BluetoothGatt.GATT_SUCCESS ? "Hors ligne" : "Erreur connexion " + status;
                node.valueChar = null;
                node.commandChar = null;
                node.infoChar = null;
            }
            requestRender();
        }

        @SuppressLint("MissingPermission")
        @Override
        public void onMtuChanged(BluetoothGatt gatt, int mtu, int status) {
            node.status = "Services...";
            gatt.discoverServices();
            requestRender();
        }

        @SuppressLint("MissingPermission")
        @Override
        public void onServicesDiscovered(BluetoothGatt gatt, int status) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                node.status = "Erreur services " + status;
                requestRender();
                return;
            }
            BluetoothGattService service = gatt.getService(SERVICE_UUID);
            if (service == null) {
                node.status = "Service absent";
                requestRender();
                return;
            }
            node.valueChar = service.getCharacteristic(VALUE_UUID);
            node.commandChar = service.getCharacteristic(COMMAND_UUID);
            node.infoChar = service.getCharacteristic(INFO_UUID);
            enableNotify(gatt, node.valueChar);
            enableNotify(gatt, node.infoChar);
            if (node.infoChar != null) gatt.readCharacteristic(node.infoChar);
            node.status = "Pret";
            requestRender();
        }

        @Override
        public void onCharacteristicChanged(BluetoothGatt gatt, BluetoothGattCharacteristic characteristic) {
            handleCharacteristic(characteristic);
        }

        @Override
        public void onCharacteristicRead(BluetoothGatt gatt, BluetoothGattCharacteristic characteristic, int status) {
            handleCharacteristic(characteristic);
        }

        private void handleCharacteristic(BluetoothGattCharacteristic characteristic) {
            String text = new String(characteristic.getValue(), StandardCharsets.UTF_8).trim();
            if (VALUE_UUID.equals(characteristic.getUuid())) {
                node.lastValue = text;
            } else if (INFO_UUID.equals(characteristic.getUuid())) {
                node.info = text;
            }
            requestRender();
        }
    }

    @SuppressLint("MissingPermission")
    private void enableNotify(BluetoothGatt gatt, BluetoothGattCharacteristic characteristic) {
        if (characteristic == null) return;
        gatt.setCharacteristicNotification(characteristic, true);
        BluetoothGattDescriptor descriptor = characteristic.getDescriptor(CCCD_UUID);
        if (descriptor != null) {
            descriptor.setValue(BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);
            gatt.writeDescriptor(descriptor);
        }
    }

    private void requestRender() {
        if (uiHandler == null) return;
        uiHandler.post(() -> {
            long now = System.currentTimeMillis();
            long elapsed = now - lastRenderAt;
            if (elapsed >= RENDER_THROTTLE_MS) {
                renderNow();
                return;
            }
            if (!renderScheduled) {
                renderScheduled = true;
                uiHandler.postDelayed(this::renderNow, RENDER_THROTTLE_MS - elapsed);
            }
        });
    }

    private void renderNow() {
        renderScheduled = false;
        lastRenderAt = System.currentTimeMillis();
        render();
    }

    private void render() {
        list.removeAllViews();
        if (nodes.isEmpty()) {
            TextView empty = text("Aucun Atom detecte\nTouchez Scanner", 20, rgb(105, 112, 125), true);
            empty.setGravity(Gravity.CENTER);
            list.addView(empty, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, dp(260)));
            return;
        }

        ArrayList<Node> ordered = new ArrayList<>(nodes.values());
        ordered.sort((left, right) -> displayNameForNode(left).compareToIgnoreCase(displayNameForNode(right)));

        for (int i = 0; i < ordered.size(); i += 2) {
            LinearLayout row = row();
            row.setGravity(Gravity.TOP);
            LinearLayout.LayoutParams rowParams = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT);
            rowParams.bottomMargin = dp(8);

            LinearLayout.LayoutParams leftParams = new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1);
            leftParams.rightMargin = dp(4);
            row.addView(card(ordered.get(i)), leftParams);

            LinearLayout.LayoutParams rightParams = new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1);
            rightParams.leftMargin = dp(4);
            if (i + 1 < ordered.size()) {
                row.addView(card(ordered.get(i + 1)), rightParams);
            } else {
                View spacer = new View(this);
                row.addView(spacer, rightParams);
            }
            list.addView(row, rowParams);
        }
    }

    private View card(Node node) {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setPadding(dp(10), dp(10), dp(10), dp(10));
        card.setBackground(rounded(Color.WHITE, 16, 0, Color.TRANSPARENT));

        String manualImage = preferences.getString("node_image_" + node.address, "");
        int modelImageRes = manualImage == null || manualImage.isEmpty() ? modelImageForNode(node) : 0;
        if ((manualImage != null && !manualImage.isEmpty()) || modelImageRes != 0) {
            ImageView model = new ImageView(this);
            if (manualImage != null && !manualImage.isEmpty()) {
                model.setImageURI(Uri.parse(manualImage));
            } else {
                model.setImageResource(modelImageRes);
            }
            model.setScaleType(ImageView.ScaleType.FIT_CENTER);
            model.setAdjustViewBounds(true);
            model.setBackground(rounded(rgb(250, 251, 253), 12, 1, rgb(232, 236, 242)));
            model.setPadding(dp(4), dp(4), dp(4), dp(4));
            LinearLayout.LayoutParams modelParams = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    dp(82));
            modelParams.bottomMargin = dp(8);
            card.addView(model, modelParams);
        }

        LinearLayout top = row();
        top.setGravity(Gravity.CENTER_VERTICAL);
        top.setMinimumHeight(dp(30));
        int logoRes = logoForNode(node);
        if (logoRes != 0) {
            ImageView logo = new ImageView(this);
            logo.setImageResource(logoRes);
            logo.setScaleType(ImageView.ScaleType.FIT_CENTER);
            LinearLayout.LayoutParams logoParams = new LinearLayout.LayoutParams(dp(50), dp(22));
            logoParams.rightMargin = dp(6);
            top.addView(logo, logoParams);
        }
        TextView name = text(displayNameForNode(node), 14, rgb(17, 19, 24), true);
        name.setSingleLine(true);
        name.setEllipsize(TextUtils.TruncateAt.END);
        top.addView(name, new LinearLayout.LayoutParams(0, dp(28), 1));
        card.addView(top);

        TextView value = text(node.lastValue.isEmpty() ? "---" : node.lastValue, 19, node.connected ? rgb(17, 19, 24) : rgb(168, 175, 186), true);
        value.setGravity(Gravity.CENTER);
        value.setSingleLine(true);
        value.setEllipsize(TextUtils.TruncateAt.END);
        value.setPadding(0, dp(6), 0, dp(2));
        card.addView(value);

        String summary = infoValue(node.info, "type");
        if (summary.isEmpty() || summary.equals("?")) summary = node.status;
        else summary = summary + " · " + node.status;
        TextView info = text(summary, 11, rgb(105, 112, 125), false);
        info.setGravity(Gravity.CENTER);
        info.setSingleLine(true);
        info.setEllipsize(TextUtils.TruncateAt.END);
        card.addView(info);

        LinearLayout actions = row();
        Button button = node.connected ? secondaryButton("Deconnecter") : primaryButton("Connecter");
        button.setOnClickListener(v -> connect(node));
        button.setTextSize(13);
        button.setMinHeight(dp(40));
        actions.addView(button, new LinearLayout.LayoutParams(0, dp(40), 1));

        Button settings = iconButton("⚙");
        settings.setMinHeight(dp(40));
        settings.setOnClickListener(v -> {
            stopScanForInteraction();
            showSettings(node);
        });
        LinearLayout.LayoutParams settingsParams = new LinearLayout.LayoutParams(dp(42), dp(40));
        settingsParams.leftMargin = dp(8);
        actions.addView(settings, settingsParams);

        LinearLayout.LayoutParams actionsParams = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, dp(40));
        actionsParams.topMargin = dp(8);
        card.addView(actions, actionsParams);
        return card;
    }

    @SuppressLint("MissingPermission")
    private void stopScanForInteraction() {
        if (scanning && scanner != null) {
            scanner.stopScan(scanCallback);
            scanning = false;
            subtitle.setText(nodes.size() + " node(s)");
        }
    }

    private String displayNameForNode(Node node) {
        String savedName = preferences.getString("node_name_" + node.address, "");
        if (savedName != null && !savedName.trim().isEmpty()) return savedName.trim();
        String label = infoValue(node.info, "id");
        if (!label.isEmpty() && !label.equals("CDO ?")) return label;
        return node.name;
    }

    private String defaultNodeName(Node node) {
        String label = infoValue(node.info, "id");
        if (!label.isEmpty() && !label.equals("CDO ?")) return label;
        return node.name == null || node.name.isEmpty() ? "Node balance" : node.name;
    }

    private int logoForNode(Node node) {
        String manualLogo = preferences.getString("node_logo_" + node.address, "");
        String brand = manualLogo == null || manualLogo.isEmpty()
                ? infoValue(node.info, "brand").toLowerCase(Locale.ROOT)
                : manualLogo.toLowerCase(Locale.ROOT);
        if (brand.contains("a&d") || brand.equals("ad") || brand.equals("and")) return R.drawable.logo_and;
        if (brand.contains("mettler")) return R.drawable.logo_mettler;
        if (brand.contains("ohaus")) return R.drawable.logo_ohaus;
        if (brand.contains("kern")) return R.drawable.logo_kern;
        if (brand.contains("sartorius")) return R.drawable.logo_sartorius;
        if (brand.contains("binder")) return R.drawable.logo_binder;
        if (brand.contains("masterflex")) return R.drawable.logo_masterflex;
        if (brand.contains("watson")) return R.drawable.logo_watson;
        if (brand.contains("precisa")) return R.drawable.logo_precisa;
        if (brand.contains("precia")) return R.drawable.logo_precia_molen;
        if (brand.contains("shimadzu")) return R.drawable.logo_shimadzu;
        if (brand.contains("bizerba")) return R.drawable.logo_bizerba;
        if (brand.contains("dini")) return R.drawable.logo_dini;
        return 0;
    }

    private void showSettings(Node node) {
        activeSettingsNode = node;

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackground(rounded(rgb(247, 248, 250), 18, 0, Color.TRANSPARENT));

        LinearLayout header = new LinearLayout(this);
        header.setOrientation(LinearLayout.VERTICAL);
        header.setPadding(dp(18), dp(16), dp(18), dp(12));
        header.setBackgroundColor(Color.WHITE);
        TextView title = text(displayNameForNode(node), 24, rgb(17, 19, 24), true);
        TextView meta = text(node.address + "  ·  " + node.rssi + " dBm  ·  " + node.status, 12, rgb(105, 112, 125), false);
        meta.setPadding(0, dp(4), 0, 0);
        header.addView(title);
        header.addView(meta);
        root.addView(header);

        ScrollView scroll = new ScrollView(this);
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(dp(14), dp(12), dp(14), dp(12));
        scroll.addView(box);

        box.addView(section("Identite"));
        EditText nodeName = field(preferences.getString("node_name_" + node.address, defaultNodeName(node)), "Nom du node");
        box.addView(fieldBlock("Nom du node", nodeName));
        EditText serial = field(preferences.getString("node_serial_" + node.address, infoValue(node.info, "sn")), "Numero de serie");
        box.addView(fieldBlock("Numero de serie", serial));

        final String[] selectedLogo = {preferences.getString("node_logo_" + node.address, "")};
        Button logoButton = secondaryButton("Logo : " + (selectedLogo[0].isEmpty() ? "Auto" : selectedLogo[0]));
        logoButton.setOnClickListener(v -> showLogoPicker(selectedLogo, logoButton));
        box.addView(actionBlock("Fabricant", logoButton));

        Button photo = secondaryButton("Photo : " + photoLabel(node));
        photo.setOnClickListener(v -> showPhotoLibrary(node));
        box.addView(actionBlock("Bibliotheque integree", photo));

        box.addView(section("Serie"));
        final String[] selectedSerialBrand = {preferences.getString("node_serial_brand_" + node.address, selectedLogo[0].isEmpty() ? "A&D" : selectedLogo[0])};
        ChoiceField baud = choiceField("Baud", preferences.getString("node_baud_" + node.address, valueOr(infoValue(node.info, "baud"), "2400")),
                new String[]{"1200", "2400", "4800", "9600", "19200", "38400", "57600", "115200"},
                new String[]{"1200", "2400", "4800", "9600", "19200", "38400", "57600", "115200"});
        ChoiceField parity = choiceField("Parite", preferences.getString("node_parity_" + node.address, "1"),
                new String[]{"Aucune", "Even", "Odd"}, new String[]{"0", "1", "2"});
        ChoiceField dataBits = choiceField("Data bits", preferences.getString("node_dbits_" + node.address, "7"),
                new String[]{"7 bits", "8 bits"}, new String[]{"7", "8"});
        ChoiceField stopBits = choiceField("Stop bits", preferences.getString("node_sbits_" + node.address, "1"),
                new String[]{"1 bit", "2 bits"}, new String[]{"1", "2"});
        EditText rx = field(preferences.getString("node_rx_" + node.address, "5"), "RX");
        EditText tx = field(preferences.getString("node_tx_" + node.address, "6"), "TX");
        Button serialBrand = secondaryButton("Fabricant : " + selectedSerialBrand[0]);
        serialBrand.setOnClickListener(v -> showSerialBrandPicker(selectedSerialBrand, serialBrand, baud, parity, dataBits, stopBits));
        box.addView(actionBlock("Parametres par defaut", serialBrand));
        box.addView(twoChoices(baud, parity));
        box.addView(twoChoices(dataBits, stopBits));
        box.addView(twoFields("RX", rx, "TX", tx));

        box.addView(section("Test"));
        EditText test = field("SI", "Commande de test");
        box.addView(fieldBlock("Commande", test));
        Button sendTest = primaryButton("Envoyer la commande");
        sendTest.setOnClickListener(v -> sendCommandToNode(node, test.getText().toString()));
        box.addView(sendTest, blockParams());

        root.addView(scroll, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1));

        LinearLayout footer = row();
        footer.setPadding(dp(14), dp(10), dp(14), dp(14));
        footer.setBackgroundColor(Color.WHITE);
        Button cancel = secondaryButton("Annuler");
        Button save = primaryButton("Sauvegarder");
        footer.addView(cancel, new LinearLayout.LayoutParams(0, dp(54), 1));
        LinearLayout.LayoutParams saveParams = new LinearLayout.LayoutParams(0, dp(54), 1);
        saveParams.leftMargin = dp(10);
        footer.addView(save, saveParams);
        root.addView(footer);

        AlertDialog dialog = new AlertDialog.Builder(this).create();
        dialog.setView(root, 0, 0, 0, 0);
        cancel.setOnClickListener(v -> dialog.dismiss());
        save.setOnClickListener(v -> {
            SharedPreferences.Editor editor = preferences.edit()
                    .putString("node_name_" + node.address, nodeName.getText().toString().trim())
                    .putString("node_serial_" + node.address, serial.getText().toString().trim())
                    .putString("node_logo_" + node.address, selectedLogo[0])
                    .putString("node_serial_brand_" + node.address, selectedSerialBrand[0])
                    .putString("node_baud_" + node.address, baud.value)
                    .putString("node_parity_" + node.address, parity.value)
                    .putString("node_dbits_" + node.address, dataBits.value)
                    .putString("node_sbits_" + node.address, stopBits.value)
                    .putString("node_rx_" + node.address, rx.getText().toString().trim())
                    .putString("node_tx_" + node.address, tx.getText().toString().trim());
            editor.apply();
            sendConfigToNode(node, nodeName, serial, selectedLogo[0].isEmpty() ? "Auto" : selectedLogo[0], baud.value, parity.value, dataBits.value, stopBits.value, rx, tx);
            dialog.dismiss();
            renderNow();
        });
        dialog.show();
        Window window = dialog.getWindow();
        if (window != null) {
            window.setBackgroundDrawable(rounded(Color.TRANSPARENT, 0, 0, Color.TRANSPARENT));
            int width = getResources().getDisplayMetrics().widthPixels - dp(36);
            int height = getResources().getDisplayMetrics().heightPixels - dp(34);
            window.setLayout(width, height);
        }
    }

    private TextView section(String text) {
        TextView view = text(text, 15, rgb(17, 19, 24), true);
        view.setPadding(dp(4), dp(14), 0, dp(8));
        return view;
    }

    private TextView label(String text) {
        TextView view = text(text, 12, rgb(105, 112, 125), false);
        view.setPadding(0, dp(8), 0, 0);
        return view;
    }

    private TextView detail(String title, String value) {
        TextView view = text(title + " : " + value, 14, rgb(17, 19, 24), false);
        view.setPadding(0, dp(3), 0, dp(3));
        return view;
    }

    private EditText field(String value, String hint) {
        EditText edit = new EditText(this);
        edit.setSingleLine(true);
        edit.setText(value == null ? "" : value);
        edit.setHint(hint);
        edit.setTextSize(14);
        edit.setTextColor(rgb(17, 19, 24));
        edit.setHintTextColor(rgb(132, 142, 156));
        edit.setPadding(dp(12), 0, dp(12), 0);
        edit.setBackground(rounded(Color.WHITE, 12, 1, rgb(216, 223, 232)));
        edit.setMinHeight(dp(48));
        return edit;
    }

    private LinearLayout fieldBlock(String title, EditText edit) {
        LinearLayout block = block();
        block.addView(label(title));
        block.addView(edit, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, dp(50)));
        return block;
    }

    private LinearLayout actionBlock(String title, Button button) {
        LinearLayout block = block();
        block.addView(label(title));
        block.addView(button, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, dp(50)));
        return block;
    }

    private LinearLayout twoFields(String leftTitle, EditText left, String rightTitle, EditText right) {
        LinearLayout row = row();
        row.setPadding(0, 0, 0, dp(8));
        LinearLayout leftBlock = fieldBlock(leftTitle, left);
        LinearLayout rightBlock = fieldBlock(rightTitle, right);
        row.addView(leftBlock, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        LinearLayout.LayoutParams rightParams = new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1);
        rightParams.leftMargin = dp(10);
        row.addView(rightBlock, rightParams);
        return row;
    }

    private LinearLayout twoFields(String leftTitle, EditText left, ChoiceField right) {
        LinearLayout row = row();
        row.setPadding(0, 0, 0, dp(8));
        LinearLayout leftBlock = fieldBlock(leftTitle, left);
        row.addView(leftBlock, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        LinearLayout.LayoutParams rightParams = new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1);
        rightParams.leftMargin = dp(10);
        row.addView(right.view, rightParams);
        return row;
    }

    private LinearLayout twoChoices(ChoiceField left, ChoiceField right) {
        LinearLayout row = row();
        row.setPadding(0, 0, 0, dp(8));
        row.addView(left.view, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        LinearLayout.LayoutParams rightParams = new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1);
        rightParams.leftMargin = dp(10);
        row.addView(right.view, rightParams);
        return row;
    }

    private ChoiceField choiceField(String title, String currentValue, String[] labels, String[] values) {
        ChoiceField field = new ChoiceField();
        field.labels = labels;
        field.values = values;
        field.value = currentValue == null || currentValue.isEmpty() ? values[0] : currentValue;
        LinearLayout block = block();
        block.addView(label(title));
        Button button = secondaryButton(labelForValue(field.value, labels, values));
        field.button = button;
        button.setGravity(Gravity.CENTER_VERTICAL | Gravity.LEFT);
        button.setPadding(dp(12), 0, dp(12), 0);
        button.setOnClickListener(v -> new AlertDialog.Builder(this)
                .setTitle(title)
                .setItems(labels, (dialog, which) -> {
                    setChoice(field, values[which]);
                })
                .show());
        block.addView(button, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, dp(50)));
        field.view = block;
        return field;
    }

    private void setChoice(ChoiceField field, String value) {
        field.value = value;
        if (field.button != null) {
            field.button.setText(labelForValue(value, field.labels, field.values));
        }
    }

    private void showSerialBrandPicker(String[] selectedBrand, Button button, ChoiceField baud, ChoiceField parity, ChoiceField dataBits, ChoiceField stopBits) {
        String[] brands = {"A&D", "Mettler", "Ohaus", "Kern", "Sartorius", "Precisa", "Precia Molen", "Shimadzu", "Bizerba", "Dini"};
        new AlertDialog.Builder(this)
                .setTitle("Fabricant")
                .setItems(brands, (dialog, which) -> {
                    selectedBrand[0] = brands[which];
                    button.setText("Fabricant : " + selectedBrand[0]);
                    applySerialDefaults(selectedBrand[0], baud, parity, dataBits, stopBits);
                })
                .show();
    }

    private void applySerialDefaults(String brand, ChoiceField baud, ChoiceField parity, ChoiceField dataBits, ChoiceField stopBits) {
        String b = brand == null ? "" : brand.toLowerCase(Locale.ROOT);
        if (b.contains("a&d")) {
            setChoice(baud, "2400");
            setChoice(parity, "1");
            setChoice(dataBits, "7");
            setChoice(stopBits, "1");
        } else if (b.contains("mettler")) {
            setChoice(baud, "9600");
            setChoice(parity, "0");
            setChoice(dataBits, "8");
            setChoice(stopBits, "1");
        } else if (b.contains("sartorius")) {
            setChoice(baud, "9600");
            setChoice(parity, "2");
            setChoice(dataBits, "8");
            setChoice(stopBits, "1");
        } else {
            setChoice(baud, "9600");
            setChoice(parity, "0");
            setChoice(dataBits, "8");
            setChoice(stopBits, "1");
        }
    }

    private String labelForValue(String value, String[] labels, String[] values) {
        for (int i = 0; i < values.length; i++) {
            if (values[i].equals(value)) return labels[i];
        }
        return labels[0];
    }

    private LinearLayout block() {
        LinearLayout block = new LinearLayout(this);
        block.setOrientation(LinearLayout.VERTICAL);
        block.setPadding(dp(12), dp(10), dp(12), dp(12));
        block.setBackground(rounded(Color.WHITE, 14, 1, rgb(232, 236, 242)));
        LinearLayout.LayoutParams params = blockParams();
        block.setLayoutParams(params);
        return block;
    }

    private LinearLayout.LayoutParams blockParams() {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        params.setMargins(0, 0, 0, dp(10));
        return params;
    }

    private void showLogoPicker(String[] selectedLogo, Button button) {
        String[] logos = {"Auto", "A&D", "Mettler", "Ohaus", "Kern", "Sartorius", "Binder", "Masterflex", "Watson", "Precisa", "Precia Molen", "Shimadzu", "Bizerba", "Dini"};
        new AlertDialog.Builder(this)
                .setTitle("Logo fabricant")
                .setItems(logos, (dialog, which) -> {
                    selectedLogo[0] = which == 0 ? "" : logos[which];
                    button.setText("Logo : " + (selectedLogo[0].isEmpty() ? "Auto" : selectedLogo[0]));
                })
                .show();
    }

    private int logoIndex(String value) {
        if (value == null || value.isEmpty()) return 0;
        String[] logos = {"auto", "a&d", "mettler", "ohaus", "kern", "sartorius", "binder", "masterflex", "watson", "precisa", "precia molen", "shimadzu", "bizerba", "dini"};
        String needle = value.toLowerCase(Locale.ROOT);
        for (int i = 0; i < logos.length; i++) {
            if (logos[i].equals(needle)) return i;
        }
        return 0;
    }

    private String valueOr(String value, String fallback) {
        return value == null || value.isEmpty() || value.equals("?") ? fallback : value;
    }

    private void sendConfigToNode(Node node, EditText nodeName, EditText serial, String logo, String baud, String parity, String dataBits, String stopBits, EditText rx, EditText tx) {
        if (!node.connected || node.commandChar == null || node.gatt == null) return;
        try {
            JSONObject json = new JSONObject();
            if (!logo.equals("Auto")) json.put("brand", logo);
            json.put("name", nodeName.getText().toString().trim());
            json.put("sn", serial.getText().toString().trim());
            json.put("baud", Integer.parseInt(valueOr(baud, "2400")));
            json.put("parity", Integer.parseInt(valueOr(parity, "0")));
            json.put("dbits", Integer.parseInt(valueOr(dataBits, "8")));
            json.put("sbits", Integer.parseInt(valueOr(stopBits, "1")));
            json.put("rx", Integer.parseInt(valueOr(rx.getText().toString().trim(), "5")));
            json.put("tx", Integer.parseInt(valueOr(tx.getText().toString().trim(), "6")));
            sendCommandToNode(node, "LC:CFG:" + json.toString());
        } catch (Exception e) {
            node.status = "Config invalide";
        }
    }

    private void sendCommandToNode(Node node, String command) {
        if (command == null || command.trim().isEmpty()) return;
        if (!node.connected || node.commandChar == null || node.gatt == null) {
            node.status = "Non connecte";
            renderNow();
            return;
        }
        byte[] payload = (command.trim() + "\r\n").getBytes(StandardCharsets.UTF_8);
        node.commandChar.setValue(payload);
        node.commandChar.setWriteType(BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT);
        boolean ok = node.gatt.writeCharacteristic(node.commandChar);
        node.status = ok ? "Commande envoyee" : "Envoi impossible";
        renderNow();
    }

    private int modelImageForNode(Node node) {
        int selected = modelImageForKey(preferences.getString("node_model_" + node.address, ""));
        if (selected != 0) return selected;

        String brand = infoValue(node.info, "brand").toLowerCase(Locale.ROOT);
        if (!(brand.contains("a&d") || brand.equals("ad") || brand.equals("and"))) return 0;

        String type = infoValue(node.info, "type").toUpperCase(Locale.ROOT);
        if (type.contains("BA")) return modelImageForKey("and_ba");
        if (type.contains("HR")) return modelImageForKey("and_hr");
        if (type.contains("MC")) return modelImageForKey("and_mc");
        if (type.contains("GX")) return modelImageForKey("and_gx");
        return 0;
    }

    private int modelImageForKey(String key) {
        if (key == null) return 0;
        switch (key) {
            case "and_ba": return R.drawable.model_and_ba;
            case "and_hr": return R.drawable.model_and_hr;
            case "and_mc": return R.drawable.model_and_mc;
            case "and_gx": return R.drawable.model_and_gx;
            default: return 0;
        }
    }

    private void chooseNodeImage(Node node) {
        pendingImageNodeAddress = node.address;
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("image/*");
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        startActivityForResult(intent, PICK_NODE_IMAGE);
    }

    private void showPhotoLibrary(Node node) {
        String[] labels = {
                "Automatique selon ?TN",
                "A&D BA",
                "A&D HR",
                "A&D MC",
                "A&D GX",
                "Importer depuis le stockage"
        };
        String[] keys = {"", "and_ba", "and_hr", "and_mc", "and_gx", "__import__"};
        new AlertDialog.Builder(this)
                .setTitle("Photo balance")
                .setItems(labels, (dialog, which) -> {
                    String key = keys[which];
                    if ("__import__".equals(key)) {
                        chooseNodeImage(node);
                        return;
                    }
                    preferences.edit()
                            .putString("node_model_" + node.address, key)
                            .remove("node_image_" + node.address)
                            .apply();
                    renderNow();
                })
                .show();
    }

    private String photoLabel(Node node) {
        String manual = preferences.getString("node_image_" + node.address, "");
        if (manual != null && !manual.isEmpty()) return "Importee";
        String key = preferences.getString("node_model_" + node.address, "");
        if ("and_ba".equals(key)) return "Bibliotheque A&D BA";
        if ("and_hr".equals(key)) return "Bibliotheque A&D HR";
        if ("and_mc".equals(key)) return "Bibliotheque A&D MC";
        if ("and_gx".equals(key)) return "Bibliotheque A&D GX";
        return "Automatique";
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != PICK_NODE_IMAGE || resultCode != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();
        if (pendingImageNodeAddress == null) return;
        final int flags = data.getFlags() & Intent.FLAG_GRANT_READ_URI_PERMISSION;
        try {
            getContentResolver().takePersistableUriPermission(uri, flags);
        } catch (SecurityException ignored) {
        }
        preferences.edit()
                .putString("node_image_" + pendingImageNodeAddress, uri.toString())
                .remove("node_model_" + pendingImageNodeAddress)
                .apply();
        pendingImageNodeAddress = null;
        renderNow();
    }

    private String infoValue(String info, String key) {
        if (info == null || info.isEmpty()) return "";
        String prefix = key + "=";
        String[] parts = info.split(";");
        for (String part : parts) {
            if (part.startsWith(prefix)) return part.substring(prefix.length()).trim();
        }
        return "";
    }

    private LinearLayout row() {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        return row;
    }

    private TextView text(String value, int sp, int color, boolean bold) {
        TextView text = new TextView(this);
        text.setText(value);
        text.setTextSize(sp);
        text.setTextColor(color);
        if (bold) text.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        return text;
    }

    private Button headerButton(String text) {
        Button button = new Button(this);
        button.setText(text);
        button.setAllCaps(false);
        button.setTextColor(rgb(10, 132, 255));
        button.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        button.setBackgroundColor(Color.TRANSPARENT);
        return button;
    }

    private Button primaryButton(String text) {
        Button button = new Button(this);
        button.setText(text);
        button.setAllCaps(false);
        button.setTextColor(Color.WHITE);
        button.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        button.setBackground(rounded(rgb(10, 132, 255), 14, 0, Color.TRANSPARENT));
        return button;
    }

    private Button secondaryButton(String text) {
        Button button = new Button(this);
        button.setText(text);
        button.setAllCaps(false);
        button.setTextColor(rgb(10, 132, 255));
        button.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        button.setBackground(rounded(Color.WHITE, 14, 1, rgb(210, 218, 230)));
        return button;
    }

    private Button iconButton(String text) {
        Button button = secondaryButton(text);
        button.setTextSize(18);
        button.setMinWidth(0);
        button.setMinimumWidth(0);
        button.setPadding(0, 0, 0, 0);
        button.setGravity(Gravity.CENTER);
        button.setBackground(rounded(rgb(246, 248, 252), 13, 1, rgb(214, 222, 234)));
        return button;
    }

    private GradientDrawable rounded(int color, int radiusDp, int strokeDp, int strokeColor) {
        GradientDrawable drawable = new GradientDrawable();
        drawable.setColor(color);
        drawable.setCornerRadius(dp(radiusDp));
        if (strokeDp > 0) drawable.setStroke(dp(strokeDp), strokeColor);
        return drawable;
    }

    @SuppressLint("MissingPermission")
    private String safeName(BluetoothDevice device) {
        String name = device.getName();
        return name == null || name.isEmpty() ? "AtomS3" : name;
    }

    private int rgb(int r, int g, int b) {
        return Color.rgb(r, g, b);
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private static class Node {
        final String address;
        final BluetoothDevice device;
        String name = "AtomS3";
        String status = "Detecte";
        String lastValue = "";
        String info = "";
        int rssi = -127;
        long seenAt = 0;
        boolean connected = false;
        BluetoothGatt gatt;
        BluetoothGattCharacteristic valueChar;
        BluetoothGattCharacteristic commandChar;
        BluetoothGattCharacteristic infoChar;

        Node(String address, BluetoothDevice device) {
            this.address = address;
            this.device = device;
        }
    }

    private static class ChoiceField {
        LinearLayout view;
        Button button;
        String[] labels;
        String[] values;
        String value;
    }
}
