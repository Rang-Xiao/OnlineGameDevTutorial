# 联机游戏

## 写在最前：课程Demo演进路线

```
Demo_1 ──→ Demo_2 ──→ Demo_3 ──→ Demo_4 ──→ Demo_5 ──→ Demo_7
 │           │           │           │           │           │
Raw TCP    聊天室     序列化      多人游戏    ENet        Lua热更新
Winsock2   粘包拆包   JSON/       JSON-RPC   可靠UDP     MsgPack
单客户端   多客户端   MsgPack/    服务端权威  channels   嵌入式脚本
阻塞I/O    ImGui UI  Protobuf    SDL渲染                无重启更新
                          
                    Demo_6: spdlog日志系统（独立示例）
```

**技术栈总览**

| 层级       | 技术                             | Demo            |
| ---------- | -------------------------------- | --------------- |
| 原始Socket | Winsock2                         | Demo_1          |
| TCP网络库  | SDL_net                          | Demo_2, Demo_4  |
| UDP网络库  | ENet                             | Demo_5, Demo_7  |
| 序列化     | JSON(cJSON) / MsgPack / Protobuf | Demo_2, 3, 4, 7 |
| 渲染/UI    | SDL2 + ImGui + SDL_image         | Demo_2, 4, 7    |
| 脚本       | Lua (C API)                      | Demo_7          |
| 日志       | spdlog                           | Demo_6          |
| 构建       | Visual Studio 2022, Release/x64  | 全部            |

## Chapter1 联机概念与通信基础

### TCP-IP模型

```
应用层    ← 用户进程（游戏客户端/服务器逻辑）
传输层    ← TCP/UDP（可靠/不可靠传输，端口寻址）
网络层    ← IP（路由寻址，尽力交付）
链路层    ← 以太网/WiFi（帧封装，MAC寻址）
物理层    ← 网线/光纤/无线电（比特流传输）
```

- **封装过程**：应用数据 → TCP段(segment) → IP包(packet) → 以太网帧(frame) → 物理信号
- **解封装过程**：物理信号 → 帧 → 包 → 段 → 应用数据（每层剥除对应头部）
- **TCP特性**
  - 面向连接：三次握手建立连接，四次挥手断开
  - 可靠传输：确认应答(ACK) + 超时重传 + 序号机制保证顺序
  - 流量控制：滑动窗口，接收方告知可用缓冲区大小
  - 拥塞控制：慢启动 → 拥塞避免 → 快速重传/快速恢复
  - 全双工通信：双方可同时收发
- **UDP特性**
  - 无连接：直接发送，无需建立连接
  - 不可靠：不保证送达、不保证顺序、不保证不重复
  - 低延迟：无握手开销，无重传等待
  - 面向报文：保留消息边界，无粘包问题
  - 适用场景：实时游戏状态同步、音视频流
- **TCP vs UDP 在游戏中的选择**
  - TCP适用：回合制、聊天、登录认证、RTS指令、非实时数据
  - UDP适用：FPS位置同步、MOBA实时对战、赛车游戏、格斗游戏
  - 实际项目常用"UDP + 可靠性层"（如ENet），结合两者优势

### Socket系统API

> 对应 Demo_1：Raw Winsock2 单文件服务器/客户端

**Winsock初始化（Windows特有）**

> Linux和MacOS上不需要

```
WSAStartup(MAKEWORD(2,2), &wsa_data)  // 加载WinSock DLL
  ↓
WSACleanup()  // 程序退出时释放资源
```

- 这是进程相关的，只要有一个线程（主线程）开启或关闭就行

**服务器端流程**

windows关闭socket的动作很特别，是closesocket而不是closesocket

```
socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)
  ↓  创建TCP套接字（AF_INET=IPv4, SOCK_STREAM=TCP流）
  ↓  返回一个文件句柄
bind(socket, &addr, sizeof(addr))
  ↓  绑定IP+端口到套接字（INADDR_ANY=监听所有网卡接口）
listen(socket, SOMAXCONN)
  ↓  转为被动监听模式（SOMAXCONN=系统最大积压连接数）
accept(socket, &client_addr, &addr_size)
  ↓  阻塞等待客户端连接，从消息队列中取出一个请求，为这个客户端创建专属socket，以此与多个客户端沟通
recv(client_socket, buf, size, 0) / send(client_socket, data, len, 0)
  ↓  收发数据（TCP无消息边界）
closesocket(socket) 
  ↓  关闭连接
```

**客户端流程**

```
socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)
  ↓
connect(socket, &server_addr, sizeof(addr))
  ↓  向服务器发起连接（127.0.0.1=本地回环测试地址）
send(socket, data, len, 0) / recv(socket, buf, size, 0)
  ↓
closesocket(socket)
```

#### **是否阻塞和单多线程**

Demo_1 使用默认**阻塞模式**：`accept()` / `recv()` / `send()` 均会阻塞线程直到操作完成。这导致服务端一次只能服务一个客户端——`accept()` 取到一个连接后进入 `recv()` 循环，再无机会 `accept()` 第二个客户端。

解决"服务多个客户端"有两种主流方案：

**方案一：单线程 + 非阻塞IO（课程路线：Demo_2/4/5/7 实际采用）**

核心思路：将所有 socket 设为非阻塞，单线程轮询所有 socket，有数据就处理，没数据就往下走（更新游戏逻辑）。

**ioctlsocket 开启非阻塞模式**

```cpp
// 将指定 socket 切换为非阻塞模式
u_long mode = 1;  // 1 = 非阻塞, 0 = 恢复阻塞
ioctlsocket(socket, FIONBIO, &mode);
```

设置后各函数行为变化：

| 函数 | 阻塞模式（默认） | 非阻塞模式（FIONBIO=1） |
|------|----------------|----------------------|
| `accept()` | 无连接时阻塞等待 | 立即返回 `INVALID_SOCKET`，`WSAGetLastError()` = `WSAEWOULDBLOCK` |
| `recv()` | 无数据时阻塞等待 | 立即返回 `SOCKET_ERROR`，`WSAGetLastError()` = `WSAEWOULDBLOCK` |
| `send()` | 缓冲区满时阻塞等待 | 缓冲区满时返回 `SOCKET_ERROR`，错误码 `WSAEWOULDBLOCK` |
| `connect()` | 阻塞到握手完成 | 立即返回 `SOCKET_ERROR`，错误码 `WSAEWOULDBLOCK`（连接在后台进行） |

**单线程非阻塞主循环模式**

```cpp
// 1. 将 server_socket 和所有 client_socket 设为非阻塞
u_long mode = 1;
ioctlsocket(server_socket, FIONBIO, &mode);
// ... 每个 client_socket 同理

// 2. 单线程游戏主循环
while (running) {
    // ① 尝试接受新连接（非阻塞，无连接则跳过）
    SOCKET new_client = accept(server_socket, ...);
    if (new_client != INVALID_SOCKET) {
        ioctlsocket(new_client, FIONBIO, &mode);  // 新socket也设非阻塞
        client_pool.push_back(new_client);
    }
    // 忽略 WSAEWOULDBLOCK（暂无连接，正常情况）

    // ② 轮询所有客户端 socket
    for (auto& client : client_pool) {
        int len = recv(client.socket, buf, sizeof(buf), 0);
        if (len > 0) {
            process_message(buf, len);     // 有数据则处理
        } else if (len == 0) {
            mark_disconnected(client);     // 对方正常关闭
        } else {
            if (WSAGetLastError() != WSAEWOULDBLOCK) {
                mark_disconnected(client); // 真正的错误，断开
            }
            // WSAEWOULDBLOCK → 无数据可读，忽略，继续下一个
        }
    }

    // ③ 清理断线客户端
    // ④ 更新游戏逻辑（delta time）
    // ⑤ 广播状态（send 也是非阻塞）
}
```

**非阻塞 I/O 的优缺点**

| 优点 | 缺点 |
|------|------|
| 单线程即可服务所有客户端，无锁竞争 | 手动轮询所有 socket，无数据时也消耗 CPU |
| 游戏逻辑和网络在同一线程，状态同步简单 | 裸轮询效率低 → 需要 `select()` / `SDLNet_CheckSockets()` 优化 |
| 无线程切换开销 | 处理逻辑复杂时容易阻塞其他客户端 |
| 架构简单，易于调试 | 编写回调/事件驱动风格代码，不如同步写法直观 |

**从裸轮询到 select 多路复用**

上面的裸 `for` 循环轮询效率低（大多数 socket 无数据时白白消耗 CPU）。`select()` 系统调用解决了这个问题：

```cpp
while (running) {
    // 构建 fd_set（内核代为监听）
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(server_socket, &readfds);
    for (auto& c : client_pool) FD_SET(c.socket, &readfds);

    // select 阻塞等待"任意一个 socket 变为可读"（或超时）
    timeval tv = {0, 0};  // timeout=0 → 立即返回，不阻塞
    int ready = select(0, &readfds, NULL, NULL, &tv);

    if (ready > 0) {
        // 只处理"真正有数据"的 socket
        if (FD_ISSET(server_socket, &readfds)) { accept_new_client(); }
        for (auto& c : client_pool) {
            if (FD_ISSET(c.socket, &readfds)) { recv_and_process(c); }
        }
    }

    // 游戏逻辑不受影响——select timeout=0 保证每帧都会走到这里
    update_game(dt);
}
```

这就是 SDL_net 的 `SDLNet_CheckSockets()` 底层原理——封装了 `select()`，提供更简洁的 API。

---

**方案二：多线程 + 阻塞IO（传统方案，课程未采用但用于对比理解）**

核心思路：socket 保持默认阻塞模式，主线程专职 `accept()`，每 accept 到一个客户端就 `std::thread` 一个工作线程去阻塞 `recv()`/`send()`。

```cpp
// 主线程：只负责 accept
void server_main() {
    SOCKET server = socket(...);
    bind(server, ...);
    listen(server, ...);

    while (running) {
        sockaddr_in client_addr;
        int addr_len = sizeof(client_addr);

        // accept 阻塞等待新连接（这是主线程唯一的阻塞点）
        SOCKET client = accept(server, (sockaddr*)&client_addr, &addr_len);

        // 为每个客户端创建一个线程
        std::thread worker(handle_client, client);
        worker.detach();  // 分离线程，主线程继续 accept
    }
}

// 工作线程：使用阻塞 recv —— 代码风格跟单客户端一模一样
void handle_client(SOCKET client) {
    char buf[1024];
    while (true) {
        int len = recv(client, buf, sizeof(buf), 0);  // 阻塞等数据
        if (len <= 0) break;                            // 断线则退出
        process_and_send(client, buf, len);
    }
    closesocket(client);
}
```

**多线程阻塞 vs 单线程非阻塞对比**

| 维度 | 单线程 + 非阻塞IO | 多线程 + 阻塞IO |
|------|-------------------|-----------------|
| **编码风格** | 事件驱动轮询，代码分散 | 同步顺序逻辑，代码直观（一个线程 = 一个客户端的完整故事） |
| **资源开销** | 1 个线程，内存开销极小 | N 个客户端 = N 个线程，栈空间 × N（通常 1-8MB/线程） |
| **并发上限** | 取决于单线程处理速度（通常支持数百连接无压力） | 受线程数限制（通常几十到几百，C10K 问题） |
| **线程安全** | 无需加锁，天然线程安全 | 共享数据（游戏状态/玩家列表）必须加锁，容易死锁/竞争 |
| **游戏逻辑** | 网络和逻辑在同一线程，同步简单 | 逻辑线程和多个网络线程之间的数据交换需消息队列 |
| **CPU 效率** | `select()` / `CheckSockets()` 只唤醒有数据的 socket | 多线程并发可充分利用多核，但线程切换有额外开销 |
| **调试难度** | 单线程调用栈清晰 | 多线程竞态 bug 难以复现和定位 |
| **适用场景** | 连接数多、每连接负载轻（游戏服务器、聊天室） | 连接数少、每连接计算重（文件传输、数据库连接池） |

#### **SDL_net 封装框架**

 ==SDL_net 实现里服务端分支是写死 INADDR_ANY ，从而监听所有网卡地址==

```
SDLNet_Init()
  ↓
SDLNet_ResolveHost(&ip, host, port)   // 解析地址
SDLNet_TCP_Open(&ip)                   // 打开TCP socket
  ↓
SDLNet_AllocSocketSet(max_sockets)     // 创建socket集合
SDLNet_TCP_AddSocket(set, socket)      // 添加socket到集合
  ↓
SDLNet_CheckSockets(set, timeout)      // 非阻塞检测就绪socket数量
SDLNet_SocketReady(socket)             // 判断具体socket是否就绪
  ↓
SDLNet_TCP_Recv(socket, buf, maxlen)   // 接收数据（>0=数据长度, ≤0=断开）
SDLNet_TCP_Send(socket, data, len)     // 发送数据
  ↓
SDLNet_FreeSocketSet(set) / SDLNet_TCP_Close(socket) / SDLNet_Quit()
```

---

### TCP粘包拆包

> 对应 Demo_2：多人聊天室（粘包拆包 + ImGui UI + SDL渲染）

**问题本质**

TCP是**字节流**协议，无消息边界：
- **粘包**：多次send的数据被TCP合并到一个TCP段，recv一次收到多条消息
- **拆包**：一次send的数据被TCP分片到多个TCP段，recv需要多次读取才能拼出完整消息
- 根本原因：发送方的Nagle算法（合并小包） + 接收方缓冲区的不确定性 + MTU分片

**方案类别**

| 方案 | 实现 | 优点 | 缺点 |
|------|------|------|------|
| **定界符法（固定分隔符）** | 消息末尾加特殊字符(如0x1E) | 实现简单，可读性好 | 消息内容不能含分隔符 |
| **固定长度法** | 每个消息固定N字节 | 简单极致 | 浪费带宽，不灵活 |
| **长度前缀法** | 消息头4字节写明长度 + 消息体 | 灵活高效，工业标准 | 实现稍复杂 |

---

### TCP非阻塞架构详解

> 对应 Demo_4：多人横版移动游戏（非阻塞 + RPC + Server Authority）

**阻塞 vs 非阻塞对比**

```
阻塞模式（Demo_1）                    非阻塞模式（Demo_2/4/7）
─────────────────                    ──────────────────────
accept() → 无连接时阻塞等待          SDLNet_TCP_Accept() 循环检查
recv()   → 无数据时阻塞等待          SDLNet_CheckSockets() 检测就绪
                                     SDLNet_SocketReady() 逐个判断
单客户端 → 一个线程只能服务一个连接    多客户端 → 单线程服务所有连接
无法同时处理游戏逻辑                  每帧检查所有socket + 更新游戏逻辑
```

**非阻塞服务端核心架构**

```
┌─────────────────── 游戏主循环（每帧）─────────────────────┐
│                                                          │
│  ① 处理新连接                                             │
│     while(socket = SDLNet_TCP_Accept(server))            │
│       → 分配唯一ID                                        │
│       → 创建 Client 对象加入 client_pool                  │
│       → 告知客户端其ID                                     │
│                                                          │
│  ② 处理收发数据                                           │
│     num = SDLNet_CheckSockets(socket_set, 0)             │
│     if (num > 0)                                         │
│       for (client : client_pool)                         │
│         if (SDLNet_SocketReady(client.socket))           │
│           recv → buffer_parse → find delimiter → RPC     │
│           if (recv <= 0) → 标记断线                       │
│       移除断线客户端                                       │
│                                                          │
│  ③ 更新游戏逻辑                                           │
│     float delta = now - last_tick   // 帧间隔时间          │
│     for (client : client_pool)                            │
│       client.pos += speed * delta * direction             │
│       边界钳制                                            │
│                                                          │
│  ④ 广播状态                                               │
│     打包所有玩家状态 → JSON数组 → broadcast("sync", ...)    │
│                                                          │
└──────────────────────────────────────────────────────────┘
```

#### SDLNet_SocketSet 机制

- `SDLNet_AllocSocketSet(N)` — 创建一个最多容纳N个socket的集合
- `SDLNet_TCP_AddSocket(set, socket)` — 将socket加入监听集合
- `SDLNet_CheckSockets(set, timeout)` — 返回就绪(可读)的socket数量
  - timeout=0 表示立即返回不阻塞（适合游戏帧循环）
  - 内部使用 `select()` 多路复用机制
- `SDLNet_SocketReady(socket)` — 判断指定socket是否在就绪集合中
- 客户端断线检测：`SDLNet_TCP_Recv() <= 0` 即视为断开（配合不超过active_socket计数避免无效遍历）

## Chapter 2 核心架构与同步机制

### 同步与序列化

#### 序列化的意义

**序列化**是一种将对象从内存中的随机访问格式转化为可以存储或传输的线性比特流格式的过程

**为什么不能直接发结构体**

直接 `send(sock, (char*)&player, sizeof(player))` 在联机场景下必然出错：

- **指针失效**：`std::string`/`std::vector` 内部存的是堆地址，对方进程里是野指针
- **内存对齐**：编译器插入的 padding 字节因编译器/配置而异
- **字节序差异**：x86 小端 vs 大端设备，多字节整数解读相反
- **类型宽度**：`long` 在 Win64 是 4 字节，Linux64 是 8 字节
- **无版本兼容**：加一个字段，新旧客户端立刻互不兼容

```
发送端                              接收端
GamePlayer{                        GamePlayer{
  id=1001                            id=1001
  name -> 0x7ff8..(堆地址)  序列化     name -> 0x2a3c..(另一个堆地址)
  pos[3]                ─────────→   pos[3]
}                     线性字节流     }
                      反序列化
              随机访问 → 线性 → 随机访问
```

#### 字节序

- **大端（Big Endian）**：高位字节存低地址，网络协议标准（Network Byte Order）
- **小端（Little Endian）**：低位字节存低地址，x86/x64 采用

```
int32 值 0x12345678：
大端：  12 34 56 78     ← 网络字节序
小端：  78 56 34 12     ← x86 内存实际布局
```

手动处理用 `htons()/htonl()`（发送前）与 `ntohs()/ntohl()`（接收后）。Chapter1 的 `SDLNet_ResolveHost` 内部就替我们做了端口转换。

成熟的序列化框架可以帮助我们解决此问题：JSON 用文本字符表示无字节序概念；MsgPack 规范强制大端；Protobuf 用 varint 编码。

> 关键认知：字节序问题不是"用了框架就消失了"，而是"框架把它固化进了格式规范"。

### 序列化实战

> 对应 Demo_3：三个独立子项目 `Demo_3_JSON` / `Demo_3_MsgPack` / `Demo_3_Protobuf`，用**同一份玩家数据**对比三种方案

```
player_id : int    player_name : string   position : float[3]
health    : int    level       : int      inventory : int[5]
skills    : map<string,int>
```

覆盖了五类典型场景：**标量、字符串、定长数组、动态数组、键值字典**。

#### JSON

> JavaScript Object Notation

**特点**

- 文本格式，键值对 + 数组两种基本结构，自描述
- cJSON 库用**双向链表 + 树**表示 JSON：每个节点有 `next/prev` 串起同层兄弟，`child` 指向子节点，由此方便一一映射
- 需逐字段手工 `cJSON_AddXxxToObject` / `cJSON_GetObjectItem`，无自动映射

**优势**

- 可读性强：抓包即可直接看懂报文，调试友好
- 跨平台兼容，工具生态丰富
- 不需要考虑字节序问题：因为每个字符只需要单个字节来表示
- 无需 schema，字段可动态增删

**劣势**

- 序列化/反序列化开销：文本解析更耗时，且每个字段一次 `malloc`
- 数据体积：文本格式更占空间，字段名和 `{}[]":,` 结构符号全部占字节
- 数值精度：浮点走文本转换，精度可能丢失
- 类型安全弱：需手动 `cJSON_IsNumber()` 校验，错了不报错只是读到脏值

**两种输出格式**

| 函数 | 输出 | 用途 |
|------|------|------|
| `cJSON_PrintUnformatted()` | 单行紧凑 | **网络传输**，省字节 |
| `cJSON_Print()` | 带缩进换行 | 调试、写配置文件 |

Demo_3_JSON 的序列化函数用 Unformatted 模拟真实传输，另在 `main` 里 re-parse 成 `cJSON_Print` 格式落盘 `test.json` 供人眼检查。

#### MsgPack

**特点**

- 告诉解析器应该解析什么类型，保证灵活性
- 每个值以一个 **format byte（首字节）** 开头标明类型和长度，后跟数据本体，因此**无需 schema 也能自解析**

```
0x00~0x7f  positive fixint   小正整数(0~127)用首字节直接表示，1字节搞定
0xa0~0xbf  fixstr            短字符串，低5位存长度
0x90~0x9f  fixarray          短数组，低4位存元素个数
0x80~0x8f  fixmap            短字典，低4位存键值对数
```

**优势**

- 保留了Json动态组装的简单性，可以看作针对二进制文件的json
- 采用自动序列化，可以在宏层面定义哪些类型需要序列化
- 也提供了丰富的手动API
- 使用二进制格式获得更好的体积和解析性能：整数 `100` JSON 要 3 个字符，MsgPack 只要 1 字节；`{}[]":,` 结构符号全部消失
- 是一个仅有头文件的库：不需要编译额外的库，接入成本极低

**劣势**

- **字段名仍以字符串明文传输**，这是它体积打不过 Protobuf 的根本原因
- 二进制不可读，调试需借助工具
- 无 schema 约束，字段顺序即契约，版本兼容能力弱

**自动打包 vs 手动打包**

两者产出的字节**完全一致**，区别只在谁来写打包代码。

```cpp
// 自动：一个宏定义参与序列化的字段（Demo_3_MsgPack/msgpack.cpp:21）
MSGPACK_DEFINE(player_id, player_name, position, health, level, inventory, skills);

msgpack::pack(sbuf, player);                    // 序列化，一行
oh.get().convert(player);                       // 反序列化，一行

// 手动：packer 逐调用（Demo_3_MsgPack/msgpack.cpp:68-100）
packer.pack_array(7);                           // 必须先声明容器长度
packer.pack(player.player_id);
packer.pack_array(3);                           // 嵌套容器要自己开头
for (float pos : player.position) packer.pack(pos);
packer.pack_map((uint32_t)player.skills.size());
for (const auto& s : player.skills) { packer.pack(s.first); packer.pack(s.second); }
```

| 维度 | 自动（MSGPACK_DEFINE） | 手动（packer） |
|------|----------------------|---------------|
| 代码量 | 1 行 | ~30 行 |
| 字段增减 | 改宏参数即可 | 改 `pack_array` 计数 + 加 `pack` 调用 |
| 反序列化 | `convert()` 自动 | 需自己遍历 `obj.via.array.ptr[i]` |
| 灵活性 | 固定映射，全字段 | 可跳字段、可换布局、可条件序列化 |
| 侵入性 | **必须能改类定义** | 第三方类也能打 |

> `pack_array(n)` 的 `n` 必须严格等于后续 `pack` 次数，否则流损坏。手动模式适用于：类来自第三方改不了定义、需要版本兼容、想把 `map` 降级成 `array` 省掉键名。

#### Protobuf

（Google产品化工业级解决方案）

> Protocol Buffers

**特点**

契约优先：先定义协议.proto

由编译器生成序列化和反序列化代码

契约文件即唯一手写的源头（`Demo_3_Protobuf/game_player.proto`）：

```protobuf
syntax = "proto3";
package game;

message GamePlayer {
    int32 player_id = 1;
    string player_name = 2;
    Vec3 position = 3;                   // 嵌套消息
    repeated int32 inventory = 6;        // 动态数组
    map<string, int32> skills = 7;       // 字典
}
```

> **`= 1` `= 2` 不是默认值，是字段编号（field number）**，它才是 wire format 里传输的真实标识。
> 由此推出两条兼容性规则：**字段可以随便改名**（不影响二进制）、**编号绝对不能改或复用**。这是 Protobuf 版本兼容的根基。

代码生成链路：

```
game_player.proto   ← 手写（20 行）
       ↓  protoc.exe --cpp_out=.      （generate_proto.bat 封装了这条命令）
game_player.pb.h    ← 类声明   (889 行)
game_player.pb.cc   ← 实现     (885 行)
```

- 20 行 `.proto` → 1774 行生成代码，**膨胀 88 倍**
- `.cc` 就是 `.cpp`，`.cc` 是 Google/Unix 命名习惯，protoc 硬编码输出该后缀
- `.pb.*` 是自动生成的产物，**不要手写不要手改**，改 `.proto` 重跑即全量覆盖
- 生成代码里是**手工展开的编解码器**（无循环无反射的直线代码，可充分内联），这是它最快的原因

**优势**

- 跨语言支持：同一份 `.proto` 生成 C++/Java/Go/Python 代码，编解码逻辑严格对称
- 版本兼容性：靠字段编号而非字段名，加字段不破坏老客户端
- 代码生成：不用手写任何编解码逻辑，序列化只需 `SerializeToString()` / `ParseFromString()`
- **体积最小**：只传字段编号不传字段名、varint 变长编码、默认值字段直接跳过

**劣势**

- 修改.proto会触发整个系统的重新编译
- 引入代码生成步骤，构建流程变复杂
- 二进制不可读，调试需要工具辅助
- 依赖库体积巨大（本项目 `libprotobuf.lib` 达 115 MB）
- 必须能修改数据结构定义，无法直接序列化已有的第三方类

**四类字段的 API 形态（`Demo_3_Protobuf/protobuf.cpp`）**

```cpp
player.set_player_id(2001);                      // ① 标量 → set_xxx()
game::Vec3* pos = player.mutable_position();     // ② 嵌套消息 → mutable_xxx() 返回指针
player.add_inventory(150);                       // ③ repeated → add_xxx() 追加
(*player.mutable_skills())["attack"] = 2;        // ④ map → mutable_xxx() 当 std::map 用

player.SerializeToString(&binary_data);          // 序列化
new_player.ParseFromString(binary_data);         // 反序列化，返回 bool
```

> 子消息只有 `mutable_` 没有 `set_`，因为子消息是**懒分配**的：`mutable_` 才触发 new 并打上"已设置"标记。读取用 `position().x()`，判断存在用 `has_position()`。

#### 三种主流序列化方案对比

以同一份玩家数据实测（数量级参考）：

|              | JSON (cJSON)          | MessagePack             | Protobuf                    |
| ------------ | --------------------- | ----------------------- | --------------------------- |
| **数据体积** | ~160 B（最大）        | ~110 B                  | **~50 B（最小）**           |
| **性能**     | 最慢，文本解析+malloc | 快，二进制直读          | **最快**，生成代码无反射    |
| **易用性**   | 中，逐字段手写        | **最好**，一个宏搞定    | 差，需 schema + 代码生成    |
| **可读性**   | **最好**，肉眼可读    | 差，二进制              | 差，二进制                  |
| **跨语言**   | 好，几乎全语言支持    | 好                      | **最好**，官方多语言生成器  |
| **版本兼容** | 弱，靠字段名手工判空  | 弱，字段顺序即契约      | **最强**，字段编号+可选字段 |
| **schema**   | 无需                  | 无需（自描述）          | **必须**先写 `.proto`       |
| **依赖**     | 单文件源码            | **纯头文件**，零编译    | 巨型库 + protoc 工具链      |
| **构建复杂度** | 最低                | 低                      | 高，需生成步骤              |

**关键取舍**

- 三者的体积差异根源在于**"字段标识怎么传"**：JSON 传字段名 + 结构符号；MsgPack 传字段名但去掉结构符号；Protobuf 只传编号
- 「无 schema」既是 JSON/MsgPack 的优点（灵活、免构建步骤）也是缺点（无法做强版本兼容，字段错位不报错）
- MsgPack 的**纯头文件**特性在小项目中价值极高——对比 Protobuf 那 115 MB 的静态库和 protoc 工具链，接入成本天差地别

**选型建议**

| 场景 | 推荐 |
|------|------|
| 配置文件、调试协议、Web 接口 | JSON |
| 中小型游戏协议、原型开发、脚本热更 | MsgPack |
| 正式商业项目、跨语言微服务、协议长期演进 | Protobuf |

课程路线的实际选择：Demo_2/Demo_4 用 JSON（便于观察报文、教学友好），Demo_7 换 MsgPack（省带宽 + 纯头文件易接入）。

### 联机游戏架构

#### P2P对等网络架构

#### C-S架构

#### 联机游戏类型（按空间划分）

##### 滚服游戏

不断地增加新的服务器，只要多开服务端就可以

玩家流失，需要合服可能会有问题

##### 副本游戏

策划需求：平衡玩家之间的资源争夺

玩法需求：同服玩家间游戏体验相对独立

从网络架构落地来看：

- **专用实例（Dedicated Instance）**：匹配成功后，系统在某台战斗服务器上 spin up 一个独立进程/房间，只服务这 10 名玩家。这与开放世界 MMO 把成千上万玩家放进同一个持续世界（AOI、兴趣管理、动态分区）形成对比。
- **生命周期短且确定**：实例随开局创建、随结算销毁，状态不持久化到大世界（只把战绩、经济奖励等结果写回账号）。
- **权威服务器 + 帧同步/状态同步**：因为是封闭小规模实例，MOBA 普遍用帧同步（Lockstep），服务器只做仲裁/转发，所有客户端跑相同的确定性逻辑。这套方案恰恰依赖"参与者固定、外部不干扰"这一副本特性才能成立——一旦有外部玩家随时进出或外部事件注入，确定性就被破坏了。

所以一句话总结：**MOBA 的每一局 = 一个限定人数、限定时长、资源封闭、状态隔离的战斗副本**，"匹配进入对局"在架构上等同于"组队进入副本"，"结算退出"等同于"通关销毁副本"。它和传统 PVE 副本的区别只在于内容是 PVP 对抗而非打怪,网络架构的隔离逻辑是一致的。

##### 大图分割游戏

全服/大服游戏

不同的地块跑在不同服务器进程上



### 游戏服务端架构

从主循环到多线程设计

#### 服务端一个线程的任务

- 数据收集：监听网络层的新连接和数据输入
- 状态更新：根据输入数据更新游戏世界
- 数据同步：将更新后的世界数据广播给客户端

这三件事构成一个 **tick（帧）**，服务端主循环就是不断重复 tick：

```
┌─→ ① 收集输入   Accept 新连接 + Recv 所有就绪 socket → 解包成命令
│   ② 更新世界   按 delta 推进游戏逻辑（移动、碰撞、技能、AI）
│   ③ 同步状态   打包世界快照 → 广播给所有客户端
└── ④ 等待剩余时间，凑满一个 tick 间隔
```

**为什么要定频而不是空转**

Demo_2/4 的循环是 `while(true)` 满速跑，CPU 直接吃满一核。真实服务端必须限定 tick rate：

| Tick Rate | 间隔 | 典型场景 |
|-----------|------|----------|
| 10~20 Hz | 50~100 ms | MMO 大世界、卡牌、策略 |
| 30 Hz | 33 ms | 多数手游、休闲竞技 |
| 60~128 Hz | 8~16 ms | FPS（CS2 为 64/128） |

tick rate 直接决定**带宽**和**手感**的取舍：频率翻倍，广播流量翻倍，但玩家输入到看见结果的延迟减半。

**固定时间步 vs 可变时间步**

- **可变步长**：`delta = now - last_tick`，实测多少算多少。Demo_4 用的就是这种，实现简单，但同样的输入在不同帧率下结果不一致，浮点误差累积，不可复现
- **固定步长**：逻辑恒定按 `1/30 s` 推进，多余时间累积到下一帧。帧同步（Lockstep）**必须**用固定步长，否则各客户端算不出相同结果

**单线程为什么常常够用**

游戏逻辑单线程有个巨大优势：**没有竞态，不用锁，行为可复现**。一台现代 CPU 单核每 tick 有几十毫秒预算，足够跑几千个实体的简单逻辑。所以主流做法不是"逻辑多线程"，而是：

> 逻辑保持单线程 → 把 IO、日志、序列化、数据库这些**可以异步的**部分挪出去

**简单的多线程**

两个线程：一个用来数据收集和数据同步，一个用来处理游戏世界状态更新

```
   [IO 线程]                      [逻辑线程]
recv → 解包 → 入队  ──消息队列──→  出队 → 更新世界
send ← 打包 ← 出队  ←─消息队列──  入队 ← 生成快照
```

两个线程只通过**队列**交界，共享数据被压缩到队列本身，锁只加在队列的入队/出队上。这已经是 Actor 模型的雏形。

> **踩坑实例**：Demo_7 的 `server.cpp` 里，主线程跑 `enet_host_service()`，`SendUpdates` 线程直接调 `enet_peer_send()`。ENet **不是线程安全的**，两个线程同时碰同一个 `ENetHost` 属于未定义行为；`connectedClient` 这个裸指针也在线程间裸读裸写，客户端断线瞬间可能变成悬空指针。教学 Demo 里能跑，但正式项目必须让发送线程把包投进队列、由网络线程统一发出。

#### 多服架构（多进程）

登陆大厅、数据库日志、副本战斗逻辑拆分成三个单独的进程

之间用Socket通信

```
              ┌──────────────┐
   玩家  ───→ │  Gateway     │  长连接接入、鉴权、转发、防刷
              └──────┬───────┘
        ┌────────────┼─────────────┐
        ↓            ↓             ↓
   ┌─────────┐  ┌─────────┐  ┌──────────┐
   │ Login   │  │ World   │  │ Battle   │  副本/战斗，可动态扩容
   │ 大厅     │  │ 大世界   │  │ 实例进程  │
   └────┬────┘  └────┬────┘  └────┬─────┘
        └────────────┼────────────┘
                     ↓
              ┌──────────────┐
              │ DB / Log     │  落库、日志、排行榜
              └──────────────┘
```

**拆进程的三个理由**

| 理由 | 说明 |
|------|------|
| **隔离崩溃** | 战斗进程 crash 不该带走整个大厅；进程是最硬的隔离边界 |
| **独立扩容** | 战斗实例可按人数动态起停，登录服只需一两个 |
| **独立更新** | 改副本逻辑只重启战斗进程，玩家不掉线 |

代价是：进程间通信必须走序列化（回到 Chapter 2 的序列化话题），调试链路变长，还要处理**部分失败**（某个服挂了其它服怎么办）。

**线程**

有自己的独立堆栈，cpu调度的最小单位

cpu可以均匀地调度每一个线程，当一个线程被IO阻塞，cpu马上会转向另一个线程

但是线程通信是一个很大的问题

**进程 / 线程 / 协程**

| | 隔离性 | 通信成本 | 切换成本 | 游戏中的用途 |
|---|---|---|---|---|
| **进程** | 最强，地址空间独立 | 高（走 socket/共享内存） | 高 | 分服、分功能模块 |
| **线程** | 弱，共享地址空间 | 低（共享内存，但要锁） | 中 | IO / 逻辑 / 日志分离 |
| **协程** | 无，单线程内切换 | 极低 | 极低 | 异步流程写成同步代码（如等 DB 返回） |

**线程安全守则**

- 尽量减少共享数据
- 使用RAII管理锁：让需要访问同一内存的线程排队
- 避免嵌套锁
- 优先使用消息队列：比共享内存更安全

补充几条实战规则：

- **锁的粒度要小，持有时间要短**：绝不在持锁期间做 IO 或调用外部回调
- **约定固定的加锁顺序**：避免嵌套锁的本质是避免"A 等 B、B 等 A"的死锁环
- **`std::atomic` 只保证单个变量原子**：Demo_7 的 `std::atomic<bool> running` 能安全地停循环，但保护不了 `connectedClient` 指针背后的对象生命周期
- **数据竞争 ≠ 逻辑竞争**：加了锁不代表逻辑对，还要考虑"读到的是旧状态"这类时序问题

### Actor模型和消息队列

> 分布式与游戏并发编程利器

多线程通信的两种策略：

共享内存：锁和原子操作 

消息传递：Actor模式（参与者模式）

传统oop有问题就是，多对象在不同线程执行对同一块内存的区域的修改时出现竞争问题

而Actor模型可以储存消息队列，在合适的时候执行

Actor = (状态+行为)+消息

状态：Actor组件本身的星系，相当于oop对象中的属性。Actor的状态会受Actor自身行为影响，且只能被自己修改

行为：Actor的计算处理操作，相当于成员函数

消息：Actor之间的交互，异步通知，具有非阻性的，发完消息就可以继续运行

无需使用锁，一次去除一个消息处理，就是一种天然的互斥锁

内部不需要锁的存在

#### Actor 的三个组成

```
        ┌──────────── Actor ────────────┐
消息 ──→ │  Mailbox（消息队列，FIFO）      │
        │        ↓ 一次取一条             │
        │  Behavior（行为/处理函数）       │ ──→ 发消息给其它 Actor
        │        ↓ 修改                  │ ──→ 创建新 Actor
        │  State（私有状态，外部不可见）    │ ──→ 改变自身行为
        └───────────────────────────────┘
```

**为什么天然无锁**

关键约束只有一条：**状态只能被自己修改**。外部想改，只能发消息排队。

于是"同一时刻只有一个线程在跑这个 Actor 的处理函数"由调度器保证，共享内存的竞态从根上消失了 —— 不是用锁解决竞争，而是**让竞争不存在**。

**调度器（Scheduler）**

Actor 数量远多于线程数（几万个 Actor 跑在 8 个线程上），中间需要调度器：

```
   [线程池 N 个工作线程]
        ↓ 取一个有消息待处理的 Actor
   [就绪 Actor 队列]
        ↓ 执行若干条消息后放回（避免单个 Actor 饿死其它人）
   [挂起的 Actor：邮箱为空，不占线程]
```

要点：**Actor 不等于线程**。空闲 Actor 不消耗任何 CPU，这是它能开到几十万个的原因。

#### 映射到游戏服务端

| 粒度 | 做法 | 说明 |
|------|------|------|
| **每个玩家一个 Actor** | 最常见 | 玩家状态私有，交互走消息，天然隔离外挂影响面 |
| **每个副本/房间一个 Actor** | MOBA、吃鸡 | 房间内仍是单线程逻辑，房间之间并行 |
| **每个服务一个 Actor** | Skynet 风格 | 登录、背包、聊天各自成 Actor，进程内像微服务 |

粒度过细（每个子弹一个 Actor）会让消息量爆炸，反而更慢 —— **Actor 的开销在消息传递，不在状态本身**。

#### 消息队列的实现要点

- **入队出队要线程安全**：内部仍然有锁（或无锁队列 CAS），只是把锁收敛到了一个点
- **背压（Backpressure）**：生产快于消费时队列无限增长 → OOM。必须设上限，满了要么阻塞发送方、要么丢弃低优先级消息
- **消息所有权**：C++ 里用 `std::unique_ptr` 或值语义传递，避免发出去后双方都持有同一块内存
- **优先级**：心跳、断线通知应插队，不能排在几千条位移包后面

**Demo 中的实例**

- `Demo_6` 的 `async_example()`：spdlog 的异步 logger 就是标准的生产者-消费者队列 —— 业务线程只把日志丢进 ring buffer 立刻返回，后台线程负责真正写盘。日志 IO 从此不阻塞游戏逻辑，这正是"把可异步的部分挪出主循环"的落地
- `Demo_7` 的两线程直接共用 ENet 对象，是**没有**用消息队列的反面案例（见上一节）

优势：

- 高级抽象
- 非阻塞型
- 无需锁机制
- 架构灵活
- 易于测试

劣势：

- 要求每个Actor相互独立
- 并发系统共有问题：不能保证全局消息处理顺序，如果需要严格顺序，需要额外机制

补充几个实战上的坑：

- **消息顺序只在"同一对收发方之间"有保证**：A→C 和 B→C 两条消息谁先到不确定。需要全局顺序时得引入序号或逻辑时钟
- **延迟换来了吞吐**：消息要排队，单次响应比直接调函数慢；换来的是高负载下不会因为抢锁而雪崩
- **死锁没消失，只是换了形式**：A 发消息给 B 并同步等回复，B 又等 A，一样卡死。所以 Actor 里应尽量用"发完就走 + 回调"，而非"发完等回复"
- **调试变难**：调用栈断在队列处，排查要靠消息 trace / 日志串联（这也是 Demo_6 引入 spdlog 的意义）

### 世界状态同步

> 游戏对象生命周期与复制机制

服务端的世界是**权威副本**，客户端持有的只是一份**近似的、滞后的拷贝**。世界状态同步要解决的就是：让这份拷贝以尽量小的带宽，尽量贴近权威副本。

#### 两大同步范式

| | **状态同步（State Sync）** | **帧同步（Lockstep）** |
|---|---|---|
| 传输内容 | 服务器广播**世界状态** | 只广播**玩家输入指令** |
| 计算位置 | 服务端算，客户端显示 | 各客户端各自算完整逻辑 |
| 带宽 | 随实体数量增长 | 极小，与实体数无关 |
| 一致性要求 | 弱，各端可有细微差异 | **必须逐位确定**，禁用浮点随机 |
| 反外挂 | 强，服务端仲裁一切 | 弱，客户端知晓全图信息 |
| 断线重连 | 容易，发一份全量快照即可 | 难，需从头追帧或走快照 |
| 典型 | MMO、FPS、本课程 Demo_4/7 | MOBA、RTS、格斗 |

课程 Demo 全部走状态同步：Demo_4 服务端算位置后广播 `sync` 数组，客户端只负责画。

#### 对象生命周期

同步的最小单位是**可复制对象（Replicated Object）**，其生命周期三阶段：

```
   Spawn ─────────→ Update ─────────→ Destroy
   创建通知          属性持续复制        销毁通知
   （含初始全量）     （只发变化量）      （客户端删本地副本）
```

| 阶段 | 服务端 | 客户端 |
|------|--------|--------|
| **Spawn** | 分配全局唯一 **NetID**，广播"创建对象 + 类型 + 初始状态" | 按类型实例化本地对象，登记到 NetID→对象 表 |
| **Update** | 每 tick 检测哪些属性变了，只发变化部分 | 查表找到对象，写入新值 |
| **Destroy** | 广播销毁，回收 NetID | 删除本地对象，清表 |

**NetID 是整套机制的地基**：网络上不能传指针（Chapter 2 已述），所以两端靠一个整数 ID 指认同一个对象。Demo_2/4 里服务端分配的 `id_user_next++` 就是最朴素的 NetID。

几个必须处理的边界：

- **NetID 不可复用**，或复用前留足冷却 —— 否则迟到的旧包会打到新对象上
- **Spawn 必须可靠送达**：漏了创建包，后续所有更新包都找不到对象。所以 spawn/destroy 走可靠通道（ENet 的 `ENET_PACKET_FLAG_RELIABLE`），高频位移可以走不可靠通道
- **中途加入（Late Join）**：新连入的玩家需要一份**全量快照**补齐已有对象，而不能只收增量。Demo_2 里新用户连上先收自己的 ID，就是最简版的"初始状态下发"

#### 复制什么：属性复制（Replication）

不是所有成员变量都要同步，按需求分三类：

| 类型 | 例子 | 处理 |
|------|------|------|
| **需复制** | 位置、血量、状态机、装备外观 | 进入复制列表，变化时下发 |
| **本地推算** | 动画混合权重、粒子、UI 抖动 | 客户端自己算，不占带宽 |
| **服务端私有** | 掉落表种子、AI 仇恨值、其他玩家的底牌 | **绝不下发**，下发即等于送外挂 |

**减少带宽的四个常规手段**

1. **增量复制（Delta）**：只发和上一次快照的差异，位置没变的对象一个字节都不发
2. **属性脏标记（Dirty Flag）**：改属性时打标记，打包时只遍历脏的
3. **量化压缩**：位置用定点数代替 `float`，朝向 360° 压进 1 字节，血量发百分比
4. **降频与分级**：远处对象 2 Hz、近处 30 Hz；不重要属性（如称号）改变时才发一次

#### 发给谁：相关性与 AOI

大世界里给每个玩家广播全世界的状态是不可能的 —— 流量是 O(N²)。所以需要**兴趣管理**：

```
        ┌─────────────────────────┐
        │        整个世界          │
        │   ┌─────────────┐       │
        │   │  AOI 半径    │       │  只有落在圈内的对象
        │   │   ◎ 玩家     │       │  才会被复制给该玩家
        │   └─────────────┘       │
        └─────────────────────────┘
```

- **九宫格 / 网格法**：世界切成格子，只同步自己所在格及相邻 8 格。实现简单，最常用
- **十字链表**：按 x、y 各维护有序链表，增量维护进出视野事件
- **进出视野 = 动态 Spawn/Destroy**：对象离开 AOI，对客户端而言等价于"销毁"；重新进入则重新 spawn

AOI 顺带解决了**透视外挂**：客户端根本没收到视野外敌人的数据，想开图也没得开。

#### 客户端侧的补偿

服务端 tick 只有 20~30 Hz，直接按收到的状态硬设位置会明显卡顿。客户端必须做平滑：

| 手段 | 做法 | 代价 |
|------|------|------|
| **插值（Interpolation）** | 缓存最近两个快照，在它们之间按时间插值渲染 | 画面比实际状态**慢一个 tick**，恒定延迟 |
| **外推（Extrapolation）** | 按最后已知速度推算当前位置 | 对方突然变向时会出现拉扯回弹 |
| **客户端预测（Prediction）** | 本地输入立刻生效，不等服务器确认 | 需要和服务器结果对账 |
| **和解（Reconciliation）** | 服务器结果回来后，回滚到该点重放之后的所有输入 | 实现复杂，要存输入历史 |

一句话概括三者的分工：**预测让自己不卡，插值让别人不抖，和解让两者最终一致**。

> 权威原则：客户端可以**预测**，但服务端说了算。凡是客户端上报的位置、伤害、拾取，服务端都必须验算 —— Demo_4 服务端只接受"方向键输入"而非"我现在在哪"，正是这个原则的体现。

（延迟、抖动、丢包的具体处理见 Chapter 3）


### Replication实战

### Rpc实战

## Chapter 3 进阶优化与专项技术

### 延迟、抖动与丢包

 
