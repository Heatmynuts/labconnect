import { Radio, RotateCcw, Send, Wifi, Zap } from "lucide-react";
import type { AtomS3BridgeSettings } from "@labconnect/shared-types";
import { Button } from "./button";

export function RawBalanceStream({
  bridge,
  lines,
  onCapture
}: {
  bridge: AtomS3BridgeSettings;
  lines: string[];
  onCapture: () => void;
}) {
  return (
    <section className="rounded-lg border border-border bg-slate-950 p-4 text-white shadow-float">
      <div className="flex items-start justify-between gap-3">
        <div>
          <h2 className="text-base font-semibold">Flux balance brut</h2>
          <p className="mt-1 text-sm text-white/58">Aucun parsing : la ligne série est affichée telle quelle.</p>
        </div>
        <span className="inline-flex items-center gap-1.5 rounded-full bg-white/10 px-2.5 py-1 text-xs font-semibold text-white">
          <Wifi className="h-3.5 w-3.5" />
          Balance
        </span>
      </div>
      <div className="mt-4 grid gap-2 rounded-2xl bg-black/28 p-3 font-mono text-sm">
        {lines.map((line, index) => (
          <div key={`${line}-${index}`} className={index === 0 ? "text-green-300" : "text-white/72"}>
            {line}
          </div>
        ))}
      </div>
      <div className="mt-4 grid gap-2 text-xs font-medium text-white/64">
        <p>Connexion locale active</p>
        <p>Balance prête</p>
      </div>
      <div className="mt-4 grid grid-cols-2 gap-2">
        <Button className="bg-white text-slate-950 hover:bg-white/92" onClick={onCapture}>
          <Radio className="h-4 w-4" />
          Capturer
        </Button>
        <Button className="bg-white/10 text-white hover:bg-white/16">
          <Send className="h-4 w-4" />
          Demander
        </Button>
        <Button className="bg-white/10 text-white hover:bg-white/16">
          <RotateCcw className="h-4 w-4" />
          Tare
        </Button>
        <Button className="bg-white/10 text-white hover:bg-white/16">
          <Zap className="h-4 w-4" />
          Zéro
        </Button>
      </div>
    </section>
  );
}
