import { Plus, Search } from "lucide-react";
import { Button } from "./button";
import { ModuleLogo } from "./module-logo";

export function Topbar({ onAddDevice }: { onAddDevice: () => void }) {
  return (
    <header className="sticky top-0 z-20 flex min-h-[72px] items-center justify-between border-b border-border bg-background/88 px-4 backdrop-blur-xl lg:px-8">
      <div className="lg:hidden">
        <ModuleLogo />
      </div>
      <div className="hidden lg:block">
        <p className="text-sm font-semibold text-slate-500">Laboratoire Central</p>
        <h1 className="text-[32px] font-bold leading-10 tracking-normal text-slate-950">LabConnect Print</h1>
      </div>
      <div className="flex items-center gap-2">
        <Button variant="secondary" size="icon" aria-label="Rechercher">
          <Search className="h-4 w-4" />
        </Button>
        <Button className="hidden sm:inline-flex" onClick={onAddDevice}>
          <Plus className="h-4 w-4" />
          Ajouter
        </Button>
      </div>
    </header>
  );
}
