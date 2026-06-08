export const AD_FZ_5000I = {
  manufacturer: "A&D",
  model: "FZ-5000i",
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
} as const;

export const LABCONNECT_PRINTER = {
  manufacturer: "LabConnect",
  model: "Print Station",
  type: "terminal",
  defaultProtocol: "wifi",
  supportedProtocols: ["wifi", "bluetooth", "usb"],
  capabilities: ["capture", "thermal-print", "scan", "operator-workflow", "ticket-template"]
} as const;
