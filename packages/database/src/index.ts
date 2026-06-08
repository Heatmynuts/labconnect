export type LocalEntity = {
  id: string;
  updatedAt: string;
  syncState: "local" | "synced" | "conflict";
};
