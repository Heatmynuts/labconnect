# LabConnect Architecture

LabConnect uses a monorepo with a single React interface shared by desktop, mobile, web and Tauri targets.

```text
apps/labconnect-tauri  application shell, routes, Tauri integration
packages/ui            reusable LabConnect components
packages/design-system tokens, Tailwind preset, themes
packages/shared-types  shared TypeScript models
packages/devices       declarative device definitions
packages/protocols     protocol contracts
packages/drivers       driver boundaries
packages/database      local persistence boundary
packages/api-client    API clients
packages/utils         shared utilities
```

React components only render state and trigger actions. Device communication, protocol parsing, native integrations and persistence live behind dedicated package boundaries.
