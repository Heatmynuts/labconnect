export type EquipmentStatus =
  | "online"
  | "connected"
  | "ready"
  | "running"
  | "standby"
  | "offline"
  | "error"
  | "maintenance"
  | "simulation";

export type EquipmentType =
  | "balance"
  | "printer"
  | "pump"
  | "terminal"
  | "chamber"
  | "sensor"
  | "controller"
  | "custom";

export type Protocol =
  | "bluetooth"
  | "wifi"
  | "usb"
  | "rs232"
  | "ethernet"
  | "tcpip"
  | "mqtt"
  | "modbus";

export type Equipment = {
  id: string;
  name: string;
  manufacturer: string;
  model: string;
  type: EquipmentType;
  serialNumber?: string;
  customerId?: string;
  location?: string;
  groupId?: string;
  image: string;
  status: EquipmentStatus;
  primaryValue?: string;
  primaryUnit?: string;
  connection?: {
    protocol: Protocol;
    status: "active" | "idle" | "error" | "offline";
    label?: string;
  };
  batteryLevel?: number;
};

export type EquipmentGroup = {
  id: string;
  name: string;
  icon: string;
  equipmentCount: number;
  status: "operational" | "warning" | "error" | "offline";
};

export type TicketField =
  | "logo"
  | "companyName"
  | "ticketTitle"
  | "dateTime"
  | "operator"
  | "balanceName"
  | "rawBalanceLine"
  | "weight"
  | "grossWeight"
  | "tareWeight"
  | "netWeight"
  | "unit"
  | "lotNumber"
  | "sampleId"
  | "comment"
  | "methodName"
  | "dryingTemperature"
  | "dryingTime"
  | "startWeight"
  | "dryWeight"
  | "moistureContent"
  | "resultStatus"
  | "signature"
  | "qrCode";

export type WeighingTicketTemplate = {
  id: string;
  name: string;
  paperWidth: "58mm" | "80mm";
  companyName: string;
  title: string;
  fields: TicketField[];
  lines?: TicketLine[];
  footer: string;
  copies: number;
  logoEnabled: boolean;
  qrCodeEnabled: boolean;
};

export type TicketLineSource =
  | TicketField
  | "command"
  | "text"
  | "blank";

export type TicketLine = {
  id: string;
  label: string;
  source: TicketLineSource;
  command?: string;
  text?: string;
  enabled: boolean;
  bold: boolean;
  fontSize: "small" | "normal" | "large";
  valueAlign: "left" | "right";
};

export type WeighingRecord = {
  id: string;
  dateTime: string;
  operator: string;
  balanceName: string;
  rawBalanceLine: string;
  weight: string;
  unit: string;
  lotNumber: string;
  sampleId: string;
  comment?: string;
  signature?: string;
  grossWeight?: string;
  tareWeight?: string;
  netWeight?: string;
  methodName?: string;
  dryingTemperature?: string;
  dryingTime?: string;
  startWeight?: string;
  dryWeight?: string;
  moistureContent?: string;
  resultStatus?: string;
  commandResponses?: Record<string, string>;
};

export type LabConnectTicketQr = {
  v: 1;
  id: string;
  dt: string;
  op: string;
  lot: string;
  sample: string;
  comment?: string;
  balance: string;
  raw: string;
  weight: string;
  gross?: string;
  tare?: string;
  net?: string;
  unit?: string;
};

export const LABCONNECT_TICKET_QR_PREFIX = "LCPT1:";

export function buildTicketQrPayload(record: WeighingRecord) {
  const payload: LabConnectTicketQr = {
    v: 1,
    id: record.id,
    dt: record.dateTime,
    op: record.operator,
    lot: record.lotNumber,
    sample: record.sampleId,
    comment: record.comment,
    balance: record.balanceName,
    raw: record.rawBalanceLine,
    weight: record.weight,
    gross: record.grossWeight,
    tare: record.tareWeight,
    net: record.netWeight,
    unit: record.unit
  };

  return `${LABCONNECT_TICKET_QR_PREFIX}${encodeBase64Url(JSON.stringify(payload))}`;
}

export function parseTicketQrPayload(value: string): LabConnectTicketQr | null {
  const trimmed = value.trim();
  if (!trimmed.startsWith(LABCONNECT_TICKET_QR_PREFIX)) return parseLegacyTicketQrPayload(trimmed);

  try {
    const json = decodeBase64Url(trimmed.slice(LABCONNECT_TICKET_QR_PREFIX.length));
    const payload = JSON.parse(json) as Partial<LabConnectTicketQr>;
    if (payload.v !== 1 || !payload.id || !payload.dt) return null;
    return {
      v: 1,
      id: String(payload.id),
      dt: String(payload.dt),
      op: String(payload.op ?? ""),
      lot: String(payload.lot ?? ""),
      sample: String(payload.sample ?? ""),
      comment: payload.comment ? String(payload.comment) : undefined,
      balance: String(payload.balance ?? ""),
      raw: String(payload.raw ?? ""),
      weight: String(payload.weight ?? payload.raw ?? ""),
      gross: payload.gross ? String(payload.gross) : undefined,
      tare: payload.tare ? String(payload.tare) : undefined,
      net: payload.net ? String(payload.net) : undefined,
      unit: payload.unit ? String(payload.unit) : undefined
    };
  } catch {
    return null;
  }
}

export function ticketQrToRecord(ticket: LabConnectTicketQr): WeighingRecord {
  return {
    id: ticket.id,
    dateTime: ticket.dt,
    operator: ticket.op,
    balanceName: ticket.balance,
    rawBalanceLine: ticket.raw,
    weight: ticket.weight,
    unit: ticket.unit ?? "g",
    lotNumber: ticket.lot,
    sampleId: ticket.sample,
    comment: ticket.comment,
    grossWeight: ticket.gross,
    tareWeight: ticket.tare,
    netWeight: ticket.net
  };
}

function parseLegacyTicketQrPayload(value: string): LabConnectTicketQr | null {
  if (!value.startsWith("LabConnect Print")) return null;
  const pairs = Object.fromEntries(
    value
      .split("|")
      .slice(1)
      .map((part) => {
        const [key, ...rest] = part.split("=");
        return [key, rest.join("=")];
      })
  );

  return {
    v: 1,
    id: `scan-${Date.now()}`,
    dt: pairs.Date ?? "",
    op: "",
    lot: pairs.Lot ?? "",
    sample: pairs.Echantillon ?? "",
    balance: "",
    raw: pairs.Poids ?? "",
    weight: pairs.Poids ?? "",
    unit: "g"
  };
}

function encodeBase64Url(value: string) {
  const bytes = new TextEncoder().encode(value);
  let binary = "";
  bytes.forEach((byte) => {
    binary += String.fromCharCode(byte);
  });
  return btoa(binary).replaceAll("+", "-").replaceAll("/", "_").replace(/=+$/, "");
}

function decodeBase64Url(value: string) {
  const padded = value.replaceAll("-", "+").replaceAll("_", "/").padEnd(Math.ceil(value.length / 4) * 4, "=");
  const binary = atob(padded);
  const bytes = Uint8Array.from(binary, (char) => char.charCodeAt(0));
  return new TextDecoder().decode(bytes);
}

export type AtomS3BridgeSettings = {
  id: string;
  name: string;
  ipAddress: string;
  port: number;
  transport: "http" | "websocket" | "mqtt";
  serial: {
    baudRate: number;
    dataBits: number;
    parity: "none" | "even" | "odd";
    stopBits: number;
  };
};

export type AddDeviceStep =
  | "method"
  | "bluetooth-search"
  | "qr-scan"
  | "manual-form"
  | "import-list"
  | "device-found"
  | "configure"
  | "success";

export const statusLabels: Record<EquipmentStatus, string> = {
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
