export type SerialConfig = {
  baudRate: number;
  dataBits: 7 | 8;
  parity: "none" | "even" | "odd";
  stopBits: 1 | 2;
  terminator: "CR" | "LF" | "CRLF";
};

export type DeviceScanResult = {
  id: string;
  label: string;
  protocol: "bluetooth" | "wifi" | "usb" | "rs232";
  signal?: number;
};

export type BalanceSerialCommand = "tare" | "zero" | "print" | "request-weight";

export const balanceSerialCommandMap: Record<BalanceSerialCommand, "T" | "RZ" | "P" | "Q"> = {
  tare: "T",
  zero: "RZ",
  print: "P",
  "request-weight": "Q"
};

export type AtomS3BalanceBridge = {
  connect(): Promise<void>;
  disconnect(): Promise<void>;
  sendSerialCommand(command: BalanceSerialCommand): Promise<void>;
  onRawLine(callback: (line: string) => void): () => void;
};
