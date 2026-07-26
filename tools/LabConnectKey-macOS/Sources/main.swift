import AppKit
import ApplicationServices
import CoreBluetooth
import Darwin
import UniformTypeIdentifiers

private enum EndingMode: Int {
    case enter = 0
    case tab = 1
    case none = 2
}

private struct MetadataProfile: Codable {
    let version: Int
    let enabled: [String: Bool]
    let values: [String: String]
    let order: [String: Int]
    let separator: Int
    let customSeparator: String
}

private final class SerialConnection {
    var onLine: ((String) -> Void)?
    var onError: ((String) -> Void)?

    private var descriptor: Int32 = -1
    private var source: DispatchSourceRead?
    private var lineBuffer = Data()
    private let queue = DispatchQueue(label: "fr.labconnect.key.serial")

    var isOpen: Bool { descriptor >= 0 }

    func open(path: String, baud: Int = 9600) throws {
        close()

        let fd = Darwin.open(path, O_RDWR | O_NOCTTY | O_NONBLOCK)
        guard fd >= 0 else {
            throw NSError(
                domain: "LabConnectKey.Serial",
                code: Int(errno),
                userInfo: [NSLocalizedDescriptionKey: "Impossible d’ouvrir \(path) : \(String(cString: strerror(errno)))"]
            )
        }

        guard fcntl(fd, F_SETFL, 0) == 0 else {
            let error = errno
            Darwin.close(fd)
            throw NSError(
                domain: "LabConnectKey.Serial",
                code: Int(error),
                userInfo: [NSLocalizedDescriptionKey: "Impossible de préparer le port : \(String(cString: strerror(error)))"]
            )
        }

        var options = termios()
        guard tcgetattr(fd, &options) == 0 else {
            let error = errno
            Darwin.close(fd)
            throw NSError(
                domain: "LabConnectKey.Serial",
                code: Int(error),
                userInfo: [NSLocalizedDescriptionKey: "Impossible de lire les paramètres du port."]
            )
        }

        cfmakeraw(&options)
        options.c_cflag |= tcflag_t(CLOCAL | CREAD)
        options.c_cflag &= ~tcflag_t(CSIZE | PARENB | PARODD | CSTOPB)
        options.c_cflag |= tcflag_t(CS8)
        options.c_cc.16 = 0
        options.c_cc.17 = 1
        let speed = serialSpeed(for: baud)
        cfsetispeed(&options, speed)
        cfsetospeed(&options, speed)

        guard tcsetattr(fd, TCSANOW, &options) == 0 else {
            let error = errno
            Darwin.close(fd)
            throw NSError(
                domain: "LabConnectKey.Serial",
                code: Int(error),
                userInfo: [NSLocalizedDescriptionKey: "Impossible d’appliquer les paramètres du port."]
            )
        }

        tcflush(fd, TCIOFLUSH)
        descriptor = fd
        lineBuffer.removeAll(keepingCapacity: true)

        let readSource = DispatchSource.makeReadSource(fileDescriptor: fd, queue: queue)
        readSource.setEventHandler { [weak self] in self?.readAvailableBytes() }
        source = readSource
        readSource.resume()
    }

    func close() {
        let fd = descriptor
        descriptor = -1
        source?.cancel()
        source = nil
        if fd >= 0 { Darwin.close(fd) }
        lineBuffer.removeAll(keepingCapacity: false)
    }

    func send(_ text: String, ending: String) throws {
        guard descriptor >= 0 else {
            throw NSError(
                domain: "LabConnectKey.Serial",
                code: 1,
                userInfo: [NSLocalizedDescriptionKey: "Aucun port n’est connecté."]
            )
        }
        guard let data = (text + ending).data(using: .utf8) else { return }
        try data.withUnsafeBytes { rawBuffer in
            guard let base = rawBuffer.baseAddress else { return }
            var total = 0
            while total < data.count {
                let written = Darwin.write(descriptor, base.advanced(by: total), data.count - total)
                if written < 0 {
                    throw NSError(
                        domain: "LabConnectKey.Serial",
                        code: Int(errno),
                        userInfo: [NSLocalizedDescriptionKey: "Échec de l’envoi : \(String(cString: strerror(errno)))"]
                    )
                }
                total += written
            }
        }
    }

    deinit {
        close()
    }

    private func readAvailableBytes() {
        guard descriptor >= 0 else { return }
        var bytes = [UInt8](repeating: 0, count: 512)

        while true {
            let count = Darwin.read(descriptor, &bytes, bytes.count)
            if count > 0 {
                consume(Data(bytes[0..<count]))
            } else if count == 0 {
                DispatchQueue.main.async { [weak self] in
                    self?.onError?("La connexion a été fermée.")
                }
                return
            } else {
                if errno != EAGAIN && errno != EWOULDBLOCK {
                    let message = String(cString: strerror(errno))
                    DispatchQueue.main.async { [weak self] in
                        self?.onError?("Erreur de lecture : \(message)")
                    }
                }
                return
            }
        }
    }

    private func consume(_ data: Data) {
        for byte in data {
            if byte == 0x0D || byte == 0x0A {
                if !lineBuffer.isEmpty {
                    let line = String(decoding: lineBuffer, as: UTF8.self)
                    lineBuffer.removeAll(keepingCapacity: true)
                    DispatchQueue.main.async { [weak self] in self?.onLine?(line) }
                }
            } else {
                if lineBuffer.count < 4096 {
                    lineBuffer.append(byte)
                } else {
                    lineBuffer.removeAll(keepingCapacity: true)
                    DispatchQueue.main.async { [weak self] in
                        self?.onError?("Une ligne trop longue a été ignorée.")
                    }
                }
            }
        }
    }

    private func serialSpeed(for baud: Int) -> speed_t {
        switch baud {
        case 300: return speed_t(B300)
        case 600: return speed_t(B600)
        case 1200: return speed_t(B1200)
        case 2400: return speed_t(B2400)
        case 4800: return speed_t(B4800)
        case 19200: return speed_t(B19200)
        case 38400: return speed_t(B38400)
        case 57600: return speed_t(B57600)
        case 115200: return speed_t(B115200)
        default: return speed_t(B9600)
        }
    }
}

private final class BluetoothConnection: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    static let serviceUUID = CBUUID(string: "6E400001-B5A3-F393-E0A9-E50E24DCCA9E")
    static let rxUUID = CBUUID(string: "6E400002-B5A3-F393-E0A9-E50E24DCCA9E")
    static let txUUID = CBUUID(string: "6E400003-B5A3-F393-E0A9-E50E24DCCA9E")

    var onDevicesChanged: (() -> Void)?
    var onLine: ((String) -> Void)?
    var onState: ((String, Bool) -> Void)?
    var onError: ((String) -> Void)?

    private lazy var central = CBCentralManager(delegate: self, queue: .main)
    private(set) var devices: [(peripheral: CBPeripheral, name: String)] = []
    private var peripheral: CBPeripheral?
    private var rxCharacteristic: CBCharacteristic?
    private var lineBuffer = Data()
    private var wantsScan = false
    private var userRequestedDisconnect = false

    var isConnected: Bool {
        peripheral?.state == .connected && rxCharacteristic != nil
    }

    func scan() {
        _ = central
        wantsScan = true
        devices.removeAll()
        onDevicesChanged?()
        guard central.state == .poweredOn else {
            onState?("Activation du Bluetooth en attente…", false)
            return
        }
        central.stopScan()
        central.scanForPeripherals(
            withServices: [Self.serviceUUID],
            options: [CBCentralManagerScanOptionAllowDuplicatesKey: false]
        )
        onState?("Recherche des LabConnect pour Mac…", false)
    }

    func connect(deviceAt index: Int) {
        guard devices.indices.contains(index) else {
            onError?("Sélectionnez un LabConnect détecté.")
            return
        }
        userRequestedDisconnect = false
        let selected = devices[index].peripheral
        peripheral = selected
        selected.delegate = self
        central.stopScan()
        onState?("Connexion à \(devices[index].name)…", false)
        central.connect(selected)
    }

    func disconnect() {
        userRequestedDisconnect = true
        if let peripheral {
            central.cancelPeripheralConnection(peripheral)
        }
        self.peripheral = nil
        rxCharacteristic = nil
        lineBuffer.removeAll(keepingCapacity: false)
        onState?("Déconnecté", false)
    }

    func send(_ text: String, ending: String) throws {
        guard let peripheral, let rxCharacteristic, peripheral.state == .connected else {
            throw NSError(
                domain: "LabConnectKey.Bluetooth",
                code: 1,
                userInfo: [NSLocalizedDescriptionKey: "Aucun LabConnect n’est connecté."]
            )
        }
        guard let data = (text + ending).data(using: .utf8) else { return }
        let maximum = max(20, peripheral.maximumWriteValueLength(for: .withResponse))
        var offset = 0
        while offset < data.count {
            let end = min(offset + maximum, data.count)
            peripheral.writeValue(data.subdata(in: offset..<end), for: rxCharacteristic, type: .withResponse)
            offset = end
        }
    }

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        switch central.state {
        case .poweredOn:
            if wantsScan { scan() }
        case .poweredOff:
            onState?("Bluetooth désactivé sur ce Mac", false)
        case .unauthorized:
            onError?("Autorisez le Bluetooth pour LabConnect Key dans les réglages macOS.")
        case .unsupported:
            onError?("Ce Mac ne prend pas en charge le Bluetooth requis.")
        default:
            onState?("Bluetooth indisponible", false)
        }
    }

    func centralManager(
        _ central: CBCentralManager,
        didDiscover peripheral: CBPeripheral,
        advertisementData: [String: Any],
        rssi RSSI: NSNumber
    ) {
        guard !devices.contains(where: { $0.peripheral.identifier == peripheral.identifier }) else { return }
        let advertisedName = advertisementData[CBAdvertisementDataLocalNameKey] as? String
        let name = advertisedName ?? peripheral.name ?? "LabConnect pour Mac"
        devices.append((peripheral, name))
        devices.sort { $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending }
        onDevicesChanged?()
        onState?("\(devices.count) LabConnect détecté(s)", false)
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        peripheral.delegate = self
        onState?("Connexion établie, préparation…", false)
        peripheral.discoverServices([Self.serviceUUID])
    }

    func centralManager(
        _ central: CBCentralManager,
        didFailToConnect peripheral: CBPeripheral,
        error: Error?
    ) {
        onError?("Connexion impossible : \(error?.localizedDescription ?? "erreur inconnue")")
    }

    func centralManager(
        _ central: CBCentralManager,
        didDisconnectPeripheral peripheral: CBPeripheral,
        error: Error?
    ) {
        rxCharacteristic = nil
        lineBuffer.removeAll(keepingCapacity: false)
        if userRequestedDisconnect {
            onState?("Déconnecté", false)
            return
        }

        onState?("Connexion perdue, nouvelle tentative…", false)
        self.peripheral = peripheral
        central.connect(peripheral)
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        if let error {
            onError?("Service introuvable : \(error.localizedDescription)")
            return
        }
        guard let service = peripheral.services?.first(where: { $0.uuid == Self.serviceUUID }) else {
            onError?("Ce périphérique n’est pas un LabConnect compatible Mac.")
            return
        }
        peripheral.discoverCharacteristics([Self.rxUUID, Self.txUUID], for: service)
    }

    func peripheral(
        _ peripheral: CBPeripheral,
        didDiscoverCharacteristicsFor service: CBService,
        error: Error?
    ) {
        if let error {
            onError?("Canaux Bluetooth indisponibles : \(error.localizedDescription)")
            return
        }
        guard let characteristics = service.characteristics,
              let rx = characteristics.first(where: { $0.uuid == Self.rxUUID }),
              let tx = characteristics.first(where: { $0.uuid == Self.txUUID }) else {
            onError?("Canaux LabConnect incomplets.")
            return
        }
        rxCharacteristic = rx
        peripheral.setNotifyValue(true, for: tx)
    }

    func peripheral(
        _ peripheral: CBPeripheral,
        didUpdateNotificationStateFor characteristic: CBCharacteristic,
        error: Error?
    ) {
        if let error {
            onError?("Réception impossible : \(error.localizedDescription)")
            return
        }
        if characteristic.uuid == Self.txUUID && characteristic.isNotifying {
            onState?("Connecté à \(peripheral.name ?? "LabConnect pour Mac")", true)
        }
    }

    func peripheral(
        _ peripheral: CBPeripheral,
        didUpdateValueFor characteristic: CBCharacteristic,
        error: Error?
    ) {
        if let error {
            onError?("Erreur de réception : \(error.localizedDescription)")
            return
        }
        guard characteristic.uuid == Self.txUUID, let data = characteristic.value else { return }
        consume(data)
    }

    private func consume(_ data: Data) {
        for byte in data {
            if byte == 0x0D || byte == 0x0A {
                if !lineBuffer.isEmpty {
                    let line = String(decoding: lineBuffer, as: UTF8.self)
                    lineBuffer.removeAll(keepingCapacity: true)
                    onLine?(line)
                }
            } else if lineBuffer.count < 4096 {
                lineBuffer.append(byte)
            } else {
                lineBuffer.removeAll(keepingCapacity: true)
                onError?("Une ligne trop longue a été ignorée.")
            }
        }
    }
}

private final class AppDelegate: NSObject, NSApplicationDelegate, NSWindowDelegate, NSTextFieldDelegate {
    private let serial = SerialConnection()
    private let bluetooth = BluetoothConnection()
    private var window: NSWindow!
    private var transportPopup: NSPopUpButton!
    private var portPopup: NSPopUpButton!
    private var connectButton: NSButton!
    private var statusLabel: NSTextField!
    private var logView: NSTextView!
    private var autoTypeCheckbox: NSButton!
    private var endingPopup: NSPopUpButton!
    private var themePopup: NSPopUpButton!
    private var accessibilityPromptShown = false
    private var metadataCheckboxes: [String: NSButton] = [:]
    private var metadataFields: [String: NSTextField] = [:]
    private var metadataOrderPopups: [String: NSPopUpButton] = [:]
    private var metadataOrder: [String: Int] = [:]
    private var metadataRowViews: [String: NSView] = [:]
    private var metadataRowsContainer: NSStackView!
    private var metadataSeparatorPopup: NSPopUpButton!
    private var customSeparatorField: NSTextField!
    private var metadataPreviewField: NSTextField!
    private var metadataWindow: NSWindow!
    private var metadataSummaryLabel: NSTextField!
    private var metadataProfileLabel: NSTextField!
    private var metadataFileLabel: NSTextField!
    private var activeMetadataProfileName: String = {
        let saved =
            UserDefaults.standard.string(forKey: "activeMetadataProfileName") ?? "Défaut"
        if saved == "Défaut" || saved.lowercased().hasSuffix(".json") {
            return saved
        }
        return saved + ".json"
    }()
    private var metadataProfileModified =
        UserDefaults.standard.bool(forKey: "metadataProfileModified")
    private let metadataDefinitions: [(key: String, label: String)] = [
        ("balanceId", "ID balance"),
        ("location", "Localisation"),
        ("user", "Utilisateur"),
        ("serialNumber", "Numéro de série"),
        ("brand", "Marque"),
        ("model", "Modèle")
    ]
    private var flowDefinitions: [(key: String, label: String)] {
        metadataDefinitions + [("measurement", "Pesée")]
    }
    private var commandField: NSTextField!
    private var sendEndingPopup: NSPopUpButton!
    private var lastLineLabel: NSTextField!

    func applicationDidFinishLaunching(_ notification: Notification) {
        buildWindow()
        configureSerialCallbacks()
        configureBluetoothCallbacks()
        refreshPorts()
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        DispatchQueue.main.async { [weak self] in
            self?.showAccessibilitySetupIfNeeded()
        }
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        true
    }

    func windowWillClose(_ notification: Notification) {
        serial.close()
        bluetooth.disconnect()
    }

    private func buildWindow() {
        window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 900, height: 720),
            styleMask: [.titled, .closable, .miniaturizable, .resizable, .fullSizeContentView],
            backing: .buffered,
            defer: false
        )
        window.title = "LabConnect Key"
        window.titleVisibility = .hidden
        window.titlebarAppearsTransparent = true
        window.isMovableByWindowBackground = true
        window.minSize = NSSize(width: 780, height: 640)
        window.center()
        window.delegate = self

        let background = NSVisualEffectView()
        background.material = .windowBackground
        background.blendingMode = .behindWindow
        background.state = .active
        background.translatesAutoresizingMaskIntoConstraints = false
        window.contentView = background

        let root = NSStackView()
        root.orientation = .vertical
        root.alignment = .leading
        root.spacing = 16
        root.edgeInsets = NSEdgeInsets(top: 0, left: 0, bottom: 0, right: 0)
        root.translatesAutoresizingMaskIntoConstraints = false
        background.addSubview(root)
        NSLayoutConstraint.activate([
            root.leadingAnchor.constraint(equalTo: background.leadingAnchor, constant: 24),
            root.trailingAnchor.constraint(equalTo: background.trailingAnchor, constant: -24),
            root.topAnchor.constraint(equalTo: background.topAnchor, constant: 34),
            root.bottomAnchor.constraint(equalTo: background.bottomAnchor, constant: -24)
        ])

        let brandRow = NSStackView()
        brandRow.orientation = .horizontal
        brandRow.alignment = .lastBaseline
        brandRow.spacing = 10
        root.addArrangedSubview(brandRow)

        let wordmark = NSMutableAttributedString(
            string: "Lab",
            attributes: [
                .font: NSFont.systemFont(ofSize: 30, weight: .bold),
                .foregroundColor: NSColor.labelColor
            ]
        )
        let connectLetters = Array("Connect")
        for (index, letter) in connectLetters.enumerated() {
            let progress = CGFloat(index) / CGFloat(connectLetters.count - 1)
            let red = 0.20 + (0.10 - 0.20) * progress
            let green = 0.53 + (0.80 - 0.53) * progress
            let blue = 1.00 + (0.91 - 1.00) * progress
            wordmark.append(
                NSAttributedString(
                    string: String(letter),
                    attributes: [
                        .font: NSFont.systemFont(ofSize: 30, weight: .bold),
                        .foregroundColor: NSColor(
                            calibratedRed: red,
                            green: green,
                            blue: blue,
                            alpha: 1
                        )
                    ]
                )
            )
        }
        brandRow.addArrangedSubview(NSTextField(labelWithAttributedString: wordmark))

        let productName = NSTextField(labelWithString: "KEY")
        productName.font = .systemFont(ofSize: 17, weight: .bold)
        productName.textColor = NSColor(calibratedRed: 0.45, green: 0.86, blue: 1, alpha: 1)
        brandRow.addArrangedSubview(productName)

        let brandSpacer = NSView()
        brandRow.addArrangedSubview(brandSpacer)
        brandSpacer.setContentHuggingPriority(.defaultLow, for: .horizontal)

        themePopup = NSPopUpButton()
        themePopup.addItems(withTitles: ["Automatique", "Clair", "Sombre"])
        themePopup.target = self
        themePopup.action = #selector(themeChanged)
        themePopup.selectItem(
            at: max(0, min(UserDefaults.standard.integer(forKey: "themeMode"), 2))
        )
        brandRow.addArrangedSubview(themePopup)
        brandRow.widthAnchor.constraint(equalTo: root.widthAnchor).isActive = true
        applyTheme()

        let subtitle = NSTextField(wrappingLabelWithString: "Vos mesures, directement dans l’application de votre choix.")
        subtitle.textColor = .secondaryLabelColor
        root.addArrangedSubview(subtitle)
        subtitle.widthAnchor.constraint(equalTo: root.widthAnchor, constant: -40).isActive = true

        let connectionBox = makeBox(title: "Connexion")
        root.addArrangedSubview(connectionBox.box)
        connectionBox.box.widthAnchor.constraint(equalTo: root.widthAnchor).isActive = true

        let connectionRow = NSStackView()
        connectionRow.orientation = .horizontal
        connectionRow.alignment = .centerY
        connectionRow.spacing = 8
        connectionBox.content.addArrangedSubview(connectionRow)

        transportPopup = NSPopUpButton()
        transportPopup.addItems(withTitles: ["Mode Bluetooth", "Mode filaire"])
        transportPopup.target = self
        transportPopup.action = #selector(transportChanged)
        transportPopup.selectItem(
            at: max(0, min(UserDefaults.standard.integer(forKey: "transportMode"), 1))
        )
        connectionRow.addArrangedSubview(transportPopup)

        portPopup = NSPopUpButton()
        portPopup.setContentHuggingPriority(.defaultLow, for: .horizontal)
        connectionRow.addArrangedSubview(portPopup)

        let refreshButton = NSButton(title: "Actualiser", target: self, action: #selector(refreshPortsAction))
        connectionRow.addArrangedSubview(refreshButton)

        connectButton = NSButton(title: "Connecter", target: self, action: #selector(toggleConnection))
        connectButton.bezelStyle = .rounded
        connectButton.bezelColor = NSColor(calibratedRed: 0.15, green: 0.55, blue: 1, alpha: 1)
        connectionRow.addArrangedSubview(connectButton)

        statusLabel = NSTextField(labelWithString: "Non connecté")
        statusLabel.textColor = .secondaryLabelColor
        connectionBox.content.addArrangedSubview(statusLabel)

        let typingBox = makeBox(title: "Envoyer les mesures dans Excel, Numbers…")
        root.addArrangedSubview(typingBox.box)
        typingBox.box.widthAnchor.constraint(equalTo: root.widthAnchor).isActive = true

        autoTypeCheckbox = NSButton(
            checkboxWithTitle: "Activer la saisie automatique",
            target: self,
            action: #selector(autoTypeChanged)
        )
        autoTypeCheckbox.state = .off
        autoTypeCheckbox.state = UserDefaults.standard.bool(forKey: "autoTypeEnabled") ? .on : .off
        autoTypeCheckbox.font = .systemFont(ofSize: 14, weight: .semibold)
        autoTypeCheckbox.contentTintColor = NSColor(calibratedRed: 0.12, green: 0.76, blue: 0.91, alpha: 1)
        typingBox.content.addArrangedSubview(autoTypeCheckbox)

        let typingHelp = NSTextField(
            wrappingLabelWithString: "Placez le curseur dans la cellule de destination. Chaque mesure reçue sera écrite automatiquement."
        )
        typingHelp.textColor = .secondaryLabelColor
        typingBox.content.addArrangedSubview(typingHelp)

        let endingRow = NSStackView()
        endingRow.orientation = .horizontal
        endingRow.alignment = .centerY
        endingRow.spacing = 10
        typingBox.content.addArrangedSubview(endingRow)

        let endingLabel = NSTextField(labelWithString: "Après la mesure :")
        endingLabel.alignment = .right
        endingLabel.widthAnchor.constraint(equalToConstant: 150).isActive = true
        endingRow.addArrangedSubview(endingLabel)
        endingPopup = NSPopUpButton()
        endingPopup.addItems(withTitles: [
            "Passer à la ligne (Entrée)",
            "Cellule suivante (Tabulation)",
            "Ne rien faire"
        ])
        endingPopup.target = self
        endingPopup.action = #selector(savePreferences)
        endingPopup.selectItem(
            at: max(0, min(UserDefaults.standard.integer(forKey: "endingMode"), 2))
        )
        endingRow.addArrangedSubview(endingPopup)

        lastLineLabel = NSTextField(labelWithString: "Dernière mesure : —")
        lastLineLabel.font = .monospacedSystemFont(ofSize: 15, weight: .medium)
        typingBox.content.addArrangedSubview(lastLineLabel)

        let metadataRow = NSStackView()
        metadataRow.orientation = .horizontal
        metadataRow.alignment = .centerY
        metadataRow.spacing = 10
        typingBox.content.addArrangedSubview(metadataRow)

        metadataSummaryLabel = NSTextField(labelWithString: "Informations jointes : aucune")
        metadataSummaryLabel.textColor = .secondaryLabelColor
        metadataSummaryLabel.setContentHuggingPriority(.defaultLow, for: .horizontal)
        metadataRow.addArrangedSubview(metadataSummaryLabel)

        let metadataButton = NSButton(
            title: "Configurer…",
            target: self,
            action: #selector(openMetadataWindow)
        )
        metadataRow.addArrangedSubview(metadataButton)

        buildMetadataWindow()
        updateMetadataSummary()

        let monitorBox = makeBox(title: "Moniteur")
        root.addArrangedSubview(monitorBox.box)
        monitorBox.box.widthAnchor.constraint(equalTo: root.widthAnchor).isActive = true
        monitorBox.box.setContentHuggingPriority(.defaultLow, for: .vertical)

        let monitorToolbar = NSStackView()
        monitorToolbar.orientation = .horizontal
        monitorToolbar.alignment = .centerY
        monitorBox.content.addArrangedSubview(monitorToolbar)
        monitorToolbar.widthAnchor.constraint(equalTo: monitorBox.content.widthAnchor).isActive = true

        let monitorHelp = NSTextField(labelWithString: "Historique des mesures et des commandes")
        monitorHelp.textColor = .secondaryLabelColor
        monitorToolbar.addArrangedSubview(monitorHelp)
        monitorHelp.setContentHuggingPriority(.defaultLow, for: .horizontal)

        let clearButton = NSButton(
            title: "Effacer",
            target: self,
            action: #selector(clearMonitor)
        )
        monitorToolbar.addArrangedSubview(clearButton)

        let scroll = NSScrollView()
        scroll.hasVerticalScroller = true
        scroll.borderType = .bezelBorder
        logView = NSTextView()
        logView.isEditable = false
        logView.font = .monospacedSystemFont(ofSize: 12, weight: .regular)
        logView.drawsBackground = true
        logView.backgroundColor = .textBackgroundColor
        logView.textColor = .textColor
        logView.insertionPointColor = .textColor
        logView.string = "En attente de données…\n"
        scroll.documentView = logView
        monitorBox.content.addArrangedSubview(scroll)
        scroll.heightAnchor.constraint(greaterThanOrEqualToConstant: 180).isActive = true
        scroll.widthAnchor.constraint(equalTo: monitorBox.content.widthAnchor).isActive = true

        let commandRow = NSStackView()
        commandRow.orientation = .horizontal
        commandRow.alignment = .centerY
        commandRow.spacing = 8
        monitorBox.content.addArrangedSubview(commandRow)

        commandField = NSTextField()
        commandField.placeholderString = "Commande de test, par exemple Q"
        commandField.stringValue = UserDefaults.standard.string(forKey: "lastCommand") ?? ""
        commandField.target = self
        commandField.action = #selector(sendCommand)
        commandRow.addArrangedSubview(commandField)
        commandField.setContentHuggingPriority(.defaultLow, for: .horizontal)

        sendEndingPopup = NSPopUpButton()
        sendEndingPopup.addItems(withTitles: ["CRLF", "CR", "LF", "Aucune"])
        sendEndingPopup.target = self
        sendEndingPopup.action = #selector(savePreferences)
        sendEndingPopup.selectItem(
            at: max(0, min(UserDefaults.standard.integer(forKey: "sendEnding"), 3))
        )
        commandRow.addArrangedSubview(sendEndingPopup)

        let sendButton = NSButton(title: "Envoyer", target: self, action: #selector(sendCommand))
        commandRow.addArrangedSubview(sendButton)
    }

    private func buildMetadataWindow() {
        metadataWindow = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 660, height: 510),
            styleMask: [.titled],
            backing: .buffered,
            defer: false
        )
        metadataWindow.title = "Informations jointes"
        metadataWindow.appearance = window.appearance

        let root = NSStackView()
        root.orientation = .vertical
        root.alignment = .leading
        root.spacing = 14
        root.edgeInsets = NSEdgeInsets()
        root.translatesAutoresizingMaskIntoConstraints = false
        metadataWindow.contentView = NSView()
        metadataWindow.contentView?.addSubview(root)
        NSLayoutConstraint.activate([
            root.leadingAnchor.constraint(equalTo: metadataWindow.contentView!.leadingAnchor, constant: 28),
            root.trailingAnchor.constraint(equalTo: metadataWindow.contentView!.trailingAnchor, constant: -28),
            root.topAnchor.constraint(equalTo: metadataWindow.contentView!.topAnchor, constant: 22),
            root.bottomAnchor.constraint(equalTo: metadataWindow.contentView!.bottomAnchor, constant: -22)
        ])

        let help = NSTextField(
            wrappingLabelWithString: "Cochez les informations à joindre à chaque mesure, puis choisissez leur ordre."
        )
        help.textColor = .secondaryLabelColor
        root.addArrangedSubview(help)

        metadataProfileLabel = NSTextField(labelWithString: "")
        metadataProfileLabel.font = .systemFont(ofSize: 12, weight: .semibold)
        root.addArrangedSubview(metadataProfileLabel)

        loadMetadataOrder()
        metadataRowsContainer = NSStackView()
        metadataRowsContainer.orientation = .vertical
        metadataRowsContainer.alignment = .leading
        metadataRowsContainer.spacing = 10
        root.addArrangedSubview(metadataRowsContainer)
        metadataRowsContainer.widthAnchor.constraint(equalTo: root.widthAnchor).isActive = true

        for definition in metadataDefinitions {
            let row = NSStackView()
            row.orientation = .horizontal
            row.alignment = .centerY
            row.spacing = 10
            metadataRowsContainer.addArrangedSubview(row)
            row.widthAnchor.constraint(equalTo: metadataRowsContainer.widthAnchor).isActive = true

            let checkbox = NSButton(
                checkboxWithTitle: definition.label,
                target: self,
                action: #selector(metadataChanged)
            )
            checkbox.state = UserDefaults.standard.bool(
                forKey: "metadata.\(definition.key).enabled"
            ) ? .on : .off
            checkbox.widthAnchor.constraint(equalToConstant: 140).isActive = true
            row.addArrangedSubview(checkbox)

            let field = NSTextField()
            field.stringValue = UserDefaults.standard.string(
                forKey: "metadata.\(definition.key).value"
            ) ?? ""
            field.delegate = self
            field.widthAnchor.constraint(equalToConstant: 300).isActive = true
            row.addArrangedSubview(field)

            let orderPopup = NSPopUpButton()
            orderPopup.addItems(withTitles: ["1", "2", "3", "4", "5", "6", "7"])
            orderPopup.identifier = NSUserInterfaceItemIdentifier(definition.key)
            orderPopup.target = self
            orderPopup.action = #selector(metadataOrderChanged(_:))
            orderPopup.selectItem(at: metadataOrder[definition.key] ?? 0)
            orderPopup.toolTip = "Position de cette information"
            orderPopup.widthAnchor.constraint(equalToConstant: 58).isActive = true
            row.addArrangedSubview(orderPopup)

            let trailingSpacer = NSView()
            trailingSpacer.setContentHuggingPriority(.defaultLow, for: .horizontal)
            row.addArrangedSubview(trailingSpacer)

            metadataCheckboxes[definition.key] = checkbox
            metadataFields[definition.key] = field
            metadataOrderPopups[definition.key] = orderPopup
            metadataRowViews[definition.key] = row
        }

        let measurementRow = NSStackView()
        measurementRow.orientation = .horizontal
        measurementRow.alignment = .centerY
        measurementRow.spacing = 10
        metadataRowsContainer.addArrangedSubview(measurementRow)
        measurementRow.widthAnchor.constraint(equalTo: metadataRowsContainer.widthAnchor).isActive = true

        let measurementCheckbox = NSButton(
            checkboxWithTitle: "Pesée",
            target: nil,
            action: nil
        )
        measurementCheckbox.state = .on
        measurementCheckbox.isEnabled = false
        measurementCheckbox.widthAnchor.constraint(equalToConstant: 140).isActive = true
        measurementRow.addArrangedSubview(measurementCheckbox)

        let measurementDescription = NSTextField(labelWithString: "Valeur brute de la balance")
        measurementDescription.textColor = .secondaryLabelColor
        measurementDescription.widthAnchor.constraint(equalToConstant: 300).isActive = true
        measurementRow.addArrangedSubview(measurementDescription)

        let measurementOrderPopup = NSPopUpButton()
        measurementOrderPopup.addItems(withTitles: ["1", "2", "3", "4", "5", "6", "7"])
        measurementOrderPopup.identifier = NSUserInterfaceItemIdentifier("measurement")
        measurementOrderPopup.target = self
        measurementOrderPopup.action = #selector(metadataOrderChanged(_:))
        measurementOrderPopup.selectItem(at: metadataOrder["measurement"] ?? 0)
        measurementOrderPopup.toolTip = "Position de la pesée"
        measurementOrderPopup.widthAnchor.constraint(equalToConstant: 58).isActive = true
        measurementRow.addArrangedSubview(measurementOrderPopup)

        let measurementSpacer = NSView()
        measurementSpacer.setContentHuggingPriority(.defaultLow, for: .horizontal)
        measurementRow.addArrangedSubview(measurementSpacer)

        metadataOrderPopups["measurement"] = measurementOrderPopup
        metadataRowViews["measurement"] = measurementRow
        reorderMetadataRows()

        let separatorRow = NSStackView()
        separatorRow.orientation = .horizontal
        separatorRow.alignment = .centerY
        separatorRow.spacing = 10
        root.addArrangedSubview(separatorRow)

        separatorRow.addArrangedSubview(NSTextField(labelWithString: "Séparateur :"))
        metadataSeparatorPopup = NSPopUpButton()
        metadataSeparatorPopup.addItems(withTitles: [
            "Tabulation", "Point-virgule ;", "Virgule ,", "Espace",
            "Retour à la ligne", "Barre verticale |", "Personnalisé"
        ])
        metadataSeparatorPopup.target = self
        metadataSeparatorPopup.action = #selector(separatorChanged)
        metadataSeparatorPopup.selectItem(
            at: max(0, min(UserDefaults.standard.integer(forKey: "metadataSeparator"), 6))
        )
        separatorRow.addArrangedSubview(metadataSeparatorPopup)

        customSeparatorField = NSTextField()
        customSeparatorField.placeholderString = "Séparateur"
        customSeparatorField.stringValue = UserDefaults.standard.string(
            forKey: "customMetadataSeparator"
        ) ?? ""
        customSeparatorField.delegate = self
        customSeparatorField.widthAnchor.constraint(equalToConstant: 120).isActive = true
        separatorRow.addArrangedSubview(customSeparatorField)
        updateCustomSeparatorVisibility()

        let previewLabel = NSTextField(labelWithString: "Aperçu transmis :")
        previewLabel.font = .systemFont(ofSize: 12, weight: .semibold)
        root.addArrangedSubview(previewLabel)

        metadataPreviewField = NSTextField(wrappingLabelWithString: "")
        metadataPreviewField.font = .monospacedSystemFont(ofSize: 12, weight: .regular)
        metadataPreviewField.drawsBackground = true
        metadataPreviewField.backgroundColor = .textBackgroundColor
        metadataPreviewField.textColor = .textColor
        metadataPreviewField.isBordered = true
        root.addArrangedSubview(metadataPreviewField)
        metadataPreviewField.widthAnchor.constraint(equalTo: root.widthAnchor).isActive = true
        metadataPreviewField.heightAnchor.constraint(greaterThanOrEqualToConstant: 42).isActive = true
        updateMetadataPreview()

        let actions = NSStackView()
        actions.orientation = .horizontal
        root.addArrangedSubview(actions)
        actions.widthAnchor.constraint(equalTo: root.widthAnchor).isActive = true

        actions.addArrangedSubview(
            NSButton(title: "Défaut", target: self, action: #selector(resetMetadataDefaults))
        )
        actions.addArrangedSubview(
            NSButton(title: "Sauvegarder…", target: self, action: #selector(saveMetadataProfile))
        )
        actions.addArrangedSubview(
            NSButton(title: "Charger…", target: self, action: #selector(loadMetadataProfile))
        )

        metadataFileLabel = NSTextField(labelWithString: "")
        metadataFileLabel.textColor = .secondaryLabelColor
        metadataFileLabel.font = .systemFont(ofSize: 11)
        metadataFileLabel.lineBreakMode = .byTruncatingMiddle
        metadataFileLabel.toolTip = "Fichier de profil actuellement chargé"
        metadataFileLabel.setContentHuggingPriority(.defaultLow, for: .horizontal)
        actions.addArrangedSubview(metadataFileLabel)

        let spacer = NSView()
        spacer.setContentHuggingPriority(.defaultLow, for: .horizontal)
        actions.addArrangedSubview(spacer)
        actions.addArrangedSubview(
            NSButton(title: "Terminer", target: self, action: #selector(closeMetadataWindow))
        )
    }

    @objc private func openMetadataWindow() {
        metadataWindow.appearance = window.appearance
        window.beginSheet(metadataWindow)
    }

    @objc private func closeMetadataWindow() {
        savePreferences()
        updateMetadataSummary()
        window.endSheet(metadataWindow)
    }

    @objc private func metadataChanged() {
        markMetadataProfileModified()
        savePreferences()
        updateMetadataSummary()
    }

    @objc private func resetMetadataDefaults() {
        for (index, definition) in metadataDefinitions.enumerated() {
            metadataCheckboxes[definition.key]?.state = .off
            metadataFields[definition.key]?.stringValue = ""
            metadataOrder[definition.key] = index + 1
            metadataOrderPopups[definition.key]?.selectItem(at: index + 1)
        }
        metadataOrder["measurement"] = 0
        metadataOrderPopups["measurement"]?.selectItem(at: 0)
        metadataSeparatorPopup.selectItem(at: 0)
        customSeparatorField.stringValue = ""
        activeMetadataProfileName = "Défaut"
        metadataProfileModified = false
        saveMetadataProfileState()
        updateCustomSeparatorVisibility()
        reorderMetadataRows()
        savePreferences()
        updateMetadataSummary()
    }

    @objc private func saveMetadataProfile() {
        savePreferences()
        let profile = MetadataProfile(
            version: 1,
            enabled: Dictionary(uniqueKeysWithValues: metadataDefinitions.map {
                ($0.key, metadataCheckboxes[$0.key]?.state == .on)
            }),
            values: Dictionary(uniqueKeysWithValues: metadataDefinitions.map {
                ($0.key, metadataFields[$0.key]?.stringValue ?? "")
            }),
            order: metadataOrder,
            separator: metadataSeparatorPopup.indexOfSelectedItem,
            customSeparator: customSeparatorField.stringValue
        )

        let panel = NSSavePanel()
        panel.title = "Sauvegarder le profil"
        panel.nameFieldStringValue = "Profil LabConnect.json"
        panel.allowedContentTypes = [.json]
        panel.canCreateDirectories = true
        panel.beginSheetModal(for: metadataWindow) { [weak self] response in
            guard response == .OK, let url = panel.url, let self else { return }
            do {
                let encoder = JSONEncoder()
                encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
                try encoder.encode(profile).write(to: url, options: .atomic)
                self.activeMetadataProfileName = url.lastPathComponent
                self.metadataProfileModified = false
                self.saveMetadataProfileState()
                self.updateMetadataSummary()
            } catch {
                self.showAlert(
                    title: "Sauvegarde impossible",
                    message: error.localizedDescription
                )
            }
        }
    }

    @objc private func loadMetadataProfile() {
        let panel = NSOpenPanel()
        panel.title = "Charger un profil"
        panel.allowedContentTypes = [.json]
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false
        panel.beginSheetModal(for: metadataWindow) { [weak self] response in
            guard response == .OK, let url = panel.url, let self else { return }
            do {
                let profile = try JSONDecoder().decode(
                    MetadataProfile.self,
                    from: Data(contentsOf: url)
                )
                try self.applyMetadataProfile(profile)
                self.activeMetadataProfileName = url.lastPathComponent
                self.metadataProfileModified = false
                self.saveMetadataProfileState()
                self.updateMetadataSummary()
            } catch {
                self.showAlert(
                    title: "Profil incompatible",
                    message: "Ce fichier n’est pas un profil LabConnect valide.\n\(error.localizedDescription)"
                )
            }
        }
    }

    private func applyMetadataProfile(_ profile: MetadataProfile) throws {
        let keys = Set(flowDefinitions.map(\.key))
        guard profile.version == 1,
              Set(profile.order.keys) == keys,
              Set(profile.order.values) == Set(0..<flowDefinitions.count),
              (0...6).contains(profile.separator) else {
            throw NSError(
                domain: "LabConnectKey.Profile",
                code: 1,
                userInfo: [NSLocalizedDescriptionKey: "Ordre ou format de profil incorrect."]
            )
        }

        for definition in metadataDefinitions {
            metadataCheckboxes[definition.key]?.state =
                profile.enabled[definition.key] == true ? .on : .off
            metadataFields[definition.key]?.stringValue =
                profile.values[definition.key] ?? ""
        }
        metadataOrder = profile.order
        for definition in flowDefinitions {
            metadataOrderPopups[definition.key]?.selectItem(
                at: profile.order[definition.key] ?? 0
            )
        }
        metadataSeparatorPopup.selectItem(at: profile.separator)
        customSeparatorField.stringValue = profile.customSeparator
        updateCustomSeparatorVisibility()
        reorderMetadataRows()
        savePreferences()
        updateMetadataSummary()
    }

    private func updateMetadataSummary() {
        guard metadataSummaryLabel != nil, metadataSeparatorPopup != nil else { return }
        let count = metadataDefinitions.filter {
            metadataCheckboxes[$0.key]?.state == .on &&
            !(metadataFields[$0.key]?.stringValue ?? "").isEmpty
        }.count
        let profileName = activeMetadataProfileName +
            (metadataProfileModified ? " — modifié" : "")
        metadataProfileLabel?.stringValue = "Profil actuel : \(profileName)"
        if activeMetadataProfileName == "Défaut" {
            metadataFileLabel?.stringValue = "Fichier : aucun · Défaut"
        } else {
            metadataFileLabel?.stringValue = "Fichier : \(profileName)"
        }
        if count == 0 {
            metadataSummaryLabel.stringValue =
                "Profil : \(profileName) · Informations jointes : aucune"
        } else {
            let suffix = count > 1 ? "informations" : "information"
            metadataSummaryLabel.stringValue =
                "Profil : \(profileName) · \(count) \(suffix) · \(metadataSeparatorPopup.titleOfSelectedItem ?? "")"
        }
        updateMetadataPreview()
    }

    private func updateMetadataPreview() {
        guard metadataPreviewField != nil else { return }
        let components = transmittedComponents(for: "ST,+000.0000 g")
        let previewSeparator: String
        switch metadataSeparatorPopup.indexOfSelectedItem {
        case 0: previewSeparator = "  ⇥  "
        case 4: previewSeparator = "\n"
        default: previewSeparator = metadataSeparator()
        }
        metadataPreviewField.stringValue = components.joined(separator: previewSeparator)
    }

    private func makeBox(title: String) -> (box: NSBox, content: NSStackView) {
        let box = NSBox()
        box.title = title
        box.titlePosition = .atTop
        box.boxType = .custom
        box.cornerRadius = 14
        box.borderWidth = 1
        box.borderColor = NSColor(
            name: NSColor.Name("LabConnectPanelBorder")
        ) { appearance in
            let isDark =
                appearance.bestMatch(from: [.darkAqua, .aqua]) == .darkAqua
            return isDark
                ? NSColor(calibratedRed: 0.20, green: 0.63, blue: 0.90, alpha: 0.25)
                : NSColor(calibratedRed: 0.20, green: 0.55, blue: 0.78, alpha: 0.24)
        }
        box.fillColor = NSColor(
            name: NSColor.Name("LabConnectPanelFill")
        ) { appearance in
            let isDark =
                appearance.bestMatch(from: [.darkAqua, .aqua]) == .darkAqua
            return isDark
                ? NSColor(calibratedWhite: 0.08, alpha: 0.76)
                : NSColor(calibratedRed: 0.96, green: 0.98, blue: 1.0, alpha: 0.82)
        }

        let content = NSStackView()
        content.orientation = .vertical
        content.alignment = .leading
        content.spacing = 10
        content.edgeInsets = NSEdgeInsets(top: 10, left: 12, bottom: 12, right: 12)
        content.translatesAutoresizingMaskIntoConstraints = false
        box.contentView = NSView()
        box.contentView?.addSubview(content)
        NSLayoutConstraint.activate([
            content.leadingAnchor.constraint(equalTo: box.contentView!.leadingAnchor),
            content.trailingAnchor.constraint(equalTo: box.contentView!.trailingAnchor),
            content.topAnchor.constraint(equalTo: box.contentView!.topAnchor),
            content.bottomAnchor.constraint(equalTo: box.contentView!.bottomAnchor)
        ])
        return (box, content)
    }

    private func configureSerialCallbacks() {
        serial.onLine = { [weak self] line in
            self?.received(line: line)
        }
        serial.onError = { [weak self] message in
            self?.appendLog("ERREUR  \(message)")
            self?.disconnect(status: message)
        }
    }

    private func configureBluetoothCallbacks() {
        bluetooth.onDevicesChanged = { [weak self] in
            guard let self, self.usesBluetoothMode else { return }
            self.portPopup.removeAllItems()
            if self.bluetooth.devices.isEmpty {
                self.portPopup.addItem(withTitle: "Aucun LabConnect détecté")
                self.connectButton.isEnabled = false
            } else {
                self.portPopup.addItems(withTitles: self.bluetooth.devices.map(\.name))
                if let savedIdentifier = UserDefaults.standard.string(forKey: "lastBleDevice"),
                   let savedIndex = self.bluetooth.devices.firstIndex(
                       where: { $0.peripheral.identifier.uuidString == savedIdentifier }
                   ) {
                    self.portPopup.selectItem(at: savedIndex)
                }
                self.connectButton.isEnabled = true
            }
        }
        bluetooth.onLine = { [weak self] line in
            self?.received(line: line)
        }
        bluetooth.onState = { [weak self] state, connected in
            guard let self else { return }
            self.statusLabel.stringValue = state
            self.statusLabel.textColor = connected ? .systemGreen : .secondaryLabelColor
            self.connectButton.title = connected ? "Déconnecter" : "Connecter"
            self.portPopup.isEnabled = !connected
            self.transportPopup.isEnabled = !connected
        }
        bluetooth.onError = { [weak self] message in
            self?.appendLog("ERREUR  \(message)")
            self?.showAlert(title: "Bluetooth", message: message)
        }
    }

    private var usesBluetoothMode: Bool {
        transportPopup?.indexOfSelectedItem == 0
    }

    @objc private func transportChanged() {
        savePreferences()
        serial.close()
        bluetooth.disconnect()
        connectButton.title = "Connecter"
        portPopup.isEnabled = true
        transportPopup.isEnabled = true
        refreshPorts()
    }

    @objc private func refreshPortsAction() {
        refreshPorts()
    }

    private func refreshPorts() {
        if usesBluetoothMode {
            portPopup.removeAllItems()
            portPopup.addItem(withTitle: "Recherche en cours…")
            connectButton.isEnabled = false
            bluetooth.scan()
            return
        }

        let selected = portPopup?.titleOfSelectedItem
        let directory = "/dev"
        let names = (try? FileManager.default.contentsOfDirectory(atPath: directory)) ?? []
        let ports = names
            .filter { $0.hasPrefix("cu.") && $0 != "cu.Bluetooth-Incoming-Port" }
            .map { "\(directory)/\($0)" }
            .sorted()

        portPopup.removeAllItems()
        if ports.isEmpty {
            portPopup.addItem(withTitle: "Aucun câble détecté")
            connectButton.isEnabled = false
        } else {
            portPopup.addItems(withTitles: ports)
            connectButton.isEnabled = true
            if let selected, ports.contains(selected) {
                portPopup.selectItem(withTitle: selected)
            } else if let saved = UserDefaults.standard.string(forKey: "lastPort"),
                      ports.contains(saved) {
                portPopup.selectItem(withTitle: saved)
            }
        }
    }

    @objc private func toggleConnection() {
        if usesBluetoothMode {
            if bluetooth.isConnected {
                bluetooth.disconnect()
                appendLog("DÉCONNEXION  Bluetooth")
            } else {
                let index = portPopup.indexOfSelectedItem
                if bluetooth.devices.indices.contains(index) {
                    UserDefaults.standard.set(
                        bluetooth.devices[index].peripheral.identifier.uuidString,
                        forKey: "lastBleDevice"
                    )
                }
                bluetooth.connect(deviceAt: index)
            }
            return
        }

        if serial.isOpen {
            disconnect(status: "Déconnecté")
            return
        }
        guard let path = portPopup.titleOfSelectedItem, path.hasPrefix("/dev/") else { return }
        do {
            try serial.open(path: path)
            UserDefaults.standard.set(path, forKey: "lastPort")
            connectButton.title = "Déconnecter"
            portPopup.isEnabled = false
            transportPopup.isEnabled = false
            statusLabel.stringValue = "Connecté par câble"
            statusLabel.textColor = .systemGreen
            appendLog("CONNEXION  \(path)")
        } catch {
            showAlert(title: "Connexion impossible", message: error.localizedDescription)
        }
    }

    private func disconnect(status: String) {
        serial.close()
        connectButton.title = "Connecter"
        portPopup.isEnabled = true
        transportPopup.isEnabled = true
        statusLabel.stringValue = status
        statusLabel.textColor = .secondaryLabelColor
    }

    private func received(line: String) {
        guard !line.isEmpty else { return }
        appendLog("REÇU       \(line)")

        lastLineLabel.stringValue = "Dernière mesure : \(line)"

        guard autoTypeCheckbox.state == .on else { return }
        guard AXIsProcessTrusted() else {
            autoTypeCheckbox.state = .off
            showAlert(
                title: "Autorisation nécessaire",
                message: "Activez LabConnect Key dans Réglages Système → Confidentialité et sécurité → Accessibilité."
            )
            return
        }
        if NSWorkspace.shared.frontmostApplication?.bundleIdentifier ==
            Bundle.main.bundleIdentifier {
            appendLog("NON SAISI  Sélectionnez la cellule de destination dans l’autre application")
            return
        }

        let components = transmittedComponents(for: line)
        typeComponents(components)
        let transmittedLine = components.joined(separator: metadataSeparator())
        let ending = EndingMode(rawValue: endingPopup.indexOfSelectedItem) ?? .enter
        if ending == .enter { pressKey(36) }
        if ending == .tab { pressKey(48) }
        appendLog("SAISI      \(transmittedLine)")
    }

    private func transmittedComponents(for measurement: String) -> [String] {
        let orderedDefinitions = flowDefinitions.sorted {
            (metadataOrder[$0.key] ?? 0) < (metadataOrder[$1.key] ?? 0)
        }
        return orderedDefinitions.compactMap { definition -> String? in
            if definition.key == "measurement" {
                return measurement
            }
            guard metadataCheckboxes[definition.key]?.state == .on else { return nil }
            let value = metadataFields[definition.key]?.stringValue ?? ""
            return value.isEmpty ? nil : value
        }
    }

    private func typeComponents(_ components: [String]) {
        for (index, component) in components.enumerated() {
            typeText(component)
            guard index < components.count - 1 else { continue }
            switch metadataSeparatorPopup.indexOfSelectedItem {
            case 0:
                pressKey(48) // Tabulation : cellule suivante.
            case 4:
                pressKey(36) // Entrée : ligne suivante.
            default:
                typeText(metadataSeparator())
            }
        }
    }

    private func metadataSeparator() -> String {
        switch metadataSeparatorPopup.indexOfSelectedItem {
        case 1: return ";"
        case 2: return ","
        case 3: return " "
        case 4: return "\n"
        case 5: return "|"
        case 6: return customSeparatorField.stringValue
        default: return "\t"
        }
    }

    private func loadMetadataOrder() {
        var available = Array(0..<flowDefinitions.count)
        let needsDefaultMigration =
            UserDefaults.standard.integer(forKey: "metadataOrderVersion") < 2
        for definition in flowDefinitions {
            let key = "metadata.\(definition.key).order"
            let defaultOrder: Int
            if definition.key == "measurement" {
                defaultOrder = 0
            } else {
                defaultOrder =
                    (metadataDefinitions.firstIndex(where: { $0.key == definition.key }) ?? 0) + 1
            }
            let saved = UserDefaults.standard.object(forKey: key) as? Int
            let requested = needsDefaultMigration ? defaultOrder : (saved ?? defaultOrder)
            let assigned = available.contains(requested) ? requested : (available.first ?? defaultOrder)
            metadataOrder[definition.key] = assigned
            available.removeAll { $0 == assigned }
            UserDefaults.standard.set(assigned, forKey: key)
        }
        UserDefaults.standard.set(2, forKey: "metadataOrderVersion")
    }

    @objc private func metadataOrderChanged(_ sender: NSPopUpButton) {
        guard let key = sender.identifier?.rawValue,
              let previousOrder = metadataOrder[key] else { return }
        let newOrder = sender.indexOfSelectedItem
        if let swappedKey = metadataOrder.first(
            where: { $0.key != key && $0.value == newOrder }
        )?.key {
            metadataOrder[swappedKey] = previousOrder
            metadataOrderPopups[swappedKey]?.selectItem(at: previousOrder)
        }
        metadataOrder[key] = newOrder
        reorderMetadataRows()
        markMetadataProfileModified()
        savePreferences()
        updateMetadataSummary()
    }

    @objc private func separatorChanged() {
        updateCustomSeparatorVisibility()
        markMetadataProfileModified()
        savePreferences()
        updateMetadataSummary()
    }

    private func updateCustomSeparatorVisibility() {
        customSeparatorField.isHidden = metadataSeparatorPopup.indexOfSelectedItem != 6
    }

    private func reorderMetadataRows() {
        guard metadataRowsContainer != nil else { return }
        let ordered = flowDefinitions.sorted {
            (metadataOrder[$0.key] ?? 0) < (metadataOrder[$1.key] ?? 0)
        }
        for definition in ordered {
            guard let row = metadataRowViews[definition.key] else { continue }
            metadataRowsContainer.removeArrangedSubview(row)
            row.removeFromSuperview()
            metadataRowsContainer.addArrangedSubview(row)
        }
    }

    private func typeText(_ text: String) {
        let utf16 = Array(text.utf16)
        guard !utf16.isEmpty,
              let down = CGEvent(keyboardEventSource: nil, virtualKey: 0, keyDown: true),
              let up = CGEvent(keyboardEventSource: nil, virtualKey: 0, keyDown: false) else {
            return
        }
        utf16.withUnsafeBufferPointer { buffer in
            guard let base = buffer.baseAddress else { return }
            down.keyboardSetUnicodeString(stringLength: utf16.count, unicodeString: base)
            up.keyboardSetUnicodeString(stringLength: utf16.count, unicodeString: base)
        }
        down.post(tap: .cghidEventTap)
        up.post(tap: .cghidEventTap)
    }

    private func pressKey(_ keyCode: CGKeyCode) {
        let down = CGEvent(keyboardEventSource: nil, virtualKey: keyCode, keyDown: true)
        let up = CGEvent(keyboardEventSource: nil, virtualKey: keyCode, keyDown: false)
        down?.post(tap: .cghidEventTap)
        up?.post(tap: .cghidEventTap)
    }

    @objc private func autoTypeChanged() {
        if autoTypeCheckbox.state == .off || AXIsProcessTrusted() {
            savePreferences()
            return
        }
        autoTypeCheckbox.state = .off
        savePreferences()
        accessibilityPromptShown = false
        showAccessibilitySetupIfNeeded(force: true)
    }

    private func requestAccessibilityPermission() {
        let key = kAXTrustedCheckOptionPrompt.takeUnretainedValue() as String
        let trusted = AXIsProcessTrustedWithOptions([key: true] as CFDictionary)
        if !trusted {
            appendLog("AUTORISATION  Activez LabConnect Key dans les réglages Accessibilité.")
        }
    }

    private func showAccessibilitySetupIfNeeded(force: Bool = false) {
        guard !AXIsProcessTrusted() else { return }
        guard force || autoTypeCheckbox.state == .on else { return }
        guard !accessibilityPromptShown else { return }
        accessibilityPromptShown = true

        let alert = NSAlert()
        alert.alertStyle = .informational
        alert.messageText = "Autoriser la saisie automatique"
        alert.informativeText =
            "Pour envoyer les mesures à l’emplacement du curseur dans Excel, Numbers ou une autre application, LabConnect Key doit être autorisé une seule fois dans les réglages de macOS."
        alert.addButton(withTitle: "Ouvrir les réglages")
        alert.addButton(withTitle: "Plus tard")
        if alert.runModal() == .alertFirstButtonReturn {
            requestAccessibilityPermission()
        }
    }

    @objc private func clearMonitor() {
        logView.string = "En attente de données…\n"
    }

    @objc private func savePreferences() {
        guard autoTypeCheckbox != nil else { return }
        UserDefaults.standard.set(transportPopup.indexOfSelectedItem, forKey: "transportMode")
        UserDefaults.standard.set(autoTypeCheckbox.state == .on, forKey: "autoTypeEnabled")
        UserDefaults.standard.set(endingPopup.indexOfSelectedItem, forKey: "endingMode")
        UserDefaults.standard.set(sendEndingPopup.indexOfSelectedItem, forKey: "sendEnding")
        UserDefaults.standard.set(
            metadataSeparatorPopup.indexOfSelectedItem,
            forKey: "metadataSeparator"
        )
        UserDefaults.standard.set(
            customSeparatorField.stringValue,
            forKey: "customMetadataSeparator"
        )
        for definition in metadataDefinitions {
            UserDefaults.standard.set(
                metadataCheckboxes[definition.key]?.state == .on,
                forKey: "metadata.\(definition.key).enabled"
            )
            UserDefaults.standard.set(
                metadataFields[definition.key]?.stringValue ?? "",
                forKey: "metadata.\(definition.key).value"
            )
            UserDefaults.standard.set(
                metadataOrder[definition.key] ?? 0,
                forKey: "metadata.\(definition.key).order"
            )
        }
        UserDefaults.standard.set(
            metadataOrder["measurement"] ?? 0,
            forKey: "metadata.measurement.order"
        )
    }

    func controlTextDidChange(_ notification: Notification) {
        markMetadataProfileModified()
        savePreferences()
        updateMetadataSummary()
    }

    private func markMetadataProfileModified() {
        metadataProfileModified = true
        saveMetadataProfileState()
    }

    private func saveMetadataProfileState() {
        UserDefaults.standard.set(
            activeMetadataProfileName,
            forKey: "activeMetadataProfileName"
        )
        UserDefaults.standard.set(
            metadataProfileModified,
            forKey: "metadataProfileModified"
        )
    }

    @objc private func themeChanged() {
        UserDefaults.standard.set(themePopup.indexOfSelectedItem, forKey: "themeMode")
        applyTheme()
    }

    private func applyTheme() {
        guard themePopup != nil else { return }
        switch themePopup.indexOfSelectedItem {
        case 1:
            window.appearance = NSAppearance(named: .aqua)
        case 2:
            window.appearance = NSAppearance(named: .darkAqua)
        default:
            window.appearance = nil
        }
        metadataWindow?.appearance = window.appearance
    }

    @objc private func sendCommand() {
        guard !commandField.stringValue.isEmpty else { return }
        UserDefaults.standard.set(commandField.stringValue, forKey: "lastCommand")
        savePreferences()
        let endings = ["\r\n", "\r", "\n", ""]
        let ending = endings[max(0, min(sendEndingPopup.indexOfSelectedItem, endings.count - 1))]
        do {
            if usesBluetoothMode {
                try bluetooth.send(commandField.stringValue, ending: ending)
            } else {
                try serial.send(commandField.stringValue, ending: ending)
            }
            appendLog("ENVOYÉ     \(commandField.stringValue)")
            window.makeFirstResponder(nil)
        } catch {
            showAlert(title: "Envoi impossible", message: error.localizedDescription)
        }
    }

    private func appendLog(_ line: String) {
        if logView.string == "En attente de données…\n" {
            logView.string = ""
        }
        let formatter = DateFormatter()
        formatter.dateFormat = "HH:mm:ss.SSS"
        logView.textStorage?.append(
            NSAttributedString(
                string: "\(formatter.string(from: Date()))  \(line)\n",
                attributes: [
                    .foregroundColor: NSColor.textColor,
                    .font: NSFont.monospacedSystemFont(ofSize: 12, weight: .regular)
                ]
            )
        )
        logView.scrollToEndOfDocument(nil)

        let lines = logView.string.split(separator: "\n", omittingEmptySubsequences: false)
        if lines.count > 1000 {
            logView.string = lines.suffix(700).joined(separator: "\n")
        }
    }

    private func showAlert(title: String, message: String) {
        let alert = NSAlert()
        alert.messageText = title
        alert.informativeText = message
        alert.alertStyle = .warning
        alert.runModal()
    }
}

@main
private struct LabConnectKeyApplication {
    static func main() {
        let application = NSApplication.shared
        let delegate = AppDelegate()
        application.delegate = delegate
        application.setActivationPolicy(.regular)
        application.run()
    }
}
