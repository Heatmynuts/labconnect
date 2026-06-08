import { ChevronLeft, ChevronRight, Grid2X2, LayoutDashboard, Map, Plus, Settings2, Wifi } from "lucide-react";
import { Button } from "./button";
import { ModuleLogo } from "./module-logo";
import { cn } from "../lib/cn";

const nav = [
  { label: "Accueil", icon: LayoutDashboard, active: true },
  { label: "Groupes", icon: Grid2X2 },
  { label: "Plan", icon: Map },
  { label: "Connexions", icon: Wifi },
  { label: "Réglages", icon: Settings2 }
];

export function Sidebar({
  collapsed,
  onToggle,
  onAddDevice
}: {
  collapsed: boolean;
  onToggle: () => void;
  onAddDevice: () => void;
}) {
  return (
    <aside
      className={cn(
        "hidden h-screen shrink-0 border-r border-border bg-white/86 p-4 backdrop-blur-xl transition-[width] duration-280 ease-lab lg:flex lg:flex-col",
        collapsed ? "w-[72px]" : "w-[260px]"
      )}
    >
      <div className="flex items-center justify-between">
        <ModuleLogo compact={collapsed} />
        {!collapsed && (
          <Button aria-label="Replier la navigation" variant="ghost" size="icon" onClick={onToggle}>
            <ChevronLeft className="h-4 w-4" />
          </Button>
        )}
      </div>
      {collapsed && (
        <Button className="mt-3" aria-label="Déplier la navigation" variant="ghost" size="icon" onClick={onToggle}>
          <ChevronRight className="h-4 w-4" />
        </Button>
      )}
      <nav className="mt-8 space-y-1">
        {nav.map((item) => (
          <button
            className={cn(
              "flex min-h-11 w-full items-center gap-3 rounded-xl px-3 text-sm font-semibold text-slate-600 transition hover:bg-surface-hover",
              item.active && "bg-brand-soft text-brand",
              collapsed && "justify-center px-0"
            )}
            key={item.label}
            title={collapsed ? item.label : undefined}
            type="button"
          >
            <item.icon className="h-5 w-5" />
            {!collapsed && <span>{item.label}</span>}
          </button>
        ))}
      </nav>
      <div className="mt-auto">
        <Button className={cn("w-full", collapsed && "px-0")} size={collapsed ? "icon" : "md"} onClick={onAddDevice}>
          <Plus className="h-4 w-4" />
          {!collapsed && "Ajouter"}
        </Button>
      </div>
    </aside>
  );
}
