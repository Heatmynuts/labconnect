import { type ReactNode, useEffect, useMemo, useRef, useState } from "react";
import { ArrowLeft, Check, ChevronRight, Download, FileText, History, Loader2, Pencil, Plus, Printer, Radar, ReceiptText, RotateCcw, Save, Scale, ScanLine, Search, Settings2, Trash2, Wifi, X, Zap } from "lucide-react";
import { buildTicketQrPayload, parseTicketQrPayload, ticketQrToRecord, type TicketField, type TicketLine, type WeighingRecord, type WeighingTicketTemplate } from "@labconnect/shared-types";
import { Button, PrintTicketDesigner, TicketPreview } from "@labconnect/ui";
import andLogo from "./assets/brands/and.png";
import bizerbaLogo from "./assets/brands/bizerba.jpg";
import diniLogo from "./assets/brands/dini.png";
import kernLogo from "./assets/brands/kern.png";
import mettlerLogo from "./assets/brands/mettler.png";
import ohausLogo from "./assets/brands/ohaus.png";
import preciaMolenLogo from "./assets/brands/precia-molen.png";
import precisaLogo from "./assets/brands/precisa.png";
import sartoriusLogo from "./assets/brands/sartorius.png";
import shimadzuLogo from "./assets/brands/shimadzu.png";
import { atomS3Bridge, equipment, lastWeighingRecord, rawBalanceLines, standardTicketTemplate, ticketTemplates, weighingHistory } from "./data/demo";

type View = "weighing" | "ticket" | "scan" | "history";
type AppMode = "main" | "ticket-scan";
type SettingsPanel = "weighing" | "ticket" | "scan";
type BalanceAction = "tare" | "clear-tare" | "zero" | "print" | "request-weight";
type TerminalFormat = "auto" | "v3mix" | "v3";
type SetupStep = "intro" | "scan" | "select";
type ConnectionState = "connected" | "connecting" | "offline";
type AppLanguage = "fr" | "en";
type BalanceBrandId = "and" | "mettler" | "ohaus" | "precia-molen" | "precisa" | "sartorius" | "shimadzu" | "kern" | "bizerba" | "dini";
type BalanceBrand = {
  id: BalanceBrandId;
  name: string;
  logo: string;
};
type DetectedDevice = {
  id: string;
  name: string;
  model: string;
  serialNumber: string;
  deviceId: string;
  brandId?: BalanceBrandId;
  brandName?: string;
  brandLogo?: string;
  photo?: string;
  transport: string;
  ipAddress: string;
  serial: string;
};
const MAX_STREAM_LINES = 4;
const MAX_BALANCE_LINE_CHARS = 48;
const BALANCE_STALE_MS = 3000;
type WeighingSettings = {
  printPlusSign: boolean;
  decimalSeparator: "." | ",";
  maskStart: number;
  maskEnd: number;
  trimIntegerLeadingZeros: boolean;
};
type ScanSettings = {
  manualInput: boolean;
  autoSaveScans: boolean;
  autoPrintScans: boolean;
  activationSeconds: number;
};
const defaultWeighingSettings: WeighingSettings = {
  printPlusSign: true,
  decimalSeparator: ".",
  maskStart: 0,
  maskEnd: 0,
  trimIntegerLeadingZeros: false
};
const defaultScanSettings: ScanSettings = {
  manualInput: true,
  autoSaveScans: false,
  autoPrintScans: false,
  activationSeconds: 10
};
const SIMULATED_DEVICE: DetectedDevice = {
  id: "sim-and-fz5000i",
  name: "A&D FZ-5000i",
  model: "FZ-5000i",
  serialNumber: "15910862",
  deviceId: "0000000",
  brandId: "and",
  brandName: "A&D",
  brandLogo: andLogo,
  transport: "simulation",
  ipAddress: "",
  serial: ""
};

const balanceBrands: BalanceBrand[] = [
  { id: "and", name: "A&D", logo: andLogo },
  { id: "mettler", name: "Mettler Toledo", logo: mettlerLogo },
  { id: "ohaus", name: "Ohaus", logo: ohausLogo },
  { id: "precia-molen", name: "Precia Molen", logo: preciaMolenLogo },
  { id: "precisa", name: "Precisa", logo: precisaLogo },
  { id: "sartorius", name: "Sartorius", logo: sartoriusLogo },
  { id: "shimadzu", name: "Shimadzu", logo: shimadzuLogo },
  { id: "kern", name: "Kern", logo: kernLogo },
  { id: "bizerba", name: "Bizerba", logo: bizerbaLogo },
  { id: "dini", name: "Dini Argeo", logo: diniLogo }
];

declare global {
  interface Window {
    LabConnectSunmiPrinter?: {
      printTicket: (payload: string) => string;
    };
    LabConnectScanner?: {
      startScan: (timeoutMs: number) => string;
      stopScan: () => string;
    };
    LabConnectDevice?: {
      model: string;
    };
  }

  interface WindowEventMap {
    "labconnect-integrated-scan": CustomEvent<{ data: string; source: "integrated-scanner" }>;
  }
}

const balanceCommands: Record<BalanceAction, string> = {
  tare: "tare",
  "clear-tare": "clear-tare",
  zero: "zero",
  "request-weight": "request-weight",
  print: "print"
};

const appCopy = {
  fr: {
    actionPrompt: "Choisir une action",
    active: "Actif",
    addBalance: "Nouvelle balance",
    add: "Ajouter",
    backHome: "Accueil",
    balance: "Balance",
    balanceAssociated: "Balance associée",
    balanceConnected: "Balance connectée",
    chooseManufacturer: "Fabricant",
    comment: "Commentaire",
    connecting: "Connexion",
    manageBalances: "Gérer les balances",
    newBadge: "Nouveau",
    noData: "Aucune donnée",
    noWeight: "Aucune pesée reçue de la balance.",
    offline: "Hors ligne",
    operator: "Opérateur",
    print: "Imprimer",
    ready: "Prêt",
    readyFallback: "Prête",
    readingBadge: "Lecture",
    scanTicket: "Scanner ticket",
    serial: "Série",
    serialNumber: "Numéro de série",
    simulation: "Simulation",
    sample: "Échantillon",
    tare: "Tare",
    ticket: "Ticket",
    weighing: "Pesée",
    zero: "Zéro"
  },
  en: {
    actionPrompt: "Choose an action",
    active: "Active",
    addBalance: "New scale",
    add: "Add",
    backHome: "Home",
    balance: "Scale",
    balanceAssociated: "Scale paired",
    balanceConnected: "Scale connected",
    chooseManufacturer: "Manufacturer",
    comment: "Comment",
    connecting: "Connecting",
    manageBalances: "Manage scales",
    newBadge: "New",
    noData: "No data",
    noWeight: "No weight received from the scale.",
    offline: "Offline",
    operator: "Operator",
    print: "Print",
    ready: "Ready",
    readyFallback: "Ready",
    readingBadge: "Read",
    scanTicket: "Scan ticket",
    serial: "Serial",
    serialNumber: "Serial number",
    simulation: "Simulation",
    sample: "Sample",
    tare: "Tare",
    ticket: "Ticket",
    weighing: "Weighing",
    zero: "Zero"
  }
} satisfies Record<AppLanguage, Record<string, string>>;

export function App() {
  const [operator, setOperator] = useLocalState("labconnect.operator", "Opérateur 01");
  const [lotNumber, setLotNumber] = useLocalState("labconnect.lot", "LC-2048");
  const [sampleId, setSampleId] = useLocalState("labconnect.sample", "ECH-001");
  const [comment, setComment] = useLocalState("labconnect.comment", "");
  const [language, setLanguage] = useLocalState("labconnect.language", "fr");
  const [terminalFormat, setTerminalFormat] = useLocalState("labconnect.terminal-format", "auto");
  const layout = useTerminalLayout(terminalFormat as TerminalFormat);
  const [ticketTemplate, setTicketTemplate] = useLocalObject<WeighingTicketTemplate>("labconnect.ticket-template", standardTicketTemplate);
  const [history, setHistory] = useLocalObject<WeighingRecord[]>("labconnect.weighing-history", weighingHistory);
  const [savedDevice, setSavedDevice] = useLocalObject<DetectedDevice | null>("labconnect.selected-device", null);
  const [savedDevices, setSavedDevices] = useLocalObject<DetectedDevice[]>("labconnect.saved-devices", savedDevice ? [savedDevice] : []);
  const [weighingSettings, setWeighingSettings] = useLocalObject<WeighingSettings>("labconnect.weighing-settings", defaultWeighingSettings);
  const [scanSettings, setScanSettings] = useLocalObject<ScanSettings>("labconnect.scan-settings", defaultScanSettings);
  const [autoAssociate, setAutoAssociate] = useLocalBoolean("labconnect.auto-associate", false);
  const [lines, setLines] = useState<string[]>([]);
  const [lastBalanceFrameAt, setLastBalanceFrameAt] = useState(0);
  const [now, setNow] = useState(() => Date.now());
  const [tareValue, setTareValue] = useState("");
  const [appMode, setAppMode] = useState<AppMode>("main");
  const [view, setView] = useState<View>("weighing");
  const [settingsPanel, setSettingsPanel] = useState<SettingsPanel | null>(null);
  const [setupStep, setSetupStep] = useState<SetupStep>("intro");
  const [selectedDevice, setSelectedDevice] = useState<DetectedDevice | null>(null);
  const [detectedDevice, setDetectedDevice] = useState<DetectedDevice | null>(savedDevice);
  const [connectionState, setConnectionState] = useState<ConnectionState>("connecting");
  const [isSimulation, setIsSimulation] = useState(false);
  const [printStatus, setPrintStatus] = useState("Prêt à imprimer");
  const [scannedRecord, setScannedRecord] = useState<WeighingRecord | null>(null);
  const socketRef = useRef<WebSocket | null>(null);
  const simulatedWeightRef = useRef(1248.52);
  const appLanguage: AppLanguage = language === "en" ? "en" : "fr";
  const copy = appCopy[appLanguage];

  const atomS3Url = `ws://${atomS3Bridge.ipAddress}:${atomS3Bridge.port}/ws`;
  const isBalanceLive = selectedDevice ? now - lastBalanceFrameAt < BALANCE_STALE_MS : false;
  const displayLines = useMemo(() => isBalanceLive ? lines.map((line) => formatBalanceFrame(line, weighingSettings)) : [], [isBalanceLive, lines, weighingSettings]);

  useEffect(() => {
    const timer = window.setInterval(() => setNow(Date.now()), 500);
    return () => window.clearInterval(timer);
  }, []);

  useEffect(() => {
    if (ticketTemplate.fields.includes("comment")) return;
    setTicketTemplate(addCommentToTicketTemplate(ticketTemplate));
  }, [ticketTemplate.id]);

  useEffect(() => {
    if (isSimulation) {
      socketRef.current?.close();
      socketRef.current = null;
      setConnectionState("connected");
      return;
    }

    let isDisposed = false;
    let reconnectTimer: number | undefined;

    const connect = () => {
      if (isDisposed) return;
      setConnectionState("connecting");
      const socket = new WebSocket(atomS3Url);
      socketRef.current = socket;

      socket.onopen = () => {
        setConnectionState("connected");
        requestDeviceInfo(socket);
      };
      socket.onmessage = (event) => {
        const device = parseDeviceMessage(event.data);
        if (device) {
          setDetectedDevice((current) => mergeDetectedDevice(current, device));
          if (setupStep === "scan") {
            window.setTimeout(() => setSetupStep("select"), 700);
          }
          return;
        }
        const nextLines = normalizeBalanceMessage(event.data);
        if (nextLines.length === 0) return;
        setLastBalanceFrameAt(Date.now());
        setLines((current) => [...nextLines, ...current].slice(0, MAX_STREAM_LINES));
      };
      socket.onerror = () => setConnectionState("offline");
      socket.onclose = () => {
        if (socketRef.current === socket) {
          socketRef.current = null;
        }
        setConnectionState("offline");
        if (!isDisposed) {
          reconnectTimer = window.setTimeout(connect, 2000);
        }
      };
    };

    connect();

    return () => {
      isDisposed = true;
      if (reconnectTimer) {
        window.clearTimeout(reconnectTimer);
      }
      socketRef.current?.close();
      socketRef.current = null;
    };
  }, [atomS3Url, autoAssociate, isSimulation, savedDevice?.id, setupStep]);

  useEffect(() => {
    if (!savedDevice) return;
    setSavedDevices(upsertDevice(savedDevices, savedDevice));
  }, [savedDevice?.id]);

  useEffect(() => {
    if (setupStep !== "scan") return;
    requestDeviceInfo(socketRef.current);
    if (connectionState !== "connected") return;
    sendBalanceCommand("request-weight");
    const timer = window.setTimeout(() => {
      setDetectedDevice((current) => current ?? createFallbackDevice());
      setSetupStep("select");
    }, 2200);
    return () => window.clearTimeout(timer);
  }, [connectionState, setupStep]);

  useEffect(() => {
    if (selectedDevice || setupStep === "intro") return;
    requestDeviceInfo(socketRef.current);
    const timer = window.setInterval(() => requestDeviceInfo(socketRef.current), 1400);
    return () => window.clearInterval(timer);
  }, [selectedDevice, setupStep]);

  useEffect(() => {
    if (!isSimulation || !selectedDevice) return;
    const emitWeight = () => {
      simulatedWeightRef.current += (Math.random() - 0.47) * 0.08;
      setLastBalanceFrameAt(Date.now());
      setLines((current) => [formatSimulatedWeight(simulatedWeightRef.current), ...current].slice(0, MAX_STREAM_LINES));
    };
    emitWeight();
    const timer = window.setInterval(emitWeight, 900);
    return () => window.clearInterval(timer);
  }, [isSimulation, selectedDevice]);

  useEffect(() => {
    if (!selectedDevice || appMode !== "main") return;
    window.history.pushState({ labconnectView: "weighing-session" }, "");
    const handleBack = () => {
      setSelectedDevice(null);
      setSetupStep("intro");
      setIsSimulation(false);
    };
    window.addEventListener("popstate", handleBack);
    return () => window.removeEventListener("popstate", handleBack);
  }, [appMode, selectedDevice?.id]);

  useEffect(() => {
    if (!selectedDevice) return;
    if (view !== "weighing" && view !== "ticket") {
      setView("weighing");
    }
  }, [selectedDevice, view]);

  const currentRecord = useMemo<WeighingRecord>(() => {
    const rawBalanceLine = isBalanceLive ? lines[0] ?? "" : "";
    const displayFrame = rawBalanceLine ? formatBalanceFrame(rawBalanceLine, weighingSettings) : "";
    const liveWeight = displayFrame ? parseWeightForTicket(displayFrame) : "";
    const netMeasurement = parseWeightMeasurement(liveWeight);
    const tareMeasurement = parseWeightMeasurement(tareValue);
    const grossWeight = netMeasurement ? formatWeightMeasurement(netMeasurement.value + (tareMeasurement?.value ?? 0), netMeasurement.unit, Math.max(netMeasurement.decimals, tareMeasurement?.decimals ?? 0), netMeasurement.separator) : "";
    return {
      ...lastWeighingRecord,
      id: history[0]?.id ?? lastWeighingRecord.id,
      dateTime: formatDateTime(new Date()),
      operator,
      balanceName: selectedDevice?.name || selectedDevice?.model || lastWeighingRecord.balanceName,
      lotNumber,
      sampleId,
      comment,
      rawBalanceLine: displayFrame || "Aucune pesée",
      weight: displayFrame || "Aucune pesée",
      grossWeight: grossWeight || liveWeight || "-",
      tareWeight: tareValue || "0 g",
      netWeight: liveWeight || "-",
      commandResponses: {
        "?ID": selectedDevice?.deviceId || SIMULATED_DEVICE.deviceId,
        "?SN": selectedDevice?.serialNumber || SIMULATED_DEVICE.serialNumber,
        "?TN": selectedDevice?.model || SIMULATED_DEVICE.model
      }
    };
  }, [comment, history, isBalanceLive, lines, lotNumber, operator, sampleId, selectedDevice, tareValue, weighingSettings]);

  const captureCurrentLine = () => {
    if (!isBalanceLive) return;
    const record: WeighingRecord = {
      ...currentRecord,
      id: `w-${Date.now()}`
    };
    persistHistory([record, ...history]);
    setHistory([record, ...history]);
  };

  const sendBalanceCommand = (command: BalanceAction) => {
    if (isSimulation) {
      if (command === "zero" || command === "tare") {
        if (command === "tare" && isBalanceLive) {
          setTareValue(parseWeightForTicket(formatBalanceFrame(lines[0] ?? "", weighingSettings)));
        }
        simulatedWeightRef.current = 0;
        setLastBalanceFrameAt(Date.now());
        setLines((current) => [formatSimulatedWeight(0), ...current].slice(0, MAX_STREAM_LINES));
        setPrintStatus(command === "tare" ? "Tare simulée" : "Zéro simulé");
        return;
      }
      if (command === "clear-tare") {
        setTareValue("");
        setPrintStatus("Tare effacée");
        return;
      }
      if (command === "request-weight") {
        setLastBalanceFrameAt(Date.now());
        setLines((current) => [formatSimulatedWeight(simulatedWeightRef.current), ...current].slice(0, MAX_STREAM_LINES));
      }
      return;
    }

    if (command === "tare" && isBalanceLive) {
      setTareValue(parseWeightForTicket(formatBalanceFrame(lines[0] ?? "", weighingSettings)));
    }
    if (command === "clear-tare") {
      setTareValue("");
    }
    const socket = socketRef.current;
    if (socket?.readyState === WebSocket.OPEN) {
      socket.send(JSON.stringify({ type: "command", command: balanceCommands[command] }));
    }
  };

  const printRecordTicket = (record: WeighingRecord) => {
    const payload = JSON.stringify({ record, template: ticketTemplate });
    if (window.LabConnectSunmiPrinter?.printTicket) {
      try {
        setPrintStatus("Envoi à l'imprimante...");
        const result = JSON.parse(window.LabConnectSunmiPrinter.printTicket(payload)) as { ok: boolean; error?: string };
        if (!result.ok) {
          setPrintStatus(result.error ?? "Erreur imprimante LabConnect");
          return;
        }
        setPrintStatus("Ticket envoyé à l'imprimante");
      } catch (error) {
        setPrintStatus(error instanceof Error ? error.message : "Erreur imprimante LabConnect");
      }
      return;
    }
    window.print();
    setPrintStatus("Impression navigateur lancée");
  };

  const printCurrentTicket = () => {
    if (!isBalanceLive) {
      setPrintStatus("Aucune pesée reçue");
      return;
    }
    captureCurrentLine();
    printRecordTicket(currentRecord);
  };

  const saveDetectedDevice = (device: DetectedDevice) => {
    const nextDevices = upsertDevice(savedDevices, device);
    setSavedDevices(nextDevices);
    setSavedDevice(autoAssociate && !isSimulation ? device : null);
    setDetectedDevice(device);
    setSelectedDevice(null);
    setSetupStep("intro");
    setPrintStatus("Balance enregistrée");
  };

  const connectSavedDevice = (device: DetectedDevice) => {
    setIsSimulation(device.transport === SIMULATED_DEVICE.transport);
    setDetectedDevice(device);
    setSelectedDevice(device);
    setSetupStep("select");
    setView("weighing");
  };

  const updateSavedDevice = (updatedDevice: DetectedDevice) => {
    const nextDevices = savedDevices.map((device) => (device.id === updatedDevice.id ? updatedDevice : device));
    setSavedDevices(nextDevices);
    if (savedDevice?.id === updatedDevice.id) {
      setSavedDevice(updatedDevice);
    }
    if (selectedDevice?.id === updatedDevice.id) {
      setSelectedDevice(updatedDevice);
    }
  };

  const deleteSavedDevice = (deviceId: string) => {
    const nextDevices = savedDevices.filter((device) => device.id !== deviceId);
    setSavedDevices(nextDevices);
    if (savedDevice?.id === deviceId) {
      setSavedDevice(null);
      setAutoAssociate(false);
    }
    if (selectedDevice?.id === deviceId) {
      setSelectedDevice(null);
      setSetupStep("intro");
    }
  };

  const startSimulation = () => {
    setIsSimulation(true);
    setDetectedDevice(SIMULATED_DEVICE);
    setLastBalanceFrameAt(Date.now());
    setLines([formatSimulatedWeight(simulatedWeightRef.current), ...rawBalanceLines].slice(0, MAX_STREAM_LINES));
    window.setTimeout(() => setSetupStep("select"), 260);
  };

  const resetDeviceSelection = () => {
    setSelectedDevice(null);
    setSetupStep("intro");
    setIsSimulation(false);
  };

  const exportCsv = () => {
    const header = ["id", "dateTime", "operator", "lotNumber", "sampleId", "rawBalanceLine"];
    const rows = history.map((record) => header.map((key) => csvCell(String(record[key as keyof WeighingRecord] ?? ""))).join(";"));
    downloadFile("labconnect-pesees.csv", [header.join(";"), ...rows].join("\n"), "text/csv;charset=utf-8");
  };

  const exportPdf = () => {
    const html = buildPrintableHistory(history, ticketTemplate);
    const popup = window.open("", "_blank", "width=720,height=900");
    if (!popup) return;
    popup.document.write(html);
    popup.document.close();
    popup.focus();
    popup.print();
  };

  const exportRecordPdf = (record: WeighingRecord) => {
    const html = buildPrintableTicket(record, ticketTemplate);
    const popup = window.open("", "_blank", "width=420,height=720");
    if (!popup) return;
    popup.document.write(html);
    popup.document.close();
    popup.focus();
    popup.print();
  };

  const saveScannedRecord = (record: WeighingRecord) => {
    const exists = history.some((item) => item.id === record.id);
    const nextHistory = exists ? history : [record, ...history];
    persistHistory(nextHistory);
    setHistory(nextHistory);
    setPrintStatus(exists ? "Ticket déjà présent dans l’historique" : "Ticket sauvegardé dans l’historique");
  };

  const useScannedRecord = (record: WeighingRecord) => {
    setLines([record.rawBalanceLine || record.weight, ...lines].slice(0, MAX_STREAM_LINES));
    setOperator(record.operator || operator);
    setLotNumber(record.lotNumber || lotNumber);
    setSampleId(record.sampleId || sampleId);
    setComment(record.comment || comment);
    setAppMode("main");
    setView("weighing");
  };

  const handleScannedRecord = (record: WeighingRecord) => {
    setScannedRecord(record);
    if (scanSettings.autoSaveScans) {
      saveScannedRecord(record);
    }
    if (scanSettings.autoPrintScans) {
      printRecordTicket(record);
    }
  };

  const openStandaloneScan = () => {
    setAppMode("ticket-scan");
    setView("scan");
  };

  return (
    <main className="min-h-screen bg-background px-3 pb-5 pt-4 text-slate-950 sm:px-4 lg:px-5">
      {appMode === "ticket-scan" ? (
        <div className="w-full animate-lab-screen">
          <header className="flex items-center justify-between gap-4">
            <button className="inline-flex min-h-11 items-center gap-2 rounded-full bg-surface-soft px-4 text-sm font-bold text-slate-700 transition active:scale-[0.98]" type="button" onClick={() => setAppMode("main")}>
              <ArrowLeft className="h-4 w-4" />
              Retour
            </button>
            <div className="text-center">
              <p className="text-sm font-semibold text-slate-500">LabConnect Print</p>
              <h1 className="text-[24px] font-bold leading-8 tracking-normal">Scan ticket</h1>
            </div>
            <Button variant="secondary" size="icon" aria-label="Options scan" onClick={() => setSettingsPanel("scan")}>
              <Settings2 className="h-5 w-5" />
            </Button>
          </header>
          <section className="mx-auto mt-5 max-w-5xl">
            <ScanTicketView
              currentRecord={currentRecord}
              onClear={() => setScannedRecord(null)}
              onPdf={exportRecordPdf}
              onPrint={printRecordTicket}
              onSave={saveScannedRecord}
              onScan={handleScannedRecord}
              onUseRecord={useScannedRecord}
              onOpenSettings={() => setSettingsPanel("scan")}
              record={scannedRecord}
              settings={scanSettings}
              ticketTemplate={ticketTemplate}
            />
          </section>
        </div>
      ) : (
        <>
          {!selectedDevice && (
            <SetupExperience
              autoAssociate={autoAssociate}
              connectionState={connectionState}
              device={detectedDevice}
              isSimulation={isSimulation}
              onAutoAssociateChange={setAutoAssociate}
              onConnectSavedDevice={connectSavedDevice}
              onDeleteSavedDevice={deleteSavedDevice}
              language={appLanguage}
              onLanguageChange={(nextLanguage) => setLanguage(nextLanguage)}
              onOpenTicketScan={openStandaloneScan}
              onSaveDevice={saveDetectedDevice}
              onStartScan={() => setSetupStep("scan")}
              onStartSimulation={startSimulation}
              savedDevices={savedDevices}
              step={setupStep}
              terminalFormat={terminalFormat as TerminalFormat}
              detectedFormat={layout.format}
              onTerminalFormatChange={(f) => setTerminalFormat(f)}
              onUpdateSavedDevice={updateSavedDevice}
            />
          )}
          <div className="w-full">
            <header className="flex items-center justify-between gap-4">
              {selectedDevice ? (
                <button className="inline-flex min-h-11 items-center gap-2 rounded-full bg-surface-soft px-4 text-sm font-bold text-slate-700 transition active:scale-[0.98]" type="button" onClick={resetDeviceSelection}>
                  <ArrowLeft className="h-4 w-4" />
                  {copy.backHome}
                </button>
              ) : (
                <div>
                  <h1 className="text-[30px] font-bold leading-9 tracking-normal">LabConnect Print</h1>
                </div>
              )}
              {selectedDevice && (
                <div className="min-w-0 flex-1 text-center">
                  <h1 className="truncate text-[24px] font-bold leading-8 tracking-normal">LabConnect Print</h1>
                </div>
              )}
              <div className="flex items-center gap-2">
                <LanguageToggle language={appLanguage} onChange={(nextLanguage) => setLanguage(nextLanguage)} />
                <ConnectionPill copy={copy} isSimulation={isSimulation} state={connectionState} />
              </div>
            </header>

            {selectedDevice ? (
              <div className={`mt-4 grid gap-4 ${layout.isPortrait ? "" : "lg:grid-cols-[92px_minmax(360px,1fr)_minmax(340px,0.86fr)]"}`}>
                {!layout.isPortrait && (
                <nav className="grid grid-cols-1 gap-2 rounded-2xl border border-white/70 bg-white/60 p-1 shadow-card backdrop-blur lg:sticky lg:top-4 lg:self-start">
                  <SessionTabButton active={view === "weighing"} icon={Scale} label={copy.weighing} onClick={() => setView("weighing")} />
                </nav>
                )}

                <section className={`min-w-0 ${layout.isPortrait ? "order-2" : ""}`}>
                    <div className="rounded-lg border border-white/80 bg-white/80 p-4 shadow-card backdrop-blur">
                      <div className="mb-3 flex items-center justify-between gap-3">
                        <div>
                          <h2 className="text-lg font-black">{copy.ticket}</h2>
                          <p className="text-sm text-slate-500">{ticketTemplate.paperWidth} · {ticketTemplate.name}</p>
                        </div>
                        <Button variant="ghost" size="icon" aria-label="Réglages ticket" onClick={() => setSettingsPanel("ticket")}>
                          <Settings2 className="h-5 w-5" />
                        </Button>
                      </div>
                      <TicketPreview size={layout.isPortrait ? "normal" : "large"} template={ticketTemplate} record={currentRecord} />
                    </div>
                </section>

                <aside className={`rounded-lg border border-white/80 bg-white/80 p-3 shadow-card backdrop-blur ${layout.isPortrait ? "order-1" : "lg:sticky lg:top-3 lg:self-start"}`}>
                  <div className="flex items-center gap-3">
                    <img className="h-16 w-20 rounded-lg bg-surface-soft object-contain p-2" src={selectedDevice.photo || equipment[0].image} alt={selectedDevice.name || "Balance"} />
                    <div className="min-w-0 flex-1">
                      <p className="text-sm font-semibold text-slate-500">{copy.balance}</p>
                      <h2 className="truncate text-lg font-bold tracking-normal">{selectedDevice.name || selectedDevice.model || copy.balance}</h2>
                      <p className="mt-1 truncate text-xs font-medium text-slate-500">{selectedDevice.serialNumber ? `${copy.serialNumber} : ${selectedDevice.serialNumber}` : copy.balanceAssociated}</p>
                    </div>
                    <Button variant="ghost" size="icon" aria-label="Options pesée" onClick={() => setSettingsPanel("weighing")}>
                      <Settings2 className="h-5 w-5" />
                    </Button>
                  </div>

                  <div className={`mt-3 min-h-[146px] overflow-hidden rounded-[22px] p-4 shadow-[inset_0_1px_0_rgba(255,255,255,0.12)] ${isBalanceLive ? "bg-[linear-gradient(135deg,#050816_0%,#10251f_54%,#2f28d8_145%)] text-white" : "bg-amber-50 text-amber-950"}`}>
                    <div className="flex items-center justify-between gap-3">
                      <p className={`text-sm font-semibold ${isBalanceLive ? "text-white/58" : "text-amber-700"}`}>{copy.weighing}</p>
                      <span className={`rounded-full px-2.5 py-1 text-xs font-black ${isBalanceLive ? "bg-green-500/15 text-green-300" : "bg-amber-100 text-amber-800"}`}>{isBalanceLive ? copy.active : copy.noData}</span>
                    </div>
                    {isBalanceLive ? (
                      <>
                        <p className="mt-2 truncate text-right font-mono text-[36px] font-bold leading-10 tracking-normal tabular-nums">{displayLines[0]}</p>
                        <div className="mt-2 max-h-[46px] space-y-1 overflow-hidden font-mono text-xs tabular-nums text-white/50">
                          {displayLines.slice(1).map((line, index) => <p className="truncate text-right" key={`${line}-${index}`}>{line}</p>)}
                        </div>
                      </>
                    ) : (
                      <div className="grid min-h-[104px] place-items-center text-center">
                        <p className="max-w-sm text-base font-black">{copy.noWeight}</p>
                      </div>
                    )}
                  </div>

                  <div className="mt-3 grid grid-cols-3 gap-2">
                    <CompactField label={copy.operator} value={operator} onChange={setOperator} />
                    <CompactField label="Lot" value={lotNumber} onChange={setLotNumber} />
                    <CompactField label={copy.sample} value={sampleId} onChange={setSampleId} />
                    <CompactField className="col-span-3" label={copy.comment} value={comment} onChange={setComment} />
                  </div>

                  <div className="mt-3 grid grid-cols-2 gap-2">
                    <Button className="col-span-2 min-h-[56px] rounded-2xl text-base" disabled={!isBalanceLive} onClick={printCurrentTicket}>
                      <Printer className="h-5 w-5" />
                      {copy.print}
                    </Button>
                    <LongPressButton className="min-h-[48px] rounded-2xl" disabled={!isBalanceLive && !tareValue} onClick={() => {
                      if (isBalanceLive) sendBalanceCommand("tare");
                    }} onLongPress={() => sendBalanceCommand("clear-tare")}>
                      <RotateCcw className="h-4 w-4" />
                      {tareValue ? `${copy.tare} ${tareValue}` : copy.tare}
                    </LongPressButton>
                    <Button variant="secondary" className="min-h-[48px] rounded-2xl" disabled={!isBalanceLive} onClick={() => sendBalanceCommand("zero")}>
                      <Zap className="h-4 w-4" />
                      {copy.zero}
                    </Button>
                  </div>
                  <p className="mt-2 truncate rounded-2xl bg-surface-soft px-3 py-2 text-sm font-semibold text-slate-600">{printStatus}</p>
                </aside>
              </div>
            ) : (
              <>
            <div className="mt-5 grid grid-cols-4 gap-2 rounded-2xl bg-slate-100 p-1">
              <TabButton active={view === "weighing"} icon={Scale} label="Pesée" onClick={() => setView("weighing")} />
              <TabButton active={view === "ticket"} icon={ReceiptText} label="Ticket" onClick={() => setView("ticket")} />
              <TabButton active={view === "scan"} icon={ScanLine} label="Scan" onClick={() => setView("scan")} />
              <TabButton active={view === "history"} icon={History} label="Historique" onClick={() => setView("history")} />
            </div>

        {view === "weighing" && (
          <section className="mt-5">
            <div className="grid gap-4 lg:grid-cols-[minmax(0,0.85fr)_minmax(390px,0.65fr)]">
              <div className="rounded-lg border border-border bg-surface p-4 shadow-card">
              <div className="flex items-center gap-4">
                <img className="h-20 w-24 rounded-lg bg-surface-soft object-contain p-2" src={equipment[0].image} alt="Balance" />
                <div className="min-w-0 flex-1">
                  <p className="text-sm font-semibold text-slate-500">Poste de pesée</p>
                  <h2 className="truncate text-xl font-bold tracking-normal">Balance + Imprimante LabConnect</h2>
                  <p className="mt-1 truncate text-xs font-medium text-slate-500">Balance associée</p>
                </div>
                <Button variant="ghost" size="icon" aria-label="Options pesée" onClick={() => setSettingsPanel("weighing")}>
                  <Settings2 className="h-5 w-5" />
                </Button>
              </div>

              <div className={`mt-6 min-h-[178px] overflow-hidden rounded-[24px] p-5 ${isBalanceLive ? "bg-slate-950 text-white" : "bg-amber-50 text-amber-950"}`}>
                <div className="flex items-center justify-between gap-3">
                  <p className={`text-sm font-semibold ${isBalanceLive ? "text-white/58" : "text-amber-700"}`}>Pesée en temps réel</p>
                  <span className={`rounded-full px-2.5 py-1 text-xs font-black ${isBalanceLive ? "bg-green-500/15 text-green-300" : "bg-amber-100 text-amber-800"}`}>{isBalanceLive ? "Actif" : "Aucune donnée"}</span>
                </div>
                {isBalanceLive ? (
                  <>
                    <p className="mt-3 truncate text-right font-mono text-[38px] font-bold leading-10 tracking-normal tabular-nums">{displayLines[0]}</p>
                    <div className="mt-4 max-h-[78px] space-y-1 overflow-hidden font-mono text-xs tabular-nums text-white/50">
                      {displayLines.slice(1).map((line, index) => <p className="truncate text-right" key={`${line}-${index}`}>{line}</p>)}
                    </div>
                  </>
                ) : (
                  <div className="grid min-h-[104px] place-items-center text-center">
                    <p className="max-w-sm text-base font-black">Aucune pesée reçue de la balance.</p>
                  </div>
                )}
              </div>

              <div className="mt-4 grid grid-cols-2 gap-2">
                <Button className="col-span-2 min-h-[64px] rounded-2xl text-base" disabled={!isBalanceLive} onClick={printCurrentTicket}>
                  <Printer className="h-5 w-5" />
                  Imprimer le ticket
                </Button>
                <LongPressButton className="min-h-[56px] rounded-2xl" disabled={!isBalanceLive && !tareValue} onClick={() => {
                  if (isBalanceLive) sendBalanceCommand("tare");
                }} onLongPress={() => sendBalanceCommand("clear-tare")}>
                  <RotateCcw className="h-4 w-4" />
                  {tareValue ? `Tare ${tareValue}` : "Tare"}
                </LongPressButton>
                <Button variant="secondary" className="min-h-[56px] rounded-2xl" disabled={!isBalanceLive} onClick={() => sendBalanceCommand("zero")}>
                  <Zap className="h-4 w-4" />
                  Zéro
                </Button>
              </div>
              <p className="mt-3 rounded-2xl bg-surface-soft px-4 py-3 text-sm font-semibold text-slate-600">{printStatus}</p>
              </div>

              <aside className="rounded-lg border border-border bg-surface p-4 shadow-card">
                <div className="mb-3 flex items-center justify-between gap-3">
                  <div>
                    <h2 className="text-lg font-black">Ticket</h2>
                    <p className="text-sm text-slate-500">{ticketTemplate.paperWidth} · {ticketTemplate.name}</p>
                  </div>
                  <ReceiptText className="h-5 w-5 text-slate-500" />
                </div>
                <TicketPreview size={layout.isPortrait ? "normal" : "large"} template={ticketTemplate} record={currentRecord} />
              </aside>
            </div>

            <div className="mt-4 rounded-lg border border-border bg-surface p-4 shadow-card">
              <div className="grid gap-3 sm:grid-cols-3">
                <Field label="Opérateur" value={operator} onChange={setOperator} />
                <Field label="Lot" value={lotNumber} onChange={setLotNumber} />
                <Field label="Échantillon" value={sampleId} onChange={setSampleId} />
              </div>
            </div>
          </section>
        )}

        {view === "ticket" && (
          <section className="mt-5">
            <div className="mb-3 flex items-center justify-between gap-3">
              <div>
                <h2 className="text-base font-semibold">Ticket</h2>
                <p className="text-sm text-slate-500">Modèle, aperçu et règles d’impression.</p>
              </div>
              <Button variant="ghost" size="icon" aria-label="Options ticket" onClick={() => setSettingsPanel("ticket")}>
                <Settings2 className="h-5 w-5" />
              </Button>
            </div>
            <PrintTicketDesigner template={ticketTemplate} templates={ticketTemplates} record={currentRecord} onTemplateChange={setTicketTemplate} />
          </section>
        )}

        {view === "scan" && (
          <section className="mt-5">
            <ScanTicketView
              currentRecord={currentRecord}
              onClear={() => setScannedRecord(null)}
              onPdf={exportRecordPdf}
              onPrint={printRecordTicket}
              onSave={saveScannedRecord}
              onScan={handleScannedRecord}
              onUseRecord={useScannedRecord}
              onOpenSettings={() => setSettingsPanel("scan")}
              record={scannedRecord}
              settings={scanSettings}
              ticketTemplate={ticketTemplate}
            />
          </section>
        )}

        {view === "history" && (
          <section className="mt-5 rounded-lg border border-border bg-surface p-4 shadow-card">
            <div className="flex items-center justify-between gap-3">
              <div>
                <h2 className="text-base font-semibold">Historique</h2>
                <p className="text-sm text-slate-500">{history.length} pesées locales</p>
              </div>
              <div className="flex gap-2">
                <Button variant="secondary" size="icon" aria-label="Exporter CSV" onClick={exportCsv}><Download className="h-4 w-4" /></Button>
                <Button variant="secondary" size="icon" aria-label="Exporter PDF" onClick={exportPdf}><FileText className="h-4 w-4" /></Button>
              </div>
            </div>
            <div className="mt-4 space-y-2">
              {history.map((record) => (
                <article key={record.id} className="rounded-2xl bg-surface-soft p-3">
                  <p className="font-mono text-sm font-bold">{record.rawBalanceLine}</p>
                  <p className="mt-1 text-xs font-medium text-slate-500">{record.dateTime} · {record.lotNumber} · {record.sampleId}</p>
                </article>
              ))}
            </div>
          </section>
        )}
              </>
            )}
      </div>
        </>
      )}
      <ContextSettingsSheet
        currentRecord={currentRecord}
        onClose={() => setSettingsPanel(null)}
        onScanSettingsChange={setScanSettings}
        onTicketTemplateChange={setTicketTemplate}
        onWeighingSettingsChange={setWeighingSettings}
        open={settingsPanel}
        rawBalanceLine={lines[0] ?? ""}
        scanSettings={scanSettings}
        ticketTemplate={ticketTemplate}
        weighingSettings={weighingSettings}
      />
    </main>
  );
}

function ConnectionPill({ copy, isSimulation, state }: { copy: (typeof appCopy)[AppLanguage]; isSimulation: boolean; state: ConnectionState }) {
  const label = isSimulation ? copy.simulation : state === "connected" ? copy.balanceConnected : state === "connecting" ? copy.connecting : copy.offline;
  const className = isSimulation ? "bg-brand-soft text-blue-700" : state === "connected" ? "bg-success-soft text-green-700" : state === "connecting" ? "bg-warning-soft text-amber-700" : "bg-slate-100 text-slate-600";
  return (
    <div className={`inline-flex min-h-11 items-center rounded-full px-3 py-2 text-right text-xs font-bold ${className}`}>
      <span className="inline-flex items-center gap-1.5"><Wifi className="h-3.5 w-3.5" />{label}</span>
    </div>
  );
}

function LanguageToggle({ language, onChange }: { language: AppLanguage; onChange: (language: AppLanguage) => void }) {
  const nextLanguage = language === "fr" ? "en" : "fr";
  return (
    <button
      aria-label="Changer de langue"
      className="inline-flex min-h-11 items-center rounded-full border border-white/70 bg-white/74 px-3 text-xs font-black text-slate-700 shadow-sm transition active:scale-[0.98]"
      type="button"
      onClick={() => onChange(nextLanguage)}
    >
      {language.toUpperCase()}
    </button>
  );
}

function ContextSettingsSheet({
  currentRecord,
  onClose,
  onScanSettingsChange,
  onTicketTemplateChange,
  onWeighingSettingsChange,
  open,
  rawBalanceLine,
  scanSettings,
  ticketTemplate,
  weighingSettings
}: {
  currentRecord: WeighingRecord;
  onClose: () => void;
  onScanSettingsChange: (settings: ScanSettings) => void;
  onTicketTemplateChange: (template: WeighingTicketTemplate) => void;
  onWeighingSettingsChange: (settings: WeighingSettings) => void;
  open: SettingsPanel | null;
  rawBalanceLine: string;
  scanSettings: ScanSettings;
  ticketTemplate: WeighingTicketTemplate;
  weighingSettings: WeighingSettings;
}) {
  const [draftWeighing, setDraftWeighing] = useState(weighingSettings);
  const [draftScan, setDraftScan] = useState(scanSettings);
  const [draftTicket, setDraftTicket] = useState(ticketTemplate);

  useEffect(() => {
    if (!open) return;
    setDraftWeighing(weighingSettings);
    setDraftScan(scanSettings);
    setDraftTicket(ticketTemplate);
  }, [open, scanSettings, ticketTemplate, weighingSettings]);

  useEffect(() => {
    if (!open) return;
    const previousBodyOverflow = document.body.style.overflow;
    const previousHtmlOverflow = document.documentElement.style.overflow;
    document.body.style.overflow = "hidden";
    document.documentElement.style.overflow = "hidden";
    return () => {
      document.body.style.overflow = previousBodyOverflow;
      document.documentElement.style.overflow = previousHtmlOverflow;
    };
  }, [open]);

  if (!open) return null;

  const title = open === "weighing" ? "Options pesée" : open === "ticket" ? "Options ticket" : "Options scan";
  const previewRawLine = rawBalanceLine || "ST,+0000,07 g";
  const previewFormattedLine = formatBalanceFrame(previewRawLine, draftWeighing);
  const apply = () => {
    if (open === "weighing") {
      onWeighingSettingsChange(draftWeighing);
    }
    if (open === "ticket") {
      onTicketTemplateChange(draftTicket);
    }
    if (open === "scan") {
      onScanSettingsChange(draftScan);
    }
    onClose();
  };

  return (
    <div className="fixed inset-0 z-[60] grid touch-none place-items-center overscroll-contain bg-slate-950/62 p-4 backdrop-blur-sm animate-lab-screen" onClick={onClose}>
      <section className={`flex max-h-[calc(100vh-32px)] w-full touch-auto flex-col overscroll-contain rounded-[28px] bg-white p-5 shadow-sheet animate-lab-panel ${open === "ticket" ? "max-w-[980px]" : "max-w-[420px]"}`} onClick={(event) => event.stopPropagation()}>
        <div className="flex items-center justify-between gap-3">
          <div>
            <p className="text-sm font-bold text-slate-500">Réglages avancés</p>
            <h2 className="text-xl font-black tracking-normal">{title}</h2>
          </div>
          <Button variant="ghost" size="icon" aria-label="Fermer" onClick={onClose}>
            <X className="h-5 w-5" />
          </Button>
        </div>

        <div className="mt-5 min-h-0 flex-1 overflow-y-auto pr-1">
        {open === "weighing" && (
          <div className="grid gap-3">
            <div className="rounded-2xl bg-slate-950 p-4 text-white">
              <p className="text-xs font-bold uppercase text-white/50">Aperçu valeur</p>
              <div className="mt-3 grid gap-2">
                <div>
                  <p className="text-xs font-bold text-white/45">Brute</p>
                  <p className="mt-1 truncate font-mono text-sm font-semibold text-white/70">{previewRawLine}</p>
                </div>
                <div>
                  <p className="text-xs font-bold text-white/45">Corrigée</p>
                  <p className="mt-1 truncate font-mono text-2xl font-black tracking-normal">{previewFormattedLine}</p>
                </div>
              </div>
            </div>
            <ToggleSetting
              checked={draftWeighing.printPlusSign}
              label="Imprimer le signe +"
              onChange={(printPlusSign) => setDraftWeighing({ ...draftWeighing, printPlusSign })}
            />
            <ToggleSetting
              checked={draftWeighing.trimIntegerLeadingZeros}
              label="Retirer les zéros non significatifs"
              onChange={(trimIntegerLeadingZeros) => setDraftWeighing({ ...draftWeighing, trimIntegerLeadingZeros })}
            />
            <label className="grid gap-1.5 text-sm font-semibold text-slate-700">
              Séparateur décimal
              <select
                className="min-h-12 rounded-2xl border border-border bg-white px-4 text-sm font-medium text-slate-950 outline-none focus:border-brand"
                value={draftWeighing.decimalSeparator}
                onChange={(event) => setDraftWeighing({ ...draftWeighing, decimalSeparator: event.target.value as "." | "," })}
              >
                <option value=".">Point .</option>
                <option value=",">Virgule ,</option>
              </select>
            </label>
            <NumberSetting
              label="Masquer au début de la trame"
              min={0}
              max={24}
              value={draftWeighing.maskStart}
              onChange={(maskStart) => setDraftWeighing({ ...draftWeighing, maskStart })}
            />
            <NumberSetting
              label="Masquer à la fin de la trame"
              min={0}
              max={24}
              value={draftWeighing.maskEnd}
              onChange={(maskEnd) => setDraftWeighing({ ...draftWeighing, maskEnd })}
            />
          </div>
        )}

        {open === "ticket" && (
          <div>
            <PrintTicketDesigner template={draftTicket} templates={ticketTemplates} record={currentRecord} onTemplateChange={setDraftTicket} />
          </div>
        )}

        {open === "scan" && (
          <div className="grid gap-3">
            <ToggleSetting
              checked={draftScan.manualInput}
              label="Afficher la saisie manuelle"
              onChange={(manualInput) => setDraftScan({ ...draftScan, manualInput })}
            />
            <ToggleSetting
              checked={draftScan.autoSaveScans}
              label="Sauvegarder après scan"
              onChange={(autoSaveScans) => setDraftScan({ ...draftScan, autoSaveScans })}
            />
            <ToggleSetting
              checked={draftScan.autoPrintScans}
              label="Réimprimer après scan"
              onChange={(autoPrintScans) => setDraftScan({ ...draftScan, autoPrintScans })}
            />
            <NumberSetting
              label="Durée d’activation du scanner"
              min={3}
              max={30}
              value={draftScan.activationSeconds}
              onChange={(activationSeconds) => setDraftScan({ ...draftScan, activationSeconds })}
            />
          </div>
        )}
        </div>
        <div className="mt-5 grid grid-cols-2 gap-2">
          <Button variant="secondary" className="min-h-12 rounded-2xl" onClick={onClose}>Annuler</Button>
          <Button className="min-h-12 rounded-2xl" onClick={apply}>Appliquer</Button>
        </div>
      </section>
    </div>
  );
}

function ToggleSetting({ checked, label, onChange }: { checked: boolean; label: string; onChange: (value: boolean) => void }) {
  return (
    <label className="flex min-h-14 items-center justify-between gap-3 rounded-2xl bg-surface-soft px-4 text-sm font-bold text-slate-700">
      {label}
      <input className="h-5 w-5 accent-blue-600" checked={checked} type="checkbox" onChange={(event) => onChange(event.target.checked)} />
    </label>
  );
}

function NumberSetting({ label, max, min, onChange, value }: { label: string; max: number; min: number; onChange: (value: number) => void; value: number }) {
  const decrement = () => onChange(clampNumber(value - 1, min, max));
  const increment = () => onChange(clampNumber(value + 1, min, max));

  return (
    <div className="grid gap-1.5 text-sm font-semibold text-slate-700">
      <span>{label}</span>
      <div className="grid min-h-12 grid-cols-[48px_1fr_48px] overflow-hidden rounded-2xl border border-border bg-white">
        <button className="grid place-items-center border-r border-border text-xl font-black text-slate-700 active:bg-slate-100 disabled:opacity-35" type="button" onClick={decrement} disabled={value <= min} aria-label={`Diminuer ${label}`}>
          -
        </button>
        <div className="grid place-items-center font-mono text-lg font-black text-slate-950">{value}</div>
        <button className="grid place-items-center border-l border-border text-xl font-black text-slate-700 active:bg-slate-100 disabled:opacity-35" type="button" onClick={increment} disabled={value >= max} aria-label={`Augmenter ${label}`}>
          +
        </button>
      </div>
    </div>
  );
}

function ScanTicketView({
  currentRecord,
  onClear,
  onOpenSettings,
  onPdf,
  onPrint,
  onSave,
  onScan,
  onUseRecord,
  record,
  settings,
  ticketTemplate
}: {
  currentRecord: WeighingRecord;
  onClear: () => void;
  onOpenSettings: () => void;
  onPdf: (record: WeighingRecord) => void;
  onPrint: (record: WeighingRecord) => void;
  onSave: (record: WeighingRecord) => void;
  onScan: (record: WeighingRecord) => void;
  onUseRecord: (record: WeighingRecord) => void;
  record: WeighingRecord | null;
  settings: ScanSettings;
  ticketTemplate: WeighingTicketTemplate;
}) {
  const [manualCode, setManualCode] = useState("");
  const [scanMessage, setScanMessage] = useState("Utilisez le lecteur intégré pour scanner un ticket LabConnect.");
  const [lastScanSource, setLastScanSource] = useState<"integrated" | "manual" | "test" | null>(null);
  const [scanDialog, setScanDialog] = useState<{
    open: boolean;
    status: "active" | "success" | "error" | "expired";
    remaining: number;
    message: string;
  }>({
    open: false,
    status: "active",
    remaining: settings.activationSeconds,
    message: ""
  });

  const handlePayload = (payload: string, source: "integrated" | "manual" | "test") => {
    if (source === "integrated") {
      window.LabConnectScanner?.stopScan?.();
    }
    const decoded = parseTicketQrPayload(payload);
    if (!decoded) {
      setScanMessage("Code non reconnu. Scannez un ticket LabConnect.");
      setLastScanSource(source);
      if (source === "integrated") {
        setScanDialog((current) => ({
          ...current,
          open: true,
          status: "error",
          message: "Code non reconnu."
        }));
      }
      return false;
    }
    onScan(ticketQrToRecord(decoded));
    setLastScanSource(source);
    setScanMessage(source === "integrated" ? "Ticket retrouvé par le lecteur intégré." : "Ticket retrouvé.");
    if (source === "integrated") {
      setScanDialog((current) => ({
        ...current,
        open: true,
        status: "success",
        message: "Ticket lu avec succès."
      }));
    }
    return true;
  };

  useEffect(() => {
    const listener = (event: WindowEventMap["labconnect-integrated-scan"]) => {
      handlePayload(event.detail.data, "integrated");
    };
    window.addEventListener("labconnect-integrated-scan", listener);
    return () => window.removeEventListener("labconnect-integrated-scan", listener);
  }, [handlePayload]);

  useEffect(() => {
    if (!scanDialog.open || scanDialog.status !== "active") return;
    if (scanDialog.remaining <= 0) {
      window.LabConnectScanner?.stopScan?.();
      setScanDialog((current) => ({
        ...current,
        status: "expired",
        message: "Aucun ticket lu."
      }));
      return;
    }
    const timer = window.setTimeout(() => {
      setScanDialog((current) => ({
        ...current,
        remaining: Math.max(0, current.remaining - 1)
      }));
    }, 1000);
    return () => window.clearTimeout(timer);
  }, [scanDialog.open, scanDialog.remaining, scanDialog.status]);

  const submitManualCode = () => {
    if (!manualCode.trim()) return;
    handlePayload(manualCode, "manual");
  };

  const startIntegratedScan = () => {
    const activationSeconds = clampNumber(settings.activationSeconds, 3, 30);
    setScanDialog({
      open: true,
      status: "active",
      remaining: activationSeconds,
      message: "Scanner activé."
    });
    setScanMessage("Scanner activé. Présentez le QR code du ticket.");
    try {
      const result = window.LabConnectScanner?.startScan?.(activationSeconds * 1000);
      if (result) {
        const parsed = JSON.parse(result) as { ok: boolean; error?: string };
        if (!parsed.ok) {
          setScanDialog({
            open: true,
            status: "error",
            remaining: activationSeconds,
            message: parsed.error ?? "Activation impossible."
          });
        }
      }
    } catch (error) {
      setScanDialog({
        open: true,
        status: "error",
        remaining: activationSeconds,
        message: error instanceof Error ? error.message : "Activation impossible."
      });
    }
  };

  const closeScanDialog = () => {
    if (scanDialog.status === "active") {
      window.LabConnectScanner?.stopScan?.();
    }
    setScanDialog((current) => ({ ...current, open: false }));
  };

  return (
    <div className="mx-auto w-full max-w-[620px]">
      <section className="rounded-[28px] border border-white/80 bg-white/86 p-5 shadow-card backdrop-blur">
        <div className="flex items-center justify-between gap-3">
          <div>
            <h2 className="text-2xl font-black tracking-normal">Scanner un ticket</h2>
            <p className="mt-1 text-sm font-semibold text-slate-500">{record ? "Ticket lu avec succès." : "Présentez le QR code au lecteur intégré."}</p>
          </div>
        </div>

        <div className="mt-5 rounded-[26px] bg-[linear-gradient(135deg,#050816_0%,#0f2b24_56%,#4338f2_150%)] p-5 text-white">
          <div className="grid place-items-center py-3 text-center">
            <div className="grid h-20 w-20 place-items-center rounded-[26px] bg-white/10">
              <ScanLine className="h-10 w-10" />
            </div>
            <h3 className="mt-4 text-2xl font-black tracking-normal">{record ? "Ticket retrouvé" : "Prêt à scanner"}</h3>
            <p className="mt-2 max-w-sm text-sm font-semibold text-white/62">{record ? "Les informations du ticket sont disponibles ci-dessous." : scanMessage}</p>
          </div>
        </div>

        {!record ? (
          <Button className="mt-4 min-h-14 w-full rounded-2xl text-base" onClick={startIntegratedScan}>
            <ScanLine className="h-5 w-5" />
            Activer le scanner
          </Button>
        ) : (
          <div className="mt-4 rounded-[24px] bg-surface-soft p-4">
            <div className="flex items-start justify-between gap-3">
              <div>
                <p className="text-xs font-black uppercase text-slate-500">Net</p>
                <p className="mt-1 font-mono text-3xl font-black tracking-normal text-slate-950">{record.netWeight || record.weight || "-"}</p>
              </div>
              <Button variant="secondary" size="icon" aria-label="Effacer" onClick={onClear}><X className="h-4 w-4" /></Button>
            </div>
            <dl className="mt-4 grid gap-2 text-sm">
              <ScanRow label="Date" value={record.dateTime} />
              <ScanRow label="Lot" value={record.lotNumber || "-"} />
              <ScanRow label="Échantillon" value={record.sampleId || "-"} />
              <ScanRow label="Balance" value={record.balanceName || "-"} />
            </dl>
            <div className="mt-4 grid grid-cols-3 gap-2">
              <Button className="col-span-3 min-h-12 rounded-2xl" onClick={() => onPrint(record)}>
                <Printer className="h-4 w-4" />
                Réimprimer
              </Button>
              <Button variant="secondary" className="min-h-12 rounded-2xl" onClick={() => onSave(record)}>
                <Save className="h-4 w-4" />
                Sauver
              </Button>
              <Button variant="secondary" className="min-h-12 rounded-2xl" onClick={() => onPdf(record)}>
                <FileText className="h-4 w-4" />
                PDF
              </Button>
              <Button variant="secondary" className="min-h-12 rounded-2xl" onClick={() => onUseRecord(record)}>Utiliser</Button>
            </div>
          </div>
        )}
      </section>

      <ScanActivationDialog dialog={scanDialog} onClose={closeScanDialog} onRestart={startIntegratedScan} />
    </div>
  );
}

function ScanRow({ label, value }: { label: string; value: string }) {
  return (
    <div className="flex items-center justify-between gap-3">
      <dt className="text-slate-500">{label}</dt>
      <dd className="truncate text-right font-semibold text-slate-950">{value}</dd>
    </div>
  );
}

function ScanActivationDialog({
  dialog,
  onClose,
  onRestart
}: {
  dialog: {
    open: boolean;
    status: "active" | "success" | "error" | "expired";
    remaining: number;
    message: string;
  };
  onClose: () => void;
  onRestart: () => void;
}) {
  useEffect(() => {
    if (!dialog.open) return;
    const previousBodyOverflow = document.body.style.overflow;
    const previousHtmlOverflow = document.documentElement.style.overflow;
    document.body.style.overflow = "hidden";
    document.documentElement.style.overflow = "hidden";
    return () => {
      document.body.style.overflow = previousBodyOverflow;
      document.documentElement.style.overflow = previousHtmlOverflow;
    };
  }, [dialog.open]);

  if (!dialog.open) return null;

  const isActive = dialog.status === "active";
  const isSuccess = dialog.status === "success";
  const title = isSuccess ? "Ticket lu" : dialog.status === "expired" ? "Temps écoulé" : dialog.status === "error" ? "Lecture impossible" : "Scanner activé";
  const message = dialog.message || (isActive ? "Présentez le QR code du ticket devant le lecteur." : "");

  return (
    <div className="fixed inset-0 z-[70] grid touch-none place-items-center overscroll-contain bg-slate-950/68 p-4 backdrop-blur-sm animate-lab-screen">
      <section className="w-full max-w-[360px] touch-auto rounded-[28px] bg-white p-5 text-center shadow-sheet animate-lab-panel">
        <div className={`mx-auto grid h-20 w-20 place-items-center rounded-full ${isSuccess ? "bg-success-soft text-green-700" : isActive ? "bg-brand-soft text-blue-700" : "bg-warning-soft text-amber-700"}`}>
          {isSuccess ? <Check className="h-10 w-10" /> : isActive ? <Loader2 className="h-9 w-9 animate-spin" /> : <ScanLine className="h-9 w-9" />}
        </div>
        <h2 className="mt-5 text-2xl font-black tracking-normal">{title}</h2>
        <p className="mx-auto mt-2 max-w-[260px] text-sm font-semibold text-slate-500">{message}</p>
        {isActive && (
          <div className="mx-auto mt-5 grid h-24 w-24 place-items-center rounded-full border-8 border-slate-100 bg-slate-950 text-white">
            <span className="font-mono text-3xl font-black">{dialog.remaining}</span>
          </div>
        )}
        <div className="mt-5 grid grid-cols-2 gap-2">
          <Button variant="secondary" className="min-h-12 rounded-2xl" onClick={onClose}>
            {isActive ? "Annuler" : "Fermer"}
          </Button>
          <Button className="min-h-12 rounded-2xl" onClick={isActive || isSuccess ? onClose : onRestart}>
            {isActive || isSuccess ? "OK" : "Réessayer"}
          </Button>
        </div>
      </section>
    </div>
  );
}

function SetupExperience({
  autoAssociate,
  connectionState,
  detectedFormat,
  device,
  isSimulation,
  language,
  onAutoAssociateChange,
  onConnectSavedDevice,
  onDeleteSavedDevice,
  onLanguageChange,
  onOpenTicketScan,
  onSaveDevice,
  onStartScan,
  onStartSimulation,
  onTerminalFormatChange,
  onUpdateSavedDevice,
  savedDevices,
  step,
  terminalFormat
}: {
  autoAssociate: boolean;
  connectionState: ConnectionState;
  detectedFormat: "v3" | "v3mix";
  device: DetectedDevice | null;
  isSimulation: boolean;
  language: AppLanguage;
  onAutoAssociateChange: (value: boolean) => void;
  onConnectSavedDevice: (device: DetectedDevice) => void;
  onDeleteSavedDevice: (deviceId: string) => void;
  onLanguageChange: (language: AppLanguage) => void;
  onOpenTicketScan: () => void;
  onSaveDevice: (device: DetectedDevice) => void;
  onStartScan: () => void;
  onStartSimulation: () => void;
  onTerminalFormatChange: (format: TerminalFormat) => void;
  onUpdateSavedDevice: (device: DetectedDevice) => void;
  savedDevices: DetectedDevice[];
  step: SetupStep;
  terminalFormat: TerminalFormat;
}) {
  const [manageOpen, setManageOpen] = useState(false);
  const [draftDevice, setDraftDevice] = useState<DetectedDevice | null>(null);
  const copy = appCopy[language];

  useEffect(() => {
    if (step !== "select" || !device) return;
    const brand = inferBrand(device);
    setDraftDevice({
      ...device,
      brandId: device.brandId ?? brand.id,
      brandName: device.brandName ?? brand.name,
      brandLogo: device.brandLogo ?? brand.logo,
      photo: device.photo || equipment[0].image,
      name: device.name || `${brand.name} ${device.model || "Balance"}`
    });
  }, [device?.id, step]);

  const updateDraftDevice = (patch: Partial<DetectedDevice>) => {
    setDraftDevice((current) => (current ? { ...current, ...patch } : current));
  };

  const updateDraftBrand = (brandId: BalanceBrandId) => {
    const brand = balanceBrands.find((item) => item.id === brandId) ?? balanceBrands[0];
    updateDraftDevice({
      brandId: brand.id,
      brandName: brand.name,
      brandLogo: brand.logo,
      name: draftDevice?.name || `${brand.name} ${draftDevice?.model || "Balance"}`
    });
  };

  return (
    <section className="fixed inset-0 z-50 overflow-hidden bg-[#f6f7f8] text-slate-950 animate-lab-screen">
      <div className="absolute inset-x-0 top-0 h-40 bg-white/70 blur-3xl" />
      <div className="relative flex min-h-screen flex-col px-5 pb-6 pt-8">
        <header className="flex items-center justify-between animate-lab-fade-down">
          <div>
            <h1 className="text-[34px] font-black leading-10 tracking-normal">LabConnect Print</h1>
          </div>
          <div className="flex items-center gap-2">
            <LanguageToggle language={language} onChange={onLanguageChange} />
            <select
              value={terminalFormat}
              onChange={(e) => onTerminalFormatChange(e.target.value as TerminalFormat)}
              className="h-10 rounded-full border border-slate-200 bg-white px-3 text-xs font-bold text-slate-600 outline-none"
              aria-label="Format terminal"
            >
              <option value="auto">Auto ({detectedFormat === "v3" ? "V3" : "V3Mix"})</option>
              <option value="v3mix">V3Mix (paysage)</option>
              <option value="v3">V3 (portrait)</option>
            </select>
            <span className={`inline-flex min-h-10 items-center gap-2 rounded-full px-3 text-xs font-bold ${isSimulation ? "bg-brand-soft text-blue-700" : connectionState === "connected" ? "bg-success-soft text-green-700" : "bg-slate-100 text-slate-500"}`}>
              <Wifi className="h-4 w-4" />
              {isSimulation ? copy.simulation : connectionState === "connected" ? copy.ready : copy.connecting}
            </span>
            <Button variant="secondary" size="icon" aria-label="Gérer les balances" onClick={() => setManageOpen(true)}>
              <Settings2 className="h-4 w-4" />
            </Button>
          </div>
        </header>

        <div className="grid min-h-0 flex-1 place-items-center py-8">
          <div className="w-full max-w-5xl">
            {step === "intro" && (
              <div className="animate-lab-panel">
                <div className="text-center">
                  <div className="mx-auto grid h-24 w-24 place-items-center rounded-[30px] bg-slate-950 text-white shadow-float animate-lab-logo">
                    <Scale className="h-12 w-12" />
                  </div>
                  <h2 className="mt-5 text-[32px] font-black leading-10 tracking-normal animate-lab-fade-up [animation-delay:90ms]">{copy.actionPrompt}</h2>
                </div>
                <div className="mt-7 flex flex-wrap justify-center gap-3">
                  {savedDevices.map((saved, index) => (
                    <HomeBalanceCard key={saved.id} copy={copy} delay={`${170 + index * 35}ms`} device={withBrandDefaults(saved)} onClick={() => onConnectSavedDevice(withBrandDefaults(saved))} />
                  ))}
                  <HomeActionCard
                    badge={copy.readingBadge}
                    delay={`${230 + savedDevices.length * 35}ms`}
                    description="QR code"
                    icon={ScanLine}
                    title={copy.scanTicket}
                    onClick={onOpenTicketScan}
                  />
                </div>
                <button className="mx-auto mt-5 flex min-h-10 items-center gap-2 rounded-full px-4 text-sm font-bold text-slate-500 transition active:scale-[0.98] animate-lab-fade-up [animation-delay:310ms]" type="button" onClick={() => setManageOpen(true)}>
                  <Settings2 className="h-4 w-4" />
                  {copy.manageBalances}
                </button>
                <button
                  className="absolute bottom-6 right-6 inline-flex min-h-14 items-center gap-3 rounded-full bg-brand px-5 text-base font-black text-white shadow-float transition active:scale-[0.98]"
                  type="button"
                  onClick={onStartScan}
                >
                  <Plus className="h-5 w-5" />
                  {copy.addBalance}
                </button>
              </div>
            )}

            {step === "scan" && (
              <div className="animate-lab-panel rounded-[28px] bg-white p-6 text-center shadow-sheet">
                <div className="relative mx-auto grid h-52 w-52 place-items-center">
                  <span className="absolute h-48 w-48 rounded-full bg-blue-500/5 blur-2xl animate-lab-breathe" />
                  <span className="absolute h-40 w-40 rounded-full border border-blue-500/20 animate-lab-ping" />
                  <span className="absolute h-28 w-28 rounded-full border border-blue-500/30 animate-lab-ping [animation-delay:260ms]" />
                  <span className="absolute h-16 w-16 rounded-full bg-blue-600/10 animate-lab-pulse" />
                  <Radar className="relative h-16 w-16 text-blue-600 animate-lab-scan" />
                </div>
                <h2 className="text-2xl font-black tracking-normal animate-lab-fade-up">{connectionState === "connected" ? "Recherche en cours" : "Connexion à la balance"}</h2>
                <p className="mt-2 text-sm font-semibold text-slate-500 animate-lab-fade-up [animation-delay:80ms]">{connectionState === "connected" ? "Recherche des balances disponibles à proximité." : "Vérifiez que l’imprimante LabConnect est prête."}</p>
                <div className="mt-5 inline-flex items-center gap-2 rounded-full bg-slate-100 px-3 py-2 text-xs font-bold text-slate-500">
                  <Loader2 className="h-4 w-4 animate-spin" />
                  {connectionState === "connected" ? "Détection active" : "Connexion"}
                </div>
                {connectionState !== "connected" && (
                  <Button className="mt-5 min-h-[56px] w-full rounded-2xl text-base transition-transform duration-150 ease-out active:scale-[0.98] animate-lab-fade-up [animation-delay:120ms]" variant="secondary" onClick={onStartSimulation}>
                    Continuer en simulation
                  </Button>
                )}
              </div>
            )}

            {step === "select" && (
              <div className="flex max-h-[calc(100vh-112px)] flex-col overflow-hidden rounded-[28px] bg-white p-5 shadow-sheet animate-lab-panel">
                <div className="min-h-0 flex-1 overflow-y-auto pr-1">
                  <div className="text-center">
                    <div className="mx-auto grid h-20 w-20 place-items-center rounded-full bg-success-soft text-green-700 animate-lab-success">
                      <Check className="h-10 w-10" />
                    </div>
                    <h2 className="mt-5 text-2xl font-black tracking-normal animate-lab-fade-up [animation-delay:70ms]">Confirmer la balance</h2>
                    <p className="mt-2 text-sm font-semibold text-slate-500">Validez le fabricant et les informations affichées sur sa carte.</p>
                  </div>

                  {draftDevice && (
                    <div className="mt-5 space-y-4">
                      <BalanceBrandSelect value={draftDevice.brandId ?? "and"} onChange={updateDraftBrand} />
                      <BalanceCardEditor device={draftDevice} onChange={updateDraftDevice} />
                      <SavedDeviceCard device={draftDevice} isSimulation={isSimulation} />
                    </div>
                  )}

                  <label className="mt-4 flex min-h-12 items-center justify-between gap-3 rounded-2xl bg-slate-100 px-4 text-sm font-bold text-slate-700 animate-lab-fade-up [animation-delay:170ms]">
                    Associer automatiquement cette balance
                    <input className="h-5 w-5 accent-blue-600" checked={autoAssociate} type="checkbox" onChange={(event) => onAutoAssociateChange(event.target.checked)} />
                  </label>

                  <p className="mt-3 text-center text-xs font-semibold text-slate-500">Elle sera disponible sur l’accueil. Cliquez ensuite sur sa carte pour commencer.</p>
                </div>
                <Button className="mt-4 min-h-[58px] w-full rounded-2xl text-base transition-transform duration-150 ease-out active:scale-[0.98] animate-lab-fade-up [animation-delay:220ms]" disabled={!draftDevice} onClick={() => draftDevice && onSaveDevice(draftDevice)}>
                  Enregistrer la balance
                </Button>
              </div>
            )}
          </div>
        </div>
      </div>
      <SavedDevicesDialog
        devices={savedDevices}
        open={manageOpen}
        onClose={() => setManageOpen(false)}
        onConnect={(saved) => {
          onConnectSavedDevice(saved);
          setManageOpen(false);
        }}
        onDelete={onDeleteSavedDevice}
        onUpdate={onUpdateSavedDevice}
        onAddNew={() => {
          setManageOpen(false);
          onStartScan();
        }}
      />
    </section>
  );
}

function HomeActionCard({
  badge,
  delay,
  description,
  disabled = false,
  icon: Icon,
  onClick,
  title
}: {
  badge: string;
  delay: string;
  description: string;
  disabled?: boolean;
  icon: typeof Scale;
  onClick: () => void;
  title: string;
}) {
  return (
    <button
      className={`group flex min-h-[172px] w-full max-w-[246px] items-center gap-4 rounded-[26px] border border-border bg-white p-4 text-left shadow-card transition-[transform,box-shadow,background-color,opacity] duration-200 ease-out active:scale-[0.99] animate-lab-fade-up ${disabled ? "opacity-58" : "hover:shadow-float"}`}
      disabled={disabled}
      style={{ animationDelay: delay }}
      type="button"
      onClick={onClick}
    >
      <span className="grid h-16 w-16 shrink-0 place-items-center rounded-[22px] bg-slate-950 text-white transition-transform duration-200 group-active:scale-95">
        <Icon className="h-8 w-8" />
      </span>
      <span className="min-w-0 flex-1">
        <span className="inline-flex rounded-full bg-slate-100 px-2.5 py-1 text-[11px] font-black uppercase text-slate-500">{badge}</span>
        <span className="mt-2 block text-lg font-black leading-6 tracking-normal text-slate-950">{title}</span>
        <span className="mt-1 block text-sm font-semibold leading-5 text-slate-500">{description}</span>
      </span>
      <ChevronRight className="h-5 w-5 shrink-0 text-slate-400 transition-transform duration-200 group-active:translate-x-0.5" />
    </button>
  );
}

function HomeBalanceCard({ copy, delay, device, onClick }: { copy: (typeof appCopy)[AppLanguage]; delay: string; device: DetectedDevice; onClick: () => void }) {
  return (
    <button
      className="group flex min-h-[190px] w-full max-w-[274px] flex-col justify-between overflow-hidden rounded-[26px] border border-border bg-white p-3 text-left shadow-card transition-[transform,box-shadow] duration-200 ease-out active:scale-[0.99] hover:shadow-float animate-lab-fade-up"
      style={{ animationDelay: delay }}
      type="button"
      onClick={onClick}
    >
      <div className="relative h-28 overflow-hidden rounded-[22px] bg-[linear-gradient(135deg,#eff8ed_0%,#e2f5f1_100%)]">
        <img className="absolute inset-0 h-full w-full scale-[1.08] object-cover mix-blend-multiply transition-transform duration-200 ease-out group-active:scale-[1.04]" src={device.photo || equipment[0].image} alt="" />
        {device.brandLogo && <img className="absolute bottom-2 right-2 max-h-8 max-w-[82px] rounded-xl bg-white/92 px-2 py-1.5 shadow-card" src={device.brandLogo} alt={device.brandName ?? ""} />}
        <ChevronRight className="absolute right-3 top-3 h-5 w-5 rounded-full bg-white/78 p-0.5 text-slate-400 shadow-sm transition-transform duration-200 group-active:translate-x-0.5" />
      </div>
      <div className="mt-3 min-w-0 px-1">
        <p className="text-xs font-black uppercase text-slate-500">{device.brandName || copy.balance}</p>
        <h3 className="mt-1 truncate text-lg font-black leading-6 tracking-normal text-slate-950">{device.name || device.model || copy.balance}</h3>
        <p className="mt-1 truncate text-xs font-semibold text-slate-500">{device.serialNumber ? `${copy.serial} : ${device.serialNumber}` : device.model || copy.readyFallback}</p>
      </div>
    </button>
  );
}

function BalanceBrandSelect({ onChange, value }: { onChange: (brandId: BalanceBrandId) => void; value: BalanceBrandId }) {
  const selectedBrand = balanceBrands.find((brand) => brand.id === value) ?? balanceBrands[0];
  return (
    <label className="grid gap-2 rounded-[24px] bg-surface-soft p-3 text-sm font-black text-slate-700 animate-lab-fade-up [animation-delay:120ms]">
      Fabricant
      <div className="flex items-center gap-3">
        <div className="grid h-14 w-28 shrink-0 place-items-center rounded-2xl bg-white p-2">
          <img className="max-h-9 max-w-[92px] object-contain" src={selectedBrand.logo} alt={selectedBrand.name} />
        </div>
        <select
          className="min-h-14 flex-1 rounded-2xl border border-border bg-white px-4 text-base font-black text-slate-950 outline-none focus:border-brand"
          value={value}
          onChange={(event) => onChange(event.target.value as BalanceBrandId)}
        >
          {balanceBrands.map((brand) => (
            <option key={brand.id} value={brand.id}>{brand.name}</option>
          ))}
        </select>
      </div>
    </label>
  );
}

function BalanceCardEditor({ device, onChange }: { device: DetectedDevice; onChange: (patch: Partial<DetectedDevice>) => void }) {
  const uploadPhoto = (file: File | null) => {
    if (!file) return;
    const reader = new FileReader();
    reader.onload = () => {
      if (typeof reader.result === "string") {
        onChange({ photo: reader.result });
      }
    };
    reader.readAsDataURL(file);
  };

  return (
    <section className="grid gap-3 rounded-[24px] bg-surface-soft p-3 animate-lab-fade-up [animation-delay:150ms]">
      <div className="flex items-center gap-3">
        <img className="h-20 w-24 shrink-0 rounded-2xl bg-white object-contain p-2" src={device.photo || equipment[0].image} alt="" />
        <label className="flex min-h-12 flex-1 cursor-pointer items-center justify-center rounded-2xl border border-border bg-white px-4 text-sm font-black text-slate-700 active:scale-[0.98]">
          Changer la photo
          <input className="hidden" accept="image/*" type="file" onChange={(event) => uploadPhoto(event.target.files?.[0] ?? null)} />
        </label>
      </div>
      <div className="grid gap-2 sm:grid-cols-2">
        <CompactField label="Nom de la carte" value={device.name} onChange={(value) => onChange({ name: value })} />
        <CompactField label="Modèle" value={device.model} onChange={(value) => onChange({ model: value })} />
        <CompactField label="Numéro de série" value={device.serialNumber} onChange={(value) => onChange({ serialNumber: value })} />
        <CompactField label="Identifiant balance" value={device.deviceId} onChange={(value) => onChange({ deviceId: value })} />
      </div>
    </section>
  );
}

function SavedDeviceCard({ device, isSimulation, onClick }: { device: DetectedDevice; isSimulation?: boolean; onClick?: () => void }) {
  const content = (
    <>
      <div className="relative shrink-0">
        <img className="h-20 w-24 rounded-2xl bg-white object-contain p-2" src={device.photo || equipment[0].image} alt="" />
        {device.brandLogo && <img className="absolute -bottom-2 -right-2 max-h-7 max-w-16 rounded-lg bg-white px-1.5 py-1 shadow-card" src={device.brandLogo} alt={device.brandName ?? ""} />}
      </div>
      <div className="min-w-0 flex-1">
        <p className="text-xs font-black uppercase text-slate-500">{device.brandName || "Balance de pesée"}</p>
        <h3 className="truncate text-xl font-black tracking-normal">{device.name || device.model || "Balance"}</h3>
        <p className="mt-1 truncate text-xs font-semibold text-slate-500">{device.model ? `Modèle : ${device.model}` : "Modèle non renseigné"}</p>
        <p className="truncate text-xs font-semibold text-slate-500">{device.serialNumber ? `Numéro de série : ${device.serialNumber}` : "Numéro de série non renseigné"}</p>
        {device.deviceId && <p className="truncate text-xs font-semibold text-slate-500">Identifiant balance : {device.deviceId}</p>}
        {isSimulation !== undefined && <p className={`mt-2 text-xs font-bold ${isSimulation ? "text-blue-700" : "text-green-700"}`}>{isSimulation ? "Mode simulation actif" : "Prête à enregistrer"}</p>}
      </div>
      {onClick && <ChevronRight className="h-5 w-5 shrink-0 text-slate-400" />}
    </>
  );

  if (onClick) {
    return (
      <button className="flex w-full items-center gap-4 rounded-[24px] border border-border bg-surface-soft p-4 text-left transition active:scale-[0.99] animate-lab-device-card" type="button" onClick={onClick}>
        {content}
      </button>
    );
  }

  return <div className="flex w-full items-center gap-4 rounded-[24px] border border-border bg-surface-soft p-4 text-left animate-lab-device-card">{content}</div>;
}

function CompactField({ className = "", label, onChange, value }: { className?: string; label: string; onChange: (value: string) => void; value: string }) {
  return (
    <label className={`grid min-w-0 gap-1 text-xs font-black text-slate-600 ${className}`}>
      {label}
      <input className="min-h-10 w-full min-w-0 rounded-2xl border border-border bg-white px-3 text-sm font-semibold text-slate-950 outline-none focus:border-brand" value={value} onChange={(event) => onChange(event.target.value)} />
    </label>
  );
}

function SavedDevicesDialog({
  devices,
  onAddNew,
  onClose,
  onConnect,
  onDelete,
  onUpdate,
  open
}: {
  devices: DetectedDevice[];
  onAddNew: () => void;
  onClose: () => void;
  onConnect: (device: DetectedDevice) => void;
  onDelete: (deviceId: string) => void;
  onUpdate: (device: DetectedDevice) => void;
  open: boolean;
}) {
  const [editingId, setEditingId] = useState<string | null>(null);
  const [draftDevice, setDraftDevice] = useState<DetectedDevice | null>(null);

  useEffect(() => {
    if (!open) return;
    const previousBodyOverflow = document.body.style.overflow;
    const previousHtmlOverflow = document.documentElement.style.overflow;
    document.body.style.overflow = "hidden";
    document.documentElement.style.overflow = "hidden";
    return () => {
      document.body.style.overflow = previousBodyOverflow;
      document.documentElement.style.overflow = previousHtmlOverflow;
    };
  }, [open]);

  if (!open) return null;

  const startEdit = (device: DetectedDevice) => {
    setEditingId(device.id);
    setDraftDevice(withBrandDefaults(device));
  };

  const applyEdit = () => {
    if (!editingId || !draftDevice) return;
    onUpdate(draftDevice);
    setEditingId(null);
    setDraftDevice(null);
  };

  const updateDraft = (patch: Partial<DetectedDevice>) => {
    setDraftDevice((current) => (current ? { ...current, ...patch } : current));
  };

  const updateBrand = (brandId: BalanceBrandId) => {
    const brand = balanceBrands.find((item) => item.id === brandId) ?? balanceBrands[0];
    updateDraft({
      brandId: brand.id,
      brandName: brand.name,
      brandLogo: brand.logo
    });
  };

  return (
    <div className="fixed inset-0 z-[80] grid touch-none place-items-center overscroll-contain bg-slate-950/68 p-4 backdrop-blur-sm animate-lab-screen">
      <section className="flex max-h-[calc(100vh-32px)] w-full max-w-[560px] touch-auto flex-col rounded-[28px] bg-white p-5 shadow-sheet animate-lab-panel">
        <div className="flex items-center justify-between gap-4">
          <div>
            <h2 className="text-2xl font-black tracking-normal">Balances enregistrées</h2>
            <p className="mt-1 text-sm font-semibold text-slate-500">{devices.length > 0 ? "Connecter, modifier ou supprimer." : "Aucune balance enregistrée."}</p>
          </div>
          <Button variant="secondary" size="icon" aria-label="Fermer" onClick={onClose}>
            <X className="h-4 w-4" />
          </Button>
        </div>

        <div className="mt-5 min-h-0 flex-1 space-y-3 overflow-y-auto pr-1">
          {devices.length === 0 && (
            <div className="grid min-h-[180px] place-items-center rounded-[24px] bg-surface-soft p-5 text-center">
              <div>
                <Scale className="mx-auto h-10 w-10 text-slate-400" />
                <p className="mt-3 text-sm font-bold text-slate-500">Ajoutez une balance pour la retrouver ici.</p>
              </div>
            </div>
          )}

          {devices.map((saved) => (
            <article key={saved.id} className="rounded-[24px] bg-surface-soft p-3">
              {editingId === saved.id && draftDevice ? (
                <div className="grid gap-3">
                  <BalanceBrandSelect value={draftDevice.brandId ?? "and"} onChange={updateBrand} />
                  <BalanceCardEditor device={draftDevice} onChange={updateDraft} />
                </div>
              ) : (
                <>
                  <SavedDeviceCard device={withBrandDefaults(saved)} onClick={() => onConnect(withBrandDefaults(saved))} />
                  <div className="mt-3 grid grid-cols-[1fr_44px_44px] gap-2">
                    <Button className="min-h-11 rounded-2xl" onClick={() => onConnect(withBrandDefaults(saved))}>Connecter</Button>
                    <Button variant="secondary" size="icon" aria-label="Modifier" onClick={() => startEdit(saved)}>
                      <Pencil className="h-4 w-4" />
                    </Button>
                    <Button variant="secondary" size="icon" aria-label="Supprimer" onClick={() => onDelete(saved.id)}>
                      <Trash2 className="h-4 w-4" />
                    </Button>
                  </div>
                </>
              )}
            </article>
          ))}
        </div>

        {editingId && draftDevice ? (
          <div className="mt-4 grid grid-cols-2 gap-2">
            <Button variant="secondary" className="min-h-12 rounded-2xl" onClick={() => {
              setEditingId(null);
              setDraftDevice(null);
            }}>Annuler</Button>
            <Button className="min-h-12 rounded-2xl" onClick={applyEdit}>Appliquer</Button>
          </div>
        ) : (
          <Button variant="secondary" className="mt-4 min-h-12 w-full rounded-2xl" onClick={onAddNew}>
            <Plus className="h-4 w-4" />
            Ajouter une nouvelle balance
          </Button>
        )}
      </section>
    </div>
  );
}

function TabButton({ active, icon: Icon, label, onClick }: { active: boolean; icon: typeof Scale; label: string; onClick: () => void }) {
  return (
    <button className={`inline-flex min-h-12 items-center justify-center gap-2 rounded-xl text-sm font-bold transition ${active ? "bg-white text-slate-950 shadow-card" : "text-slate-500"}`} type="button" onClick={onClick}>
      <Icon className="h-4 w-4" />
      {label}
    </button>
  );
}

function SessionTabButton({ active, icon: Icon, label, onClick }: { active: boolean; icon: typeof Scale; label: string; onClick: () => void }) {
  return (
    <button className={`flex min-h-16 flex-col items-center justify-center gap-1 rounded-xl text-xs font-black transition lg:min-h-[84px] ${active ? "bg-white text-slate-950 shadow-card" : "text-slate-500"}`} type="button" onClick={onClick}>
      <Icon className="h-5 w-5" />
      {label}
    </button>
  );
}

function LongPressButton({
  children,
  className,
  disabled,
  onClick,
  onLongPress
}: {
  children: ReactNode;
  className?: string;
  disabled?: boolean;
  onClick: () => void;
  onLongPress: () => void;
}) {
  const longPressTimer = useRef<number | null>(null);
  const didLongPress = useRef(false);

  const clearTimer = () => {
    if (longPressTimer.current !== null) {
      window.clearTimeout(longPressTimer.current);
      longPressTimer.current = null;
    }
  };

  const start = () => {
    if (disabled) return;
    didLongPress.current = false;
    clearTimer();
    longPressTimer.current = window.setTimeout(() => {
      didLongPress.current = true;
      onLongPress();
      clearTimer();
    }, 750);
  };

  const end = () => {
    if (disabled) return;
    const wasLongPress = didLongPress.current;
    clearTimer();
    if (!wasLongPress) onClick();
  };

  return (
    <Button
      variant="secondary"
      className={className}
      disabled={disabled}
      onPointerDown={start}
      onPointerCancel={clearTimer}
      onPointerLeave={clearTimer}
      onPointerUp={end}
    >
      {children}
    </Button>
  );
}

function Field({ label, value, onChange }: { label: string; value: string; onChange: (value: string) => void }) {
  return (
    <label className="grid gap-1.5 text-sm font-semibold text-slate-700">
      {label}
      <input
        className="min-h-12 rounded-2xl border border-border bg-white px-4 text-sm font-medium text-slate-950 outline-none transition focus:border-brand focus:outline focus:outline-[3px] focus:outline-brand/30"
        value={value}
        onChange={(event) => onChange(event.target.value)}
      />
    </label>
  );
}

function normalizeBalanceMessage(data: unknown) {
  return String(data ?? "")
    .split(/\r?\n|\r/g)
    .map((line) => line.trim())
    .filter((line) => line.length > 0 && line !== "ATOM_READY")
    .map((line) => line.slice(0, MAX_BALANCE_LINE_CHARS))
    .slice(-MAX_STREAM_LINES)
    .reverse();
}

function formatSimulatedWeight(weight: number) {
  const sign = weight < 0 ? "-" : "+";
  const absolute = Math.abs(weight).toFixed(2).padStart(9, "0");
  return `ST,${sign}${absolute} g`;
}

function formatBalanceFrame(rawLine: string, settings: WeighingSettings) {
  let frame = applyDecimalSeparator(rawLine, settings.decimalSeparator);
  if (settings.trimIntegerLeadingZeros) {
    frame = trimIntegerLeadingZeros(frame);
  }
  if (!settings.printPlusSign) {
    frame = frame.replaceAll("+", "");
  }
  return maskFrame(frame, settings.maskStart, settings.maskEnd);
}

function trimIntegerLeadingZeros(value: string) {
  return value.replace(/[+-]?\d+(?:[.,]\d+)?/g, (match) => {
    const sign = match.startsWith("+") || match.startsWith("-") ? match[0] : "";
    const unsigned = sign ? match.slice(1) : match;
    const separatorMatch = unsigned.match(/[.,]/);
    const separatorIndex = separatorMatch?.index ?? -1;
    const integer = separatorIndex >= 0 ? unsigned.slice(0, separatorIndex) : unsigned;
    const decimal = separatorIndex >= 0 ? unsigned.slice(separatorIndex) : "";
    if (integer.length <= 1) return match;
    const trimmedInteger = integer.replace(/^0+(?=\d)/, "");
    return `${sign}${trimmedInteger || "0"}${decimal}`;
  });
}

function applyDecimalSeparator(value: string, separator: "." | ",") {
  return value.replace(/(\d)[.,](\d)/g, `$1${separator}$2`);
}

function maskFrame(value: string, maskStart: number, maskEnd: number) {
  const chars = [...value];
  const start = clampNumber(Math.floor(maskStart || 0), 0, chars.length);
  const end = clampNumber(Math.floor(maskEnd || 0), 0, chars.length - start);
  return chars.slice(start, chars.length - end).join("").trim();
}

function clampNumber(value: number, min: number, max: number) {
  if (!Number.isFinite(value)) return min;
  return Math.min(max, Math.max(min, value));
}

function parseWeightForTicket(rawLine: string) {
  const match = rawLine.match(/[+-]?\d+(?:[.,]\d+)?\s*(g|kg|mg)?/i);
  if (!match) return rawLine || "-";
  const unit = match[1] ?? "g";
  const number = match[0].replace(/[^\d.,+-]/g, "");
  return `${number} ${unit}`;
}

function addCommentToTicketTemplate(template: WeighingTicketTemplate): WeighingTicketTemplate {
  const insertAfterSample = <T,>(items: T[], commentItem: T, isSample: (item: T) => boolean, isComment: (item: T) => boolean) => {
    if (items.some(isComment)) return items;
    const nextItems = [...items];
    const sampleIndex = nextItems.findIndex(isSample);
    nextItems.splice(sampleIndex >= 0 ? sampleIndex + 1 : nextItems.length, 0, commentItem);
    return nextItems;
  };

  const commentLine: TicketLine = {
    id: `line-comment-${Math.random().toString(36).slice(2, 9)}`,
    label: "Commentaire",
    source: "comment",
    enabled: true,
    bold: false,
    fontSize: "normal",
    valueAlign: "right"
  };

  return {
    ...template,
    fields: insertAfterSample<TicketField>(template.fields, "comment", (field) => field === "sampleId", (field) => field === "comment"),
    lines: template.lines
      ? insertAfterSample<TicketLine>(template.lines, commentLine, (line) => line.source === "sampleId", (line) => line.source === "comment")
      : template.lines
  };
}

function parseWeightMeasurement(weight: string) {
  const match = weight.match(/([+-]?\d+(?:[.,]\d+)?)\s*(mg|kg|g)?/i);
  if (!match) return null;
  const rawNumber = match[1] ?? "";
  const normalizedNumber = rawNumber.replace(",", ".");
  const value = Number(normalizedNumber);
  if (!Number.isFinite(value)) return null;
  const unit = (match[2] ?? "g").toLowerCase();
  const decimals = rawNumber.includes(".") || rawNumber.includes(",") ? rawNumber.split(/[.,]/)[1]?.length ?? 0 : 0;
  const separator: "." | "," = rawNumber.includes(",") ? "," : ".";
  return { decimals, separator, unit, value };
}

function formatWeightMeasurement(value: number, unit: string, decimals: number, separator: "." | ",") {
  const rounded = value.toFixed(Math.max(0, decimals));
  const text = decimals > 0 ? rounded.replace(".", separator) : rounded;
  return `${text} ${unit}`;
}

function parseDeviceMessage(data: unknown): DetectedDevice | null {
  const raw = String(data ?? "").trim();
  if (!raw.startsWith("{")) return null;
  try {
    const payload = JSON.parse(raw) as Partial<DetectedDevice> & { type?: string; kind?: string; brand?: string; brandName?: string };
    if (payload.type !== "device" || payload.kind !== "balance") return null;
    const serialNumber = normalizeMetadata(payload.serialNumber);
    const deviceId = normalizeMetadata(payload.deviceId);
    const model = normalizeMetadata(payload.model) || "Balance";
    const brand = payload.brand
      ? balanceBrands.find((b) => b.id === payload.brand) ?? inferBrand({ model, name: payload.name || "", serialNumber, deviceId })
      : inferBrand({ model, name: payload.name || "", serialNumber, deviceId });
    return {
      id: serialNumber || deviceId || model,
      name: payload.name || `${brand.name} ${model}`,
      model,
      serialNumber,
      deviceId,
      brandId: brand.id,
      brandName: brand.name,
      brandLogo: brand.logo,
      photo: equipment[0].image,
      transport: payload.transport || "Connexion locale",
      ipAddress: payload.ipAddress || "192.168.4.1",
      serial: payload.serial || "Configuration standard"
    };
  } catch {
    return null;
  }
}

function requestDeviceInfo(socket: WebSocket | null) {
  if (socket?.readyState !== WebSocket.OPEN) return;
  socket.send(JSON.stringify({ type: "command", command: "device-info" }));
}

function createFallbackDevice(): DetectedDevice {
  return {
    id: "unknown-balance",
    name: "Balance",
    model: "Balance",
    serialNumber: "",
    deviceId: "",
    photo: equipment[0].image,
    transport: "",
    ipAddress: "",
    serial: ""
  };
}

function mergeDetectedDevice(current: DetectedDevice | null, next: DetectedDevice) {
  if (!current) return next;
  return {
    ...current,
    ...next,
    model: next.model || current.model,
    serialNumber: next.serialNumber || current.serialNumber,
    deviceId: next.deviceId || current.deviceId,
    brandId: next.brandId || current.brandId,
    brandName: next.brandName || current.brandName,
    brandLogo: next.brandLogo || current.brandLogo,
    photo: next.photo || current.photo,
    id: next.serialNumber || next.deviceId || current.id
  };
}

function upsertDevice(devices: DetectedDevice[], device: DetectedDevice) {
  const normalized = withBrandDefaults({
    ...device,
    name: device.name || device.model || "Balance"
  });
  const exists = devices.some((saved) => saved.id === normalized.id);
  if (!exists) return [normalized, ...devices];
  return devices.map((saved) => (saved.id === normalized.id ? { ...saved, ...normalized, name: saved.name || normalized.name } : saved));
}

function inferBrand(device: Pick<DetectedDevice, "model" | "name" | "serialNumber" | "deviceId">) {
  const source = `${device.name} ${device.model} ${device.serialNumber} ${device.deviceId}`.toLowerCase();
  if (source.includes("mettler") || source.includes("toledo")) return balanceBrands[1];
  if (source.includes("ohaus")) return balanceBrands[2];
  if (source.includes("precia") || source.includes("molen")) return balanceBrands[3];
  if (source.includes("precisa")) return balanceBrands[4];
  if (source.includes("sartorius")) return balanceBrands[5];
  if (source.includes("shimadzu")) return balanceBrands[6];
  if (source.includes("kern")) return balanceBrands[7];
  if (source.includes("bizerba")) return balanceBrands[8];
  if (source.includes("dini") || source.includes("argeo")) return balanceBrands[9];
  return balanceBrands[0];
}

function withBrandDefaults(device: DetectedDevice) {
  const brand = device.brandId ? balanceBrands.find((item) => item.id === device.brandId) ?? inferBrand(device) : inferBrand(device);
  return {
    ...device,
    brandId: device.brandId ?? brand.id,
    brandName: device.brandName ?? brand.name,
    brandLogo: device.brandLogo ?? brand.logo,
    photo: device.photo || equipment[0].image
  };
}

function normalizeMetadata(value: unknown) {
  const normalized = String(value ?? "")
    .trim()
    .replace(/^(TN|SN|ID)\s*[,;:=]?\s*/i, "")
    .trim();
  return normalized === "-" ? "" : normalized;
}

function useLocalState(key: string, initialValue: string) {
  const [value, setValue] = useState(() => window.localStorage.getItem(key) ?? initialValue);
  const update = (nextValue: string) => {
    setValue(nextValue);
    window.localStorage.setItem(key, nextValue);
  };
  return [value, update] as const;
}

function useLocalBoolean(key: string, initialValue: boolean) {
  const [value, setValue] = useState(() => {
    const stored = window.localStorage.getItem(key);
    return stored ? stored === "true" : initialValue;
  });
  const update = (nextValue: boolean) => {
    setValue(nextValue);
    window.localStorage.setItem(key, String(nextValue));
  };
  return [value, update] as const;
}

function useLocalObject<T>(key: string, initialValue: T) {
  const [value, setValue] = useState<T>(() => {
    const stored = window.localStorage.getItem(key);
    if (!stored) return initialValue;
    try {
      return JSON.parse(stored) as T;
    } catch {
      return initialValue;
    }
  });
  const update = (nextValue: T) => {
    setValue(nextValue);
    window.localStorage.setItem(key, JSON.stringify(nextValue));
  };
  return [value, update] as const;
}

function persistHistory(history: WeighingRecord[]) {
  window.localStorage.setItem("labconnect.weighing-history", JSON.stringify(history));
}

function formatDateTime(date: Date) {
  return new Intl.DateTimeFormat("fr-FR", {
    dateStyle: "short",
    timeStyle: "short"
  }).format(date);
}

function csvCell(value: string) {
  return `"${value.replaceAll("\"", "\"\"")}"`;
}

function downloadFile(filename: string, content: string, type: string) {
  const blob = new Blob([content], { type });
  const url = URL.createObjectURL(blob);
  const link = document.createElement("a");
  link.href = url;
  link.download = filename;
  link.click();
  URL.revokeObjectURL(url);
}

function buildPrintableHistory(records: WeighingRecord[], template: WeighingTicketTemplate) {
  const rows = records.map((record) => `
    <tr>
      <td>${record.dateTime}</td>
      <td>${record.operator}</td>
      <td>${record.lotNumber}</td>
      <td>${record.sampleId}</td>
      <td><strong>${record.rawBalanceLine}</strong></td>
    </tr>
  `).join("");
  return `
    <!doctype html>
    <html lang="fr">
      <head>
        <meta charset="utf-8" />
        <title>${template.companyName} - historique pesées</title>
        <style>
          body { font-family: Arial, sans-serif; padding: 24px; color: #0f172a; }
          h1 { font-size: 22px; margin: 0 0 4px; }
          p { margin: 0 0 18px; color: #475569; }
          table { width: 100%; border-collapse: collapse; font-size: 12px; }
          th, td { border-bottom: 1px solid #e5e7eb; padding: 8px; text-align: left; }
        </style>
      </head>
      <body>
        <h1>${template.companyName}</h1>
        <p>Historique local des pesées</p>
        <table>
          <thead><tr><th>Date</th><th>Opérateur</th><th>Lot</th><th>Échantillon</th><th>Ligne brute</th></tr></thead>
          <tbody>${rows}</tbody>
        </table>
      </body>
    </html>
  `;
}

function buildPrintableTicket(record: WeighingRecord, template: WeighingTicketTemplate) {
  const rows = [
    ["Date", record.dateTime],
    ["Opérateur", record.operator],
    ["Lot", record.lotNumber],
    ["Échantillon", record.sampleId],
    ["Commentaire", record.comment ?? ""],
    ["Balance", record.balanceName],
    ["Ligne brute", record.rawBalanceLine],
    ["Brut", record.grossWeight ?? ""],
    ["Tare", record.tareWeight ?? ""],
    ["Net", record.netWeight ?? record.weight]
  ].filter(([, value]) => value);

  return `
    <!doctype html>
    <html lang="fr">
      <head>
        <meta charset="utf-8" />
        <title>${escapeHtml(template.companyName)} - ${escapeHtml(record.id)}</title>
        <style>
          body { font-family: Arial, sans-serif; padding: 24px; color: #0f172a; }
          main { max-width: 360px; margin: 0 auto; border: 1px solid #e5e7eb; padding: 24px; }
          h1 { font-size: 20px; text-align: center; margin: 0; }
          h2 { font-size: 15px; text-align: center; margin: 6px 0 18px; font-weight: 500; }
          dl { display: grid; gap: 8px; font-size: 13px; }
          div { display: flex; justify-content: space-between; gap: 18px; border-bottom: 1px solid #f1f5f9; padding-bottom: 6px; }
          dt { color: #64748b; }
          dd { margin: 0; text-align: right; font-weight: 700; }
          .weight { font-family: monospace; font-size: 20px; }
        </style>
      </head>
      <body>
        <main>
          <h1>${escapeHtml(template.companyName)}</h1>
          <h2>${escapeHtml(template.title)}</h2>
          <dl>
            ${rows.map(([label, value]) => `<div><dt>${escapeHtml(label)}</dt><dd class="${label === "Ligne brute" ? "weight" : ""}">${escapeHtml(value)}</dd></div>`).join("")}
          </dl>
        </main>
      </body>
    </html>
  `;
}

function escapeHtml(value: string) {
  return value
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll("\"", "&quot;")
    .replaceAll("'", "&#039;");
}

function useTerminalLayout(terminalFormat: TerminalFormat): { isPortrait: boolean; format: "v3" | "v3mix" } {
  const [isPortraitWindow, setIsPortraitWindow] = useState(() => window.innerHeight > window.innerWidth);

  useEffect(() => {
    const onResize = () => setIsPortraitWindow(window.innerHeight > window.innerWidth);
    window.addEventListener("resize", onResize);
    return () => window.removeEventListener("resize", onResize);
  }, []);

  if (terminalFormat === "v3") return { isPortrait: true, format: "v3" };
  if (terminalFormat === "v3mix") return { isPortrait: false, format: "v3mix" };
  return { isPortrait: isPortraitWindow, format: isPortraitWindow ? "v3" : "v3mix" };
}
