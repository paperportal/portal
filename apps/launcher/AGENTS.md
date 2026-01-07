# Instructions for AI agents

This Zig project implements an application launcher interface for Paper Portal.
It compiles to WebAssembly and is run in Paper Portal Runner.

For accessing the hardware and some services a Zig SDK is used from GitHub.
Source code of the SDK is available at /Users/mika/code/paperportal/zig-sdk so
you can see the available APIs.

## Naming conventions (Zig)

Use `lowerCamelCase` for all functions, except functions that return a type (Zig), which use `PascalCase`.

## Specifications

This section links to various spec files in the docs folder. The specs are used
to design all features of the project. This section must be always kept up to date.

- [Main launcher interface specification](./docs/spec-main-interface.md)
- [WebAssembly app packaging specification](./docs/spec-app-packaging.md)
- [Settings specification](./docs/spec-settings.md)
- [Web configuration utility specification](./docs/spec-web-configuration.md)
- [Development mode specification](./docs/spec-development-mode.md)
- [UI toolkit specification](./docs/spec-ui-toolkit.md)
