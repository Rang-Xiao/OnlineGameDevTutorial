# 第三方依赖清单

仓库不包含 `Demos/Thirdparty/` 下的第三方库（预编译 `.lib`/`.dll` 体积过大，`libprotobuf.lib` 单文件 112 MB 超过 GitHub 100 MB 限制）。
克隆后需按下表自行下载并放到对应目录，工程即可编译。

编译配置：Visual Studio，x64 Debug/Release。

## 依赖列表

| 库 | 版本 | 用途 | 获取地址 |
|---|---|---|---|
| SDL2 | 2.30.2 | 窗口 / 渲染 / 输入 | https://github.com/libsdl-org/SDL/releases （SDL2-devel-x.y.z-VC.zip） |
| SDL2_image | 2.8.2 | 图片加载 | https://github.com/libsdl-org/SDL_image/releases （SDL2_image-devel-x.y.z-VC.zip） |
| SDL2_net | 2.2.0 | TCP/UDP 网络 | https://github.com/libsdl-org/SDL_net/releases （SDL2_net-devel-x.y.z-VC.zip） |
| ENet | 1.3.18 | 可靠 UDP 网络 | https://github.com/lsalzman/enet/releases |
| cJSON | 1.7.17 | JSON 序列化 | https://github.com/DaveGamble/cJSON/releases |
| msgpack-cxx | 7.0.0 | MessagePack 序列化 | https://github.com/msgpack/msgpack-c/releases |
| Protobuf | 3.21.12 | Protobuf 序列化 | https://github.com/protocolbuffers/protobuf/releases |
| Lua | 5.4.7 | 脚本 | https://www.lua.org/download.html |
| Dear ImGui | 1.91.5 | 调试 UI | https://github.com/ocornut/imgui/releases |
| spdlog | 1.16.0 | 日志 | https://github.com/gabime/spdlog/releases |

Protobuf 另需 `protoc.exe` 生成 `.pb.cc` / `.pb.h`。

## 目录结构

工程按下列相对路径查找头文件与库，请保持一致：

```
Demos/Thirdparty/
├── SDL2/          include/*.h              lib/x64/{SDL2.lib, SDL2main.lib, SDL2.dll}
├── SDL2_image/    include/SDL_image.h      lib/x64/{SDL2_image.lib, SDL2_image.dll}
├── SDL2_net/      include/SDL_net.h        lib/x64/{SDL2_net.lib, SDL2_net.dll}
├── enet/          include/enet/*.h         lib/x64/enet.lib
├── Lua/           include/*.h              lib/x64/lua.lib
├── Protobuf/      include/google/...       lib/x64/{libprotobuf.lib, libprotobuf.dll, libprotoc.dll}
├── cJSON/         include/cJSON.h          cJSON.c
├── ImGUI/         include/*.h              imgui*.cpp
├── msgpack/       include/msgpack/...      （header-only）
└── spdlog/        include/spdlog/...       （header-only）
```

`cJSON.c` 与 `ImGUI/imgui*.cpp`（`imgui.cpp`、`imgui_draw.cpp`、`imgui_tables.cpp`、`imgui_widgets.cpp`、`imgui_impl_sdl2.cpp`、`imgui_impl_sdlrenderer2.cpp`、`imgui_stdlib.cpp`）由 vcxproj 直接参与编译，必须存在。

ENet 与 Lua 官方不提供 VS 预编译库，需自行编译出 `enet.lib` / `lua.lib`。

## 各 Demo 依赖

| Demo | 链接的库 |
|---|---|
| Demo_1_Client / Demo_1_Server | Winsock（`ws2_32`） |
| Demo_2_Client / Demo_2_Server | SDL2, SDL2main, SDL2_net（Client 另含 ImGui 源码） |
| Demo_3_JSON | cJSON（源码编译） |
| Demo_3_MsgPack | msgpack-cxx（header-only） |
| Demo_3_Protobuf | libprotobuf |
| Demo_4_Client | SDL2, SDL2main, SDL2_net, SDL2_image, cJSON |
| Demo_4_Server | SDL2, SDL2main, SDL2_net, cJSON |
| Demo_5_Client / Demo_5_Server | enet, ws2_32, winmm |
| Demo_6 | — |
| Demo_7_Client | SDL2, SDL2main, enet, lua, ws2_32, winmm |
| Demo_7_Server | enet, ws2_32, winmm |

## 其它工具

- Godot 4.5 stable 源码：https://github.com/godotengine/godot/releases
- Tracy Profiler 0.13.1：https://github.com/wolfpld/tracy/releases
