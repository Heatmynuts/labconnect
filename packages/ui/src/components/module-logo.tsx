import { BrandMark } from "./brand-mark";

export function ModuleLogo({ compact = false }: { compact?: boolean }) {
  return (
    <div className="flex min-w-0 items-center gap-3">
      <BrandMark />
      {!compact && (
        <div className="min-w-0">
          <p className="truncate text-[17px] font-bold leading-5 tracking-normal text-slate-950">LabConnect</p>
          <p className="truncate text-xs font-medium text-slate-500">Print</p>
        </div>
      )}
    </div>
  );
}
