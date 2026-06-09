import type { AtomS3BridgeSettings, Equipment, EquipmentGroup, WeighingRecord, WeighingTicketTemplate } from "@labconnect/shared-types";
import andFz5000i from "../assets/products/and-fz5000i.jpg";
import labconnectPrinterImage from "../assets/products/sunmi-v3mix-front-angle.png";

export const groups: EquipmentGroup[] = [
  { id: "weighing-station", name: "Poste de pesée", icon: "P", equipmentCount: 2, status: "operational" }
];

export const equipment: Equipment[] = [
  {
    id: "and-fz5000i-bal-01",
    name: "A&D FZ-5000i",
    manufacturer: "A&D",
    model: "FZ-5000i",
    type: "balance",
    serialNumber: "FZ5000I-001",
    customerId: "BAL-01",
    location: "Poste de pesée · Paillasse 1",
    groupId: "weighing-station",
    image: andFz5000i,
    status: "ready",
    primaryValue: "1248.52",
    primaryUnit: "brut",
    connection: {
      protocol: "wifi",
      status: "active",
      label: "Connexion balance"
    }
  },
  {
    id: "labconnect-printer-01",
    name: "Imprimante LabConnect",
    manufacturer: "LabConnect",
    model: "Print Station",
    type: "terminal",
    serialNumber: "LCP-2026-014",
    customerId: "PRN-01",
    location: "Poste de pesée · Impression tickets",
    groupId: "weighing-station",
    image: labconnectPrinterImage,
    status: "connected",
    primaryValue: "1",
    primaryUnit: "tickets",
    connection: {
      protocol: "wifi",
      status: "active",
      label: "Wi-Fi · LabConnect Print"
    },
    batteryLevel: 86
  }
];

export const activity = [
  { id: "a1", title: "Pesée capturée", detail: "ST,+001248.52 g", time: "10:42" },
  { id: "a2", title: "Ticket imprimé", detail: "Imprimante LabConnect · Ticket lot LC-2048", time: "10:41" },
  { id: "a3", title: "Connexion vérifiée", detail: "Balance prête", time: "10:39" }
];

export const standardTicketTemplate: WeighingTicketTemplate = {
  id: "standard-weighing-ticket",
  name: "Ticket de pesée standard",
  paperWidth: "80mm",
  companyName: "LabConnect",
  title: "TICKET DE PESÉE",
  fields: ["logo", "dateTime", "operator", "balanceName", "rawBalanceLine", "weight", "lotNumber", "sampleId", "comment", "signature", "qrCode"],
  footer: "Contrôle validé - conserver ce ticket avec le lot",
  copies: 1,
  logoEnabled: true,
  qrCodeEnabled: true
};

export const ticketTemplates: WeighingTicketTemplate[] = [
  standardTicketTemplate,
  {
    id: "traceable-net-weighing-ticket",
    name: "Pesée brut / tare / net",
    paperWidth: "80mm",
    companyName: "LabConnect",
    title: "BON DE PESÉE",
    fields: ["logo", "dateTime", "operator", "balanceName", "grossWeight", "tareWeight", "netWeight", "lotNumber", "sampleId", "comment", "signature", "qrCode"],
    footer: "Poids net validé - ticket à joindre au dossier de lot",
    copies: 1,
    logoEnabled: true,
    qrCodeEnabled: true
  },
  {
    id: "lot-release-weighing-ticket",
    name: "Contrôle de lot",
    paperWidth: "80mm",
    companyName: "LabConnect",
    title: "CONTRÔLE DE LOT",
    fields: ["companyName", "ticketTitle", "dateTime", "operator", "lotNumber", "sampleId", "comment", "balanceName", "rawBalanceLine", "resultStatus", "signature", "qrCode"],
    footer: "Conforme si la pesée correspond à la fiche de fabrication",
    copies: 2,
    logoEnabled: false,
    qrCodeEnabled: true
  },
  {
    id: "halogen-moisture-ticket",
    name: "Dessiccateur halogène",
    paperWidth: "80mm",
    companyName: "LabConnect",
    title: "ANALYSE D'HUMIDITÉ",
    fields: ["companyName", "ticketTitle", "dateTime", "operator", "sampleId", "methodName", "dryingTemperature", "dryingTime", "startWeight", "dryWeight", "moistureContent", "resultStatus", "signature"],
    footer: "Résultat exprimé en % humidité - méthode halogène",
    copies: 1,
    logoEnabled: false,
    qrCodeEnabled: false
  }
];

export const lastWeighingRecord: WeighingRecord = {
  id: "w-20260605-1042",
  dateTime: "05/06/2026 10:42",
  operator: "Opérateur 01",
  balanceName: "A&D FZ-5000i",
  rawBalanceLine: "ST,+001248.52 g",
  weight: "ST,+001248.52 g",
  unit: "g",
  lotNumber: "LC-2048",
  sampleId: "ECH-001",
  comment: "Contrôle routine",
  signature: "Signature opérateur",
  grossWeight: "1250.52 g",
  tareWeight: "2.00 g",
  netWeight: "1248.52 g",
  methodName: "Halogène 105°C",
  dryingTemperature: "105 °C",
  dryingTime: "08:30",
  startWeight: "5.234 g",
  dryWeight: "4.918 g",
  moistureContent: "6.04 %",
  resultStatus: "Conforme"
};

export const weighingHistory: WeighingRecord[] = [
  lastWeighingRecord,
  {
    id: "w-20260605-1036",
    dateTime: "05/06/2026 10:36",
    operator: "Opérateur 01",
    balanceName: "A&D FZ-5000i",
    rawBalanceLine: "ST,+000872.10 g",
    weight: "ST,+000872.10 g",
    unit: "g",
    lotNumber: "LC-2047",
    sampleId: "ECH-003",
    signature: "Signature opérateur"
  }
];

export const rawBalanceLines = [
  "ST,+001248.52 g",
  "US,+001248.48 g",
  "ST,+001248.52 g"
];

export const atomS3Bridge: AtomS3BridgeSettings = {
  id: "atoms3-balance-bridge",
  name: "Connexion balance",
  ipAddress: "192.168.4.1",
  port: 80,
  transport: "websocket",
  serial: {
    baudRate: 2400,
    dataBits: 7,
    parity: "even",
    stopBits: 1
  }
};
