import type * as React from "react";
import { useState } from "react";
import { MobileBottomNav } from "./mobile-bottom-nav";
import { Sidebar } from "./sidebar";
import { Topbar } from "./topbar";
import { AddDeviceSheet } from "./add-device-sheet";

export function AppShell({ children }: { children: React.ReactNode }) {
  const [collapsed, setCollapsed] = useState(false);
  const [addOpen, setAddOpen] = useState(false);

  return (
    <div className="min-h-screen bg-background text-slate-950">
      <div className="flex min-h-screen">
        <Sidebar collapsed={collapsed} onToggle={() => setCollapsed((value) => !value)} onAddDevice={() => setAddOpen(true)} />
        <div className="min-w-0 flex-1 pb-24 lg:pb-0">
          <Topbar onAddDevice={() => setAddOpen(true)} />
          {children}
        </div>
      </div>
      <MobileBottomNav onAddDevice={() => setAddOpen(true)} />
      <AddDeviceSheet open={addOpen} onClose={() => setAddOpen(false)} />
    </div>
  );
}
