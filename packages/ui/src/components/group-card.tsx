import { ArrowUpRight, CheckCircle2, CircleAlert, Grid2X2 } from "lucide-react";
import type { EquipmentGroup } from "@labconnect/shared-types";
import { cn } from "../lib/cn";

const groupStatus = {
  operational: { label: "Opérationnel", className: "text-green-700 bg-success-soft", icon: CheckCircle2 },
  warning: { label: "Attention", className: "text-amber-700 bg-warning-soft", icon: CircleAlert },
  error: { label: "Erreur", className: "text-red-700 bg-danger-soft", icon: CircleAlert },
  offline: { label: "Hors ligne", className: "text-slate-600 bg-slate-100", icon: CircleAlert }
};

export type GroupCardProps = EquipmentGroup & {
  onClick: () => void;
};

export function GroupCard({ name, icon, equipmentCount, status, onClick }: GroupCardProps) {
  const meta = groupStatus[status];
  return (
    <button
      type="button"
      onClick={onClick}
      className="group min-h-[148px] rounded-lg border border-border bg-surface p-4 text-left shadow-card transition duration-180 ease-lab hover:-translate-y-0.5 hover:bg-surface-hover"
    >
      <div className="flex items-start justify-between gap-3">
        <div className="grid h-12 w-12 place-items-center rounded-2xl bg-slate-950 text-xl text-white">
          {icon || <Grid2X2 className="h-5 w-5" />}
        </div>
        <ArrowUpRight className="h-4 w-4 text-slate-400 transition group-hover:text-brand" />
      </div>
      <div className="mt-5">
        <h3 className="text-base font-semibold leading-[22px] text-slate-950">{name}</h3>
        <p className="mt-1 text-sm text-slate-500">{equipmentCount} {equipmentCount > 1 ? "équipements" : "équipement"}</p>
      </div>
      <span className={cn("mt-4 inline-flex items-center gap-1.5 rounded-full px-2.5 py-1 text-xs font-semibold", meta.className)}>
        <meta.icon className="h-3.5 w-3.5" />
        {meta.label}
      </span>
    </button>
  );
}
