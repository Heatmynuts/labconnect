import { cn } from "../lib/cn";

export function BrandMark({ className }: { className?: string }) {
  return (
    <div className={cn("relative grid h-10 w-10 place-items-center rounded-[14px] bg-slate-950 text-white shadow-card", className)} aria-hidden>
      <div className="absolute left-2 top-2 h-2 w-2 rounded-full bg-brand" />
      <div className="absolute right-2 top-2.5 h-2 w-2 rounded-full bg-cyan-400" />
      <div className="absolute bottom-2 left-1/2 h-2 w-2 -translate-x-1/2 rounded-full bg-green-400" />
      <div className="h-px w-5 rotate-45 bg-white/45" />
      <div className="absolute h-px w-5 -rotate-45 bg-white/35" />
    </div>
  );
}
