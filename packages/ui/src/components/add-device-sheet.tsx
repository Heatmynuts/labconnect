import { Bluetooth, Check, ChevronRight, FileSpreadsheet, HelpCircle, Keyboard, Loader2, QrCode, Radar, ScanLine, X } from "lucide-react";
import { useMemo, useState } from "react";
import type { AddDeviceStep } from "@labconnect/shared-types";
import { Button } from "./button";
import { cn } from "../lib/cn";

const methodOptions = [
  { step: "bluetooth-search", icon: Bluetooth, title: "Recherche Bluetooth", description: "Détecter une balance ou un terminal à proximité." },
  { step: "qr-scan", icon: QrCode, title: "Scanner un code QR", description: "Importer les informations depuis une étiquette." },
  { step: "manual-form", icon: Keyboard, title: "Saisir manuellement", description: "Renseigner le modèle, le numéro de série et le groupe." },
  { step: "import-list", icon: FileSpreadsheet, title: "Importer une liste", description: "Ajouter plusieurs équipements via CSV, XLSX ou JSON." }
] as const;

export function AddDeviceSheet({ open, onClose }: { open: boolean; onClose: () => void }) {
  const [step, setStep] = useState<AddDeviceStep>("method");
  const title = useMemo(() => {
    if (step === "bluetooth-search") return "Recherche Bluetooth";
    if (step === "device-found") return "1 équipement trouvé";
    if (step === "configure") return "Configurer l’équipement";
    if (step === "success") return "Équipement ajouté";
    if (step === "qr-scan") return "Scanner un code QR";
    if (step === "manual-form") return "Saisir manuellement";
    if (step === "import-list") return "Importer une liste";
    return "Ajouter un équipement";
  }, [step]);

  if (!open) return null;

  return (
    <div className="fixed inset-0 z-50 flex items-end justify-center bg-slate-950/36 p-0 backdrop-blur-[2px] lg:items-center lg:p-6" role="dialog" aria-modal="true" aria-labelledby="add-device-title">
      <div className="max-h-[92vh] w-full max-w-2xl overflow-hidden rounded-t-[28px] bg-white shadow-sheet animate-sheet-in lg:rounded-[28px]">
        <div className="flex items-center justify-center pt-3 lg:hidden">
          <div className="h-1.5 w-12 rounded-full bg-slate-200" />
        </div>
        <header className="flex items-center justify-between gap-3 border-b border-border px-5 py-4">
          <div>
            <h2 id="add-device-title" className="text-[22px] font-bold leading-[30px] tracking-normal text-slate-950">{title}</h2>
            {step === "method" && <p className="mt-1 text-sm text-slate-500">Comment souhaitez-vous ajouter un nouvel équipement ?</p>}
          </div>
          <Button aria-label="Fermer" variant="ghost" size="icon" onClick={onClose}>
            <X className="h-5 w-5" />
          </Button>
        </header>
        <div className="max-h-[calc(92vh-92px)] overflow-y-auto px-5 py-5">
          {step === "method" && <MethodView onStep={setStep} />}
          {step === "bluetooth-search" && <BluetoothSearchView onFound={() => setStep("device-found")} />}
          {step === "device-found" && <DeviceFoundView onConfigure={() => setStep("configure")} />}
          {step === "configure" && <ConfigureView onSuccess={() => setStep("success")} />}
          {step === "success" && <SuccessView onDone={onClose} onAnother={() => setStep("method")} />}
          {step === "qr-scan" && <QRCodeScannerView />}
          {step === "manual-form" && <ManualDeviceForm onSuccess={() => setStep("success")} />}
          {step === "import-list" && <ImportDeviceList />}
        </div>
      </div>
    </div>
  );
}

function MethodView({ onStep }: { onStep: (step: AddDeviceStep) => void }) {
  return (
    <div className="space-y-2">
      {methodOptions.map((option) => (
        <button
          key={option.title}
          type="button"
          onClick={() => onStep(option.step)}
          className="flex min-h-[72px] w-full items-center gap-4 rounded-2xl border border-border bg-white p-3 text-left transition hover:bg-surface-hover"
        >
          <span className="grid h-12 w-12 shrink-0 place-items-center rounded-2xl bg-brand-soft text-brand">
            <option.icon className="h-5 w-5" />
          </span>
          <span className="min-w-0 flex-1">
            <span className="block text-sm font-semibold text-slate-950">{option.title}</span>
            <span className="mt-0.5 block text-sm text-slate-500">{option.description}</span>
          </span>
          <ChevronRight className="h-5 w-5 shrink-0 text-slate-400" />
        </button>
      ))}
    </div>
  );
}

export function BluetoothSearchView({ onFound }: { onFound?: () => void }) {
  return (
    <div className="grid gap-6">
      <div className="relative mx-auto grid h-64 w-64 place-items-center">
        <div className="absolute h-44 w-44 rounded-full border border-brand/20 animate-bluetooth-pulse" />
        <div className="absolute h-32 w-32 rounded-full border border-brand/30 animate-bluetooth-pulse [animation-delay:240ms]" />
        <div className="grid h-24 w-24 place-items-center rounded-full bg-brand text-white shadow-float">
          <Bluetooth className="h-10 w-10" />
        </div>
        <DeviceDot className="left-2 top-16" label="GX" />
        <DeviceDot className="right-4 top-28" label="LC" />
        <DeviceDot className="bottom-8 left-20" label="?" muted />
      </div>
      <div className="text-center">
        <div className="inline-flex items-center gap-2 rounded-full bg-brand-soft px-3 py-1 text-sm font-semibold text-brand">
          <Loader2 className="h-4 w-4 animate-spin" />
          Recherche en cours...
        </div>
        <p className="mx-auto mt-3 max-w-sm text-sm text-slate-500">Assurez-vous que l’équipement est allumé et en mode appairage.</p>
      </div>
      <div className="flex gap-2">
        <Button className="flex-1" variant="secondary">
          <HelpCircle className="h-4 w-4" />
          Besoin d’aide ?
        </Button>
        <Button className="flex-1" onClick={onFound}>
          <Radar className="h-4 w-4" />
          Simuler trouvé
        </Button>
      </div>
    </div>
  );
}

function DeviceDot({ className, label, muted }: { className?: string; label: string; muted?: boolean }) {
  return (
    <div className={cn("absolute grid h-12 w-12 place-items-center rounded-2xl text-xs font-bold shadow-card", muted ? "bg-slate-100 text-slate-500" : "bg-white text-brand", className)}>
      {label}
    </div>
  );
}

function DeviceFoundView({ onConfigure }: { onConfigure: () => void }) {
  return (
    <div className="space-y-4">
      <div className="rounded-lg border border-border bg-surface-soft p-4">
        <p className="text-base font-semibold text-slate-950">A&D GX-603A</p>
        <p className="mt-1 text-sm text-slate-500">Balance analytique</p>
        <dl className="mt-4 grid gap-2 text-sm">
          <Row label="ID Bluetooth" value="GX603A_83051248" />
          <Row label="Numéro de série" value="83051248" />
          <Row label="Firmware" value="1.2.7" />
        </dl>
      </div>
      <Button className="w-full" size="lg" onClick={onConfigure}>Ajouter cet équipement</Button>
      <Button className="w-full" variant="secondary">Je ne le vois pas</Button>
    </div>
  );
}

function ConfigureView({ onSuccess }: { onSuccess: () => void }) {
  return (
    <div className="space-y-4">
      <div className="rounded-lg border border-brand/20 bg-brand-soft p-4">
        <p className="text-sm font-semibold text-brand">A&D GX-603A</p>
        <p className="mt-1 text-sm text-slate-600">SN: 83051248 · Détecté en Bluetooth</p>
      </div>
      <Field label="Affecter à" value="Salle de pesée" />
      <Field label="Nom de l’équipement" value="A&D GX-603A" />
      <Field label="ID client" value="BAL-01" />
      <Field label="Localisation" value="Paillasse 2" />
      <Button className="w-full" size="lg" onClick={onSuccess}>Ajouter à la salle de pesée</Button>
    </div>
  );
}

function SuccessView({ onDone, onAnother }: { onDone: () => void; onAnother: () => void }) {
  return (
    <div className="space-y-4 text-center">
      <div className="mx-auto grid h-20 w-20 place-items-center rounded-full bg-success-soft text-green-700">
        <Check className="h-9 w-9" />
      </div>
      <p className="text-sm text-slate-600">A&D GX-603A a été ajouté à Salle de pesée.</p>
      <Button className="w-full" size="lg">Voir l’équipement</Button>
      <Button className="w-full" variant="secondary" onClick={onAnother}>Ajouter un autre équipement</Button>
      <Button className="w-full" variant="ghost" onClick={onDone}>Terminer</Button>
    </div>
  );
}

export function QRCodeScannerView() {
  return (
    <div className="grid gap-4">
      <div className="grid aspect-square place-items-center rounded-[24px] border border-dashed border-border-strong bg-surface-soft">
        <ScanLine className="h-16 w-16 text-brand" />
      </div>
      <p className="text-center text-sm text-slate-500">Le scan récupérera modèle, numéro de série, ID client, protocole et configuration de base.</p>
    </div>
  );
}

export function ManualDeviceForm({ onSuccess }: { onSuccess?: () => void }) {
  return (
    <div className="space-y-3">
      {["Type d’équipement", "Fabricant", "Modèle", "Numéro de série", "ID client", "Protocole", "Groupe", "Localisation"].map((label) => (
        <Field key={label} label={label} value="" placeholder={label} />
      ))}
      <Button className="w-full" size="lg" onClick={onSuccess}>Ajouter l’équipement</Button>
    </div>
  );
}

export function ImportDeviceList() {
  return (
    <div className="grid gap-4">
      <div className="rounded-[24px] border border-dashed border-border-strong bg-surface-soft p-8 text-center">
        <FileSpreadsheet className="mx-auto h-12 w-12 text-brand" />
        <p className="mt-3 text-sm font-semibold text-slate-950">Déposer un fichier CSV, XLSX ou JSON</p>
        <p className="mt-1 text-sm text-slate-500">Mode prévu pour les installations avec beaucoup d’équipements.</p>
      </div>
      <Button variant="secondary">Choisir un fichier</Button>
    </div>
  );
}

function Field({ label, value, placeholder }: { label: string; value: string; placeholder?: string }) {
  return (
    <label className="grid gap-1.5 text-sm font-semibold text-slate-700">
      {label}
      <input
        className="min-h-[52px] rounded-2xl border border-border bg-white px-4 text-sm font-medium text-slate-950 outline-none transition focus:border-brand focus:outline focus:outline-[3px] focus:outline-brand/30"
        defaultValue={value}
        placeholder={placeholder}
      />
    </label>
  );
}

function Row({ label, value }: { label: string; value: string }) {
  return (
    <div className="flex justify-between gap-4">
      <dt className="text-slate-500">{label}</dt>
      <dd className="font-semibold text-slate-900">{value}</dd>
    </div>
  );
}
