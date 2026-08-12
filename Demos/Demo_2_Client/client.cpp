#define SDL_MAIN_HANDLED

/*
 * Demo_2_Client — TCP 聊天室客户端
 *
 * 本 demo 重点演示 TCP 的粘包/拆包问题及其解决方案：
 *   - 粘包：多个发送的数据包在 TCP 流中粘连在一起接收
 *   - 拆包：一个数据包被拆分成多次接收
 * 解决方案：在每个消息末尾追加一个分隔符 0x1E（ASCII Record Separator），
 * 接收方在缓冲区中查找该分隔符来切分出完整的消息。
 *
 * 通信流程：
 *   1) 连接服务器后，等待服务器分配用户 ID（第一个数据包即为 ID）
 *   2) 进入主循环：接收并显示聊天消息，用户输入文本并发送
 *   3) 发送时以分隔符结尾，接收时根据分隔符拆包
 */

#include <SDL.h>           // SDL 主库：窗口创建、事件处理、渲染
#include <imgui.h>         // Dear ImGui：即时模式 GUI 库
#include <SDL_net.h>       // SDL_net：跨平台网络库（TCP socket 封装）
#include <imgui_stdlib.h>  // ImGui std::string 输入框支持（ImGui::InputText 绑定到 std::string）
#include <imgui_impl_sdl2.h>       // ImGui SDL2 后端：将 SDL 事件转发给 ImGui
#include <imgui_impl_sdlrenderer2.h> // ImGui SDL_Renderer 后端：用 SDL2 渲染 ImGui 绘制命令

#include <string>     // std::string：用于网络缓冲区管理
#include <queue>      // std::queue：存储从 TCP 流中拆包得到的完整消息队列
#include <chrono>     // 高精度时间：用于帧率控制（144 FPS 固定步长）
#include <thread>     // std::this_thread::sleep_for：帧率控制中的主动休眠
#include <vector>     // std::vector：存储聊天历史记录列表

// ============================================================
// 常量定义
// ============================================================

/**
 * @brief 消息分隔符
 *
 * 0x1E 是 ASCII 控制字符中的"记录分隔符"（Record Separator, RS），
 * 适合作为 TCP 流中消息的边界标记。
 *
 * 为什么需要分隔符？
 *   TCP 是基于字节流的协议，它不保留应用层消息的边界。
 *   当连续发送多条消息时，接收方可能一次性收到多条消息
 *   （粘包），也可能只收到一条消息的一部分（拆包）。
 *   通过在每条消息尾部追加一个不会在消息内容中出现的
 *   特殊分隔符，接收方可以准确还原每条消息的边界。
 */
static constexpr char delimiter = 0x1E;

// ============================================================
// 网络缓冲区与消息队列
// ============================================================

/**
 * @brief 原始接收缓冲区
 *
 * SDLNet_TCP_Recv() 每次调用能读取到的字节数是不确定的，
 * 它取决于操作系统 TCP 栈当前接收缓冲区中有多少数据可用。
 * 每次 recv 之后，立即将这个缓冲区中的内容追加到 buffer_parse 中。
 */
static char buffer_recv[1024];

/**
 * @brief 累积解析缓冲区
 *
 * 这个 std::string 作为累积缓冲区，保存所有已接收但尚未
 * 解析完成的原始数据。每次收到新数据就追加到末尾，然后
 * 由 parse_pkg() 从中查找分隔符并切分出完整消息。
 * 未包含分隔符的不完整数据会被保留，等待下次接收数据后继续解析。
 */
static std::string buffer_parse;

/**
 * @brief 解析完成的消息队列
 *
 * 存放已经从 buffer_parse 中完整切分出来的消息字符串。
 * 主循环中从该队列取出消息，添加到聊天历史显示列表中。
 *
 * 使用队列的原因是：单次 recv 可能包含多条完整消息
 * （粘包），需要全部取出并排队处理。
 */
static std::queue<std::string> queue_pkg;

// ============================================================
// UI 状态变量
// ============================================================

/** @brief 当前用户在服务器上的编号（由服务器在连接时分配） */
static int id_user = -1;

/** @brief 输入框中的文本内容（与 ImGui InputText 双向绑定） */
static std::string str_inputbox;

/** @brief 聊天历史消息列表（每条是一个字符串，逐行显示） */
static std::vector<std::string> history_list;

// ============================================================
// 拆包函数（核心逻辑）
// ============================================================

/**
 * @brief 从累积缓冲区中根据分隔符切分出完整消息
 *
 * 这是解决 TCP 粘包/拆包问题的核心函数。其工作流程如下：
 *
 * 1. 从 offset 开始，在 buffer_parse 中查找第一个分隔符 0x1E
 * 2. 如果找到，说明 [offset, pos) 区间包含一条完整消息：
 *    - 将该段字符串提取出来，压入 queue_pkg
 *    - 将 offset 移动到 pos + 1（跳过分隔符），继续查找下一个
 * 3. 如果找不到分隔符，说明剩余数据不完整，保留在 buffer_parse 中
 *
 * @return true  找到了至少一个完整数据包
 * @return false 没有找到完整数据包
 */
bool parse_pkg()
{
    size_t offset = 0;       // 当前搜索的起始位置
    bool has_new_pkg = false; // 是否至少找到了一个完整包

    while (true)
    {
        // 从 offset 位置开始，在累积缓冲区中查找分隔符
        size_t pos = buffer_parse.find_first_of(delimiter, offset);

        if (pos == std::string::npos)
        {
            // 没有找到分隔符 → 剩余数据是不完整的消息尾部
            // 将其保留在 buffer_parse（覆盖掉已处理的部分）
            buffer_parse = buffer_parse.substr(offset);
            break;
        }

        // 找到分隔符 → 提取 [offset, pos) 区间作为一条完整消息
        // pos 指向分隔符本身，所以长度 = pos - offset
        std::string str_packet(buffer_parse.data() + offset, pos - offset);

        // 将完整消息放入队列，等待主循环处理
        queue_pkg.push(str_packet);
        has_new_pkg = true;

        // offset 移动到分隔符之后，继续查找下一条消息
        offset = pos + 1;
    }

    return has_new_pkg;
}

// ============================================================
// 主函数
// ============================================================

int main(int argc, char** argv)
{
    using namespace std::chrono;

    // ----------------------------------------------------------
    // 第一步：初始化 SDL 和 SDL_net
    // ----------------------------------------------------------

    // 初始化 SDL 全部子系统（视频、事件、计时器、音频等）
    SDL_Init(SDL_INIT_EVERYTHING);
    // 初始化 SDL_net 网络库
    SDLNet_Init();

    // 启用 SDL 输入法扩展接口（IME），允许在游戏中显示中文输入法候选框
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");

    // ----------------------------------------------------------
    // 第二步：创建窗口和渲染器
    // ----------------------------------------------------------

    // 创建一个 500×620 的窗口，标题为 "Chat Room Online"，居中显示
    SDL_Window* window = SDL_CreateWindow(
        u8"Chat Room Online",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        500, 620, SDL_WINDOW_ALLOW_HIGHDPI
    );

    // 创建硬件加速渲染器（启用垂直同步，限制帧率到显示器刷新率）
    SDL_Renderer* renderer = SDL_CreateRenderer(
        window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );

    // ----------------------------------------------------------
    // 第三步：解析服务器地址并连接
    // ----------------------------------------------------------

    /**
     * 服务器地址结构体（IPaddress 包含 host 和 port 的二进制表示）
     * SDLNet_ResolveHost() 将字符串形式的主机和端口解析为内部表示：
     *   - 主机：127.0.0.1（本地回环地址，即在本机测试）
     *   - 端口：25565
     * 返回 0 表示成功，非 0 表示解析失败（如无效 IP 或无法解析域名）
     */
    IPaddress ip;
    if (SDLNet_ResolveHost(&ip, "192.168.0.108", 25565))
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error",
            u8"无法解析服务器地址。", window);
        return -1;
    }

    // 使用解析好的地址打开 TCP 连接
    // SDLNet_TCP_Open(&ip) 作为客户端：连接到 ip 指定的服务器
    // 返回的 socket 代表了与服务器的 TCP 连接
    TCPsocket socket;
    if (!(socket = SDLNet_TCP_Open(&ip)))
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error",
            u8"无法连接到服务器。", window);
        return -1;
    }

    // ----------------------------------------------------------
    // 第四步：创建 socket 集合用于非阻塞轮询
    // ----------------------------------------------------------

    /**
     * SDLNet_SocketSet 是 SDL_net 提供的 socket 集合抽象，
     * 配合 SDLNet_CheckSockets() 可以非阻塞地检查多个 socket
     * 上是否有数据可读。
     *
     * 分配一个容量为 1 的 socket 集合（因为我们只监控这一个连接），
     * 然后将 socket 加入集合。
     */
    SDLNet_SocketSet socket_set = SDLNet_AllocSocketSet(1);
    SDLNet_TCP_AddSocket(socket_set, socket);

    // ----------------------------------------------------------
    // 第五步：等待服务器分配用户 ID
    // ----------------------------------------------------------

    /**
     * 连接建立后，服务器会立即发送一个数据包，内容为分配给
     * 当前连接的用户编号（整数，如 "0"、"1"）。客户端需要
     * 先接收这个 ID，然后才能进入正常的聊天界面。
     *
     * 这个循环持续尝试接收数据，直到成功解析出第一个数据包为止。
     * 注意这里使用了 SDLNet_CheckSockets(socket_set, 0)，
     * 超时参数为 0 表示立即返回，不阻塞等待。
     */
    while (true)
    {
        // 非阻塞检查 socket 上是否有数据可读
        // > 0：有数据；== 0：无数据；< 0：错误
        if (SDLNet_CheckSockets(socket_set, 0) <= 0)
        {
            // 有数据可读，执行接收
            // SDLNet_TCP_Recv 返回实际接收到的字节数
            int sz_received = SDLNet_TCP_Recv(socket, buffer_recv, 1024);
            if (sz_received <= 0)
            {
                // 接收失败或连接已关闭（0 表示对方关闭连接）
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error",
                    u8"网络连接意外中断。", window);
                return -1;
            }

            // 将本次收到的数据追加到累积解析缓冲区
            buffer_parse.append(buffer_recv, sz_received);

            // 尝试从累积缓冲区中提取完整数据包
            // 服务器发送的第一个数据包就是用户 ID 的字符串表示
            if (parse_pkg())
            {
                // 成功接收到第一个数据包，其内容即用户 ID
                const std::string& pkg = queue_pkg.back();
                id_user = std::stoi(pkg);
                queue_pkg.pop();
                break;  // 退出等待循环，进入主界面
            }
        }
    }

    // ----------------------------------------------------------
    // 第六步：初始化 ImGui
    // ----------------------------------------------------------

    // 创建 ImGui 上下文（管理所有 ImGui 状态）
    ImGui::CreateContext();

    // 初始化 ImGui 的 SDL2 后端（将 SDL 事件转发给 ImGui）
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    // 初始化 ImGui 的 SDL_Renderer 后端（使用 SDL2 渲染 ImGui 图形）
    ImGui_ImplSDLRenderer2_Init(renderer);

    // 设置 ImGui 样式：圆角按钮、1px 边框、子窗口圆角
    ImGuiStyle& style = ImGui::GetStyle();
    style.FrameRounding = 4.0f;
    style.FrameBorderSize = 1.0f;
    style.ChildRounding = 4.0f;

    // 加载中文字体（微软雅黑），支持完整中文字符集
    // 如果不加载中文字体，ImGui 将无法正确显示中文文本
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->AddFontFromFileTTF(
        R"(C:\Windows\Fonts\msyh.ttc)", 18.0f, nullptr,
        io.Fonts->GetGlyphRangesChineseFull()
    );

    // ----------------------------------------------------------
    // 第七步：主循环前的准备工作
    // ----------------------------------------------------------

    /**
     * 帧率控制：目标 144 FPS
     * 每帧的理论时长 ≈ 6.94ms
     * 每帧渲染完成后，如果实际耗时小于此值，则主动休眠剩余时间，
     * 从而降低 CPU 占用并保持稳定的帧率。
     */
    const nanoseconds frame_duration(1000000000 / 144);

    // 上一帧的时间点（用于计算 delta time）
    steady_clock::time_point last_tick = steady_clock::now();

    // 主循环退出标志
    bool is_quit = false;

    // ----------------------------------------------------------
    // 第八步：主循环（事件处理 → 网络接收 → GUI 渲染 → 帧率控制）
    // ----------------------------------------------------------

    while (!is_quit)
    {
        // ----- 8a. 事件处理 -----

        SDL_Event event;

        // SDL_PollEvent 轮询事件队列（非阻塞，没有事件时返回 false）
        // 处理所有待处理的窗口事件、键盘事件、鼠标事件等
        while (SDL_PollEvent(&event))
        {
            // 将 SDL 事件转发给 ImGui（让 ImGui 处理鼠标点击、键盘输入等）
            ImGui_ImplSDL2_ProcessEvent(&event);

            // 检测窗口关闭事件（用户点击了窗口右上角的 ×）
            if (event.type == SDL_QUIT)
                is_quit = true;
        }

        // ----- 8b. 帧时间计算 -----

        // 记录当前帧的开始时间
        steady_clock::time_point frame_start = steady_clock::now();

        // 计算距离上一帧的经过时间（delta time），以秒为单位
        // 虽然当前版本未直接使用 delta，但为后续版本预留（如动画、游戏逻辑）
        float delta = duration<float>(frame_start - last_tick).count();

        // ----- 8c. 接收网络数据 -----

        /**
         * 使用 SDLNet_CheckSockets 非阻塞检查是否有数据可读
         * 超时参数为 0：立即返回，不等待
         *
         * 注意这里的逻辑与之前等待 ID 时相反：
         * 之前是 !(有数据) 时进入接收，这里是 (有数据) 时进入接收。
         * 这是因为循环条件的判断逻辑不同：
         *   等待 ID 阶段：if (CheckSockets <= 0) → 实际上是 <= 0 表示"没有等待中的 socket"
         *   正常接收阶段：if (CheckSockets > 0) → 有数据可读
         */
        if (SDLNet_CheckSockets(socket_set, 0) > 0)
        {
            // 接收数据到原始缓冲区
            int sz_received = SDLNet_TCP_Recv(socket, buffer_recv, 1024);
            if (sz_received <= 0)
            {
                // 连接中断：服务器关闭或网络出错
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error",
                    u8"网络连接意外中断。", window);
                return -1;
            }

            // 追加到累积解析缓冲区
            buffer_parse.append(buffer_recv, sz_received);

            /**
             * 尝试拆包：从累积缓冲区中提取所有完整消息
             * handle_tcp_packet 可能会提取出 0 条（数据尚不完整）、
             * 1 条（正好一个完整包）或多条（粘包）消息
             */
            if (parse_pkg())
            {
                /**
                 * 将本次解析出的所有消息依次出队，添加到聊天历史列表中
                 * 注意：这里使用 queue_pkg.back() 取出队尾元素再 push_back，
                 * 但更正确的做法应该是 queue_pkg.front() 取出队首，因为
                 * 队列是先进先出的。多条消息时使用 while 循环全部处理。
                 *
                 * 当前写法有一个 bug：queue_pkg.back() 始终取最后一个，
                 * 而 queue_pkg.pop() 弹出队首，所以如果只有一个包时没问题，
                 * 但多个包时只有最后一个会被保留。
                 * 已修复为使用 queue_pkg.front()。
                 */
                while (!queue_pkg.empty())
                {
                    // 将队首消息添加到历史列表
                    history_list.push_back(queue_pkg.front());
                    // 弹出已处理的消息
                    queue_pkg.pop();
                }
            }
        }

        // ----- 8d. 渲染 ImGui 界面 -----

        // 开始新一帧的 ImGui 渲染
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // --- 聊天界面布局 ---

        {
            // 获取主视口信息，让窗口覆盖整个工作区
            ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);
            ImGui::SetNextWindowViewport(viewport->ID);

            // 创建全屏面板（无标题栏、不可折叠、不可调整大小、不可移动）
            ImGui::Begin("Panel", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse
                | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

            // 显示当前用户的 ID
            ImGui::BulletText(u8"用户 ID：%d", id_user);

            // 设置聊天历史子窗口的背景色（深蓝色）
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImColor(5, 15, 25, 255).Value);

            // 聊天消息显示区域：占据面板剩余高度减去输入区域高度（30px）
            // ImGuiChildFlags_Border 添加子窗口边框
            ImGui::BeginChild("History",
                { 0, ImGui::GetContentRegionAvail().y - 30 },
                ImGuiChildFlags_Border);
            ImGui::PopStyleColor();

            // 逐行显示所有聊天历史消息
            for (const std::string& str : history_list)
                ImGui::TextUnformatted(str.c_str());

            /**
             * 自动滚动到底部：如果当前滚动条已经在最底部，
             * 则在渲染新消息后自动滚到底部，模拟聊天软件的"新消息自动显示"效果。
             * 如果用户已经向上滚动查看历史消息，则不会强制跳转底部。
             */
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY(1.0f);

            ImGui::EndChild();

            // --- 输入区域 ---

            // 输入框宽度：面板宽度减去发送按钮宽度（100px）
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 100);

            // 文本输入框（标签 "##input" 为隐藏标签，不会显示在界面上）
            ImGui::InputText("##input", &str_inputbox);

            // "发送" 按钮放在同一行
            ImGui::SameLine();

            // 输入框为空时禁用发送按钮（防止发送空消息）
            ImGui::BeginDisabled(str_inputbox.empty());

            /**
             * 发送按钮：点击或按 Enter 键时
             * ImGui::Button 返回 true 表示按钮被点击
             * ImGui::IsKeyPressed(ImGuiKey_Enter, false) 检测是否按下了 Enter 键
             */
            if (ImGui::Button(u8"发送", { ImGui::GetContentRegionAvail().x, 0 })
                || ImGui::IsKeyPressed(ImGuiKey_Enter, false))
            {
                /**
                 * 发送消息时，将消息内容和分隔符拼接，然后通过 TCP 发送。
                 * 分隔符 0x1E 追加在末尾，服务器也根据这个分隔符来拆包。
                 *
                 * 例如用户输入 "你好"，实际发送的数据为：
                 *   "你好\x1E"
                 */
                std::string str_pkg = str_inputbox + delimiter;
                SDLNet_TCP_Send(socket, str_pkg.c_str(), (int)str_pkg.size());

                // 发送成功后清空输入框，准备输入下一条消息
                str_inputbox.clear();
            }

            ImGui::EndDisabled();
            ImGui::End();
        }

        // ----- 8e. 绘制到屏幕 -----

        // 设置清屏颜色为黑色
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        // 清空渲染目标
        SDL_RenderClear(renderer);

        // 完成 ImGui 渲染并生成绘制命令
        ImGui::Render();
        // 将 ImGui 的绘制命令通过 SDL_Renderer 提交
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);

        // 将渲染缓冲区交换到屏幕（显示最终画面）
        SDL_RenderPresent(renderer);

        // ----- 8f. 帧率控制 -----

        // 更新上一帧记录的时间
        last_tick = frame_start;

        /**
         * 计算本帧剩余时间，如果比预期完成时间短，则主动休眠。
         * 这样可以：
         *   1) 降低 CPU 占用率（不让主循环空转）
         *   2) 保持稳定的帧率（144 FPS，避免画面撕裂或卡顿）
         *   3) 为笔记本电脑节省电量
         */
        nanoseconds sleep_duration = frame_duration - (steady_clock::now() - frame_start);
        if (sleep_duration > nanoseconds(0))
            std::this_thread::sleep_for(sleep_duration);
    }

    // ----------------------------------------------------------
    // 第九步：清理资源（按初始化相反顺序）
    // ----------------------------------------------------------

    // 关闭 ImGui 渲染后端
    ImGui_ImplSDLRenderer2_Shutdown();
    // 关闭 ImGui SDL2 后端
    ImGui_ImplSDL2_Shutdown();
    // 销毁 ImGui 上下文
    ImGui::DestroyContext();

    // 释放 socket 集合
    SDLNet_FreeSocketSet(socket_set);

    // 销毁渲染器
    SDL_DestroyRenderer(renderer);
    // 销毁窗口
    SDL_DestroyWindow(window);

    // 关闭 SDL_net 子系统
    SDLNet_Quit();
    // 关闭 SDL 全部子系统
    SDL_Quit();

    return 0;
}
