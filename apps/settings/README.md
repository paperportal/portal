# Paper Portal Settings App

This app is embedded in the Paper Portal Runner and is used to configure the Paper Portal.

## Build (writes `main/embedded/settings.wasm`)

From the repo root: `zig build`

## Naming conventions (Zig)

Use `lowerCamelCase` for all functions, except functions that return a type (Zig), which use `PascalCase`.
