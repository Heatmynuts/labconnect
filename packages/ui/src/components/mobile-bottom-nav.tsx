import { Grid2X2, Home, Map, Plus, Wifi } from "lucide-react";
import { cn } from "../lib/cn";

const items = [
  { label: "Accueil", icon: Home, active: true },
  { label: "Groupes", icon: Grid2X2 },
  { label: "Ajouter", icon: Plus, action: true },
  { label: "Plan", icon: Map },
  { label: "Liaisons", icon: Wifi }
];

export function MobileBottomNav({ onAddDevice }: { onAddDevice: () => void }) {
  return (
    <nav className="fixed inset-x-0 bottom-0 z-30 border-t border-border bg-white/92 px-2 pb-[calc(env(safe-area-inset-bottom)+8px)] pt-2 backdrop-blur-xl lg:hidden">
      <div className="mx-auto grid max-w-md grid-cols-5 gap-1">
        {items.map((item) => (
          <button
            type="button"
            key={item.label}
            onClick={item.action ? onAddDevice : undefined}
            className={cn(
              "flex min-h-[52px] flex-col items-center justify-center gap-1 rounded-2xl text-[11px] font-semibold text-slate-500",
              item.active && "text-brand",
              item.action && "bg-slate-950 text-white shadow-card"
            )}
          >
            <item.icon className="h-5 w-5" />
            <span>{item.label}</span>
          </button>
        ))}
      </div>
    </nav>
  );
}
