import { ArrowUpRight, Battery, Bluetooth, Wifi } from "lucide-react";
import type { Equipment, EquipmentStatus } from "@labconnect/shared-types";
import { StatusBadge } from "./status-badge";
import { ProductScreenPreview } from "./product-screen-preview";
import { cn } from "../lib/cn";

export type EquipmentCardProps = {
  id: string;
  name: string;
  type: string;
  model: string;
  image: string;
  status: EquipmentStatus;
  serialNumber?: string;
  customerId?: string;
  location?: string;
  primaryValue?: string;
  primaryUnit?: string;
  connectionType?: string;
  batteryLevel?: number;
  onClick: () => void;
};

export function EquipmentCard({
  name,
  type,
  model,
  image,
  status,
  serialNumber,
  customerId,
  location,
  primaryValue,
  primaryUnit,
  connectionType,
  batteryLevel,
  onClick
}: EquipmentCardProps) {
  return (
    <button
      type="button"
      onClick={onClick}
      className="group flex min-h-[300px] flex-col overflow-hidden rounded-lg border border-border bg-surface text-left shadow-card transition duration-180 ease-lab hover:-translate-y-0.5 hover:bg-surface-hover"
    >
      <div className="relative h-40 bg-gradient-to-b from-surface-soft to-white">
        <img className="h-full w-full object-contain p-4 transition duration-280 ease-lab group-hover:scale-[1.03]" src={image} alt={name} />
        {type.toLowerCase().includes("terminal") && (
          <div className="absolute bottom-3 left-3 origin-bottom-left scale-75">
            <ProductScreenPreview />
          </div>
        )}
        <div className="absolute right-3 top-3 rounded-full bg-white/90 p-2 shadow-card">
          <ArrowUpRight className="h-4 w-4 text-slate-500 group-hover:text-brand" />
        </div>
      </div>
      <div className="flex flex-1 flex-col p-4">
        <div className="flex items-start justify-between gap-3">
          <div className="min-w-0">
            <h3 className="truncate text-base font-semibold leading-[22px] text-slate-950">{name}</h3>
            <p className="mt-1 text-sm text-slate-500">{type} · {model}</p>
          </div>
          <StatusBadge status={status} />
        </div>
        {primaryValue && (
          <div className="mt-5 flex items-baseline gap-1">
            <span className="font-mono text-[40px] font-bold leading-[48px] tracking-normal text-slate-950">{primaryValue}</span>
            <span className="text-sm font-semibold text-slate-500">{primaryUnit}</span>
          </div>
        )}
        <div className="mt-auto grid gap-1.5 pt-4 text-xs font-medium text-slate-500">
          <p>{customerId} {serialNumber ? `· SN: ${serialNumber}` : ""}</p>
          <p>{location}</p>
          <div className="mt-2 flex items-center gap-3">
            <span className="inline-flex items-center gap-1">
              {connectionType === "Bluetooth" ? <Bluetooth className="h-3.5 w-3.5" /> : <Wifi className="h-3.5 w-3.5" />}
              {connectionType}
            </span>
            {batteryLevel ? (
              <span className="inline-flex items-center gap-1">
                <Battery className="h-3.5 w-3.5" />
                {batteryLevel}%
              </span>
            ) : null}
          </div>
        </div>
      </div>
    </button>
  );
}

export function equipmentToCardProps(equipment: Equipment): Omit<EquipmentCardProps, "onClick" | "connectionType"> & { connectionType?: string } {
  return {
    id: equipment.id,
    name: equipment.name,
    type: equipment.type === "balance" ? "Balance analytique" : equipment.type === "terminal" ? "Terminal mobile" : equipment.type,
    model: equipment.model,
    image: equipment.image,
    status: equipment.status,
    serialNumber: equipment.serialNumber,
    customerId: equipment.customerId,
    location: equipment.location,
    primaryValue: equipment.primaryValue,
    primaryUnit: equipment.primaryUnit,
    connectionType: equipment.connection?.protocol === "bluetooth" ? "Bluetooth" : equipment.connection?.protocol === "wifi" ? "Wi-Fi" : equipment.connection?.protocol,
    batteryLevel: equipment.batteryLevel
  };
}
