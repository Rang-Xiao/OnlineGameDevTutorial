# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

This is course material for a multiplayer game development guide (联机游戏开发完全指南). The repository includes progressive coding examples that walk through building networked game applications from raw sockets to full game client-server architectures.

## Build & Run

- Open `课件示例/MultiplayerGameDevelopment.sln` in Visual Studio 2022
- Build with **Release/x64** configuration
- Individual `.vcxproj` projects can also be opened and built independently
- Third-party library packages are in `程序工具/` and must be extracted before use (referenced by the projects in `课件示例/Thirdparty/`)

## Project Architecture

### Directory Layout

```
引擎源码/          - Godot 4.5 engine source (reference), includes network architecture diagram
程序工具/          - Third-party library source packages (SDL2, enet, cJSON, protobuf, spdlog, etc.)
课件示例/          - Visual Studio solution with progressive course demos
  Thirdparty/      - Extracted third-party libraries used by the demos
  x64/Debug/       - Build output directory
联机游戏.md        - Course outline and chapter notes
```

### Demo Progression

The demos build concepts incrementally for networked game development:

1. **Demo_1** (`Demo_1_Server`, `Demo_1_Client`) — Raw Winsock2 TCP: `socket()` → `bind()` → `listen()` → `accept()` / `connect()` → `send()`/`recv()`. Single-file server and client, minimal error handling.

2. **Demo_2** (`Demo_2_Server`, `Demo_2_Client`) — TCP packet framing: addresses the sticky/split packet problem (粘包/拆包) by adding delimiters (`0x1E`) between messages.

3. **Demo_3** (`Demo_3_JSON`, `Demo_3_MsgPack`, `Demo_3_Protobuf`) — Serialization formats: standalone examples showing how to serialize/deserialize game data with JSON (cJSON), MessagePack, and Protocol Buffers (`.proto` → `protoc` → generated C++).

4. **Demo_4** (`Demo_4_Server`, `Demo_4_Client`) — First real game: SDL2 + SDL_net for TCP networking. Server-authoritative multiplayer with game logic loop (delta time), JSON-RPC style messaging (`{"f": "func_name", "p": {...}}`), per-client state (position, skin, movement), and periodic `sync` broadcasts of all player states. Uses delimiter-based packet framing with `0x1E`.

5. **Demo_5** (`Demo_5_Server`, `Demo_5_Client`) — Advanced multiplayer game features building on Demo_4 patterns.

6. **Demo_6** — Standalone spdlog logging library tutorial (not a game demo).

7. **Demo_7** (`Demo_7_Server`, `Demo_7_Client`, `Demo_7_Common`) — Advanced game with a shared `Demo_7_Common` library project containing MsgPack-based message struct definitions (e.g., `Script` struct with `MSGPACK_DEFINE` macro) shared between client and server.

### Key Patterns

- **RPC protocol**: JSON messages with `f` (function name) and `p` (params) fields. Server registers RPC handlers in `rpc_func_pool` (an `unordered_map<string, function>`); clients send RPC calls; server dispatches by function name.
- **Packet framing**: Use `0x1E` (ASCII Record Separator) as a delimiter appended to each message to handle TCP stream boundaries. Buffers accumulate received data and `process_rpc()` extracts complete messages by finding delimiters.
- **Server game loop**: Poll sockets with `SDLNet_CheckSockets()`, process RPCs, update game state with delta time, then broadcast state to all clients.
- **Server authority**: All game state (position, movement) lives on server; clients send input (move_left/move_right); server broadcasts authoritative state.

### Third-Party Libraries Used

| Library | Purpose |
|---------|---------|
| SDL2 + SDL_net | Multimedia & TCP networking abstraction |
| enet | UDP-based reliable networking |
| cJSON | JSON parsing/writing |
| msgpack-cxx | Binary serialization |
| Protocol Buffers | Structured data serialization |
| spdlog | Logging |
| ImGui | Debug/editor UI |
| Tracy | Performance profiling |
| Lua | Embedded scripting |
