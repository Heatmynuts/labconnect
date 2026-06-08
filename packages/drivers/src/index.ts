export type DeviceAction = "tare" | "zero" | "print" | "capture";

export type DeviceDriver = {
  id: string;
  model: string;
  actions: DeviceAction[];
};

export const andBalanceDriver: DeviceDriver = {
  id: "and-balance-driver",
  model: "GX-603A",
  actions: ["tare", "zero", "print", "capture"]
};
