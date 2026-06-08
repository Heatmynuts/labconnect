import { Circle, CircleAlert, CircleCheck, Wrench } from "lucide-react";
import { EquipmentStatus, statusLabels } from "@labconnect/shared-types";
import { cn } from "../lib/cn";

const styles: Record<EquipmentStatus, string> = {
  online: "bg-success-soft text-green-700",
  connected: "bg-success-soft text-green-700",
  ready: "bg-success-soft text-green-700",
  running: "bg-info-soft text-cyan-700",
  standby: "bg-warning-soft text-amber-700",
  offline: "bg-slate-100 text-slate-600",
  error: "bg-danger-soft text-red-700",
  maintenance: "bg-warning-soft text-amber-700",
  simulation: "bg-brand-soft text-blue-700"
};

export function StatusBadge({ status, className }: { status: EquipmentStatus; className?: string }) {
  const Icon = status === "error" ? CircleAlert : status === "maintenance" ? Wrench : status === "offline" ? Circle : CircleCheck;
  return (
    <span className={cn("inline-flex items-center gap-1.5 rounded-full px-2.5 py-1 text-xs font-semibold", styles[status], className)}>
      <Icon aria-hidden className="h-3.5 w-3.5" />
      {statusLabels[status]}
    </span>
  );
}
