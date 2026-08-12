#define MSGPACK_NO_BOOST
#define SDL_MAIN_HANDLED

#include "script.h"

#include <msgpack.hpp>
#include <enet/enet.h>
#include <SDL.h>
#include <lua.hpp>

#include <iostream>
#include <string>
#include <chrono>

struct RectangleParams
{
    float size = 100.0f;
    float r = 255.0f;
    float g = 255.0f;
    float b = 255.0f;
    float a = 255.0f;
};

class LuaManager
{
private:
    lua_State* L = nullptr;
    RectangleParams currentParams;
    int currentVersion = 0;

public:
    LuaManager()
    {
        L = luaL_newstate();
        luaL_openlibs(L);

        // 注册C函数到Lua
        lua_pushcfunction(L, print);
        lua_setglobal(L, "print");
    }

    ~LuaManager()
    {
        if (L) lua_close(L);
    }

    bool LoadScript(const std::string& script, int version)
    {
        if (version <= currentVersion) return false;

        std::cout << "[Lua] Loading script v" << version << std::endl;

        // 执行Lua脚本
        if (luaL_dostring(L, script.c_str()) != LUA_OK)
        {
            std::cerr << "[Lua] Error: " << lua_tostring(L, -1) << std::endl;
            lua_pop(L, 1);
            return false;
        }

        // 获取矩形参数
        if (!lua_istable(L, -1))
        {
            std::cerr << "[Lua] Script must return a table" << std::endl;
            lua_pop(L, 1);
            return false;
        }

        // 从表中读取参数
        RectangleParams params = { 100.0f, 0.0f, 0.0f, 0.0f, 255.0f };

        lua_getfield(L, -1, "size");
        if (lua_isnumber(L, -1))
            params.size = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, -1, "r");
        if (lua_isnumber(L, -1))
            params.r = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, -1, "g");
        if (lua_isnumber(L, -1))
            params.g = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, -1, "b");
        if (lua_isnumber(L, -1))
            params.b = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);

        lua_pop(L, 1);  // 弹出table

        currentParams = params;
        currentVersion = version;

        std::cout << "[Lua] Rectangle params: size=" << params.size
            << ", color=(" << params.r << "," << params.g << "," << params.b << ")" << std::endl;
        return true;
    }

    const RectangleParams& GetParams() const { return currentParams; }
    int GetVersion() const { return currentVersion; }

private:
    static int print(lua_State* L)
    {
        int n = lua_gettop(L);
        for (int i = 1; i <= n; i++)
        {
            if (i > 1) std::cout << "\t";
            if (lua_isstring(L, i))
                std::cout << lua_tostring(L, i);
        }
        std::cout << std::endl;
        return 0;
    }
};

class HotUpdateClient
{
private:
    ENetHost* client = nullptr;
    ENetPeer* server = nullptr;
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    LuaManager lua;
    float time = 0.0f;
    bool running = true;

public:
    bool Initialize(const char* title = "Hot Update Client")
    {
        if (enet_initialize() != 0)
        {
            std::cerr << "Failed to initialize ENet" << std::endl;
            return false;
        }

        client = enet_host_create(nullptr, 1, 2, 0, 0);
        if (!client)
        {
            std::cerr << "Failed to create ENet client" << std::endl;
            return false;
        }

        if (SDL_Init(SDL_INIT_VIDEO) < 0)
        {
            std::cerr << "SDL initialization failed: " << SDL_GetError() << std::endl;
            return false;
        }

        window = SDL_CreateWindow(title,
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            800, 600, SDL_WINDOW_SHOWN);
        if (!window)
        {
            std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
            return false;
        }

        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
        if (!renderer)
        {
            std::cerr << "Renderer creation failed: " << SDL_GetError() << std::endl;
            return false;
        }

        std::cout << "Hot Update Client initialized" << std::endl;
        return true;
    }

    bool Connect(const char* address, enet_uint16 port)
    {
        ENetAddress enetAddress;
        enet_address_set_host(&enetAddress, address);
        enetAddress.port = port;

        server = enet_host_connect(client, &enetAddress, 2, 0);
        if (!server)
        {
            std::cerr << "Failed to connect to server" << std::endl;
            return false;
        }

        std::cout << "Connecting to " << address << ":" << port << "..." << std::endl;

        ENetEvent event;
        if (enet_host_service(client, &event, 3000) > 0 && event.type == ENET_EVENT_TYPE_CONNECT)
        {
            std::cout << "Connected to server successfully!" << std::endl;
            return true;
        }

        std::cerr << "Connection timeout" << std::endl;
        return false;
    }

    void Run()
    {
        auto lastTime = std::chrono::high_resolution_clock::now();

        while (running)
        {
            auto currentTime = std::chrono::high_resolution_clock::now();
            float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
            lastTime = currentTime;

            time += deltaTime;

            ProcessEvents();
            ProcessNetwork();
            Render();

            SDL_Delay(16);
        }
    }

private:
    void ProcessEvents()
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
                running = false;
            else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)
                running = false;
        }
    }

    void ProcessNetwork()
    {
        ENetEvent event;

        while (enet_host_service(client, &event, 0) > 0)
        {
            switch (event.type)
            {
            case ENET_EVENT_TYPE_CONNECT:
                std::cout << "Connected to server" << std::endl;
                break;

            case ENET_EVENT_TYPE_DISCONNECT:
                std::cout << "Disconnected from server" << std::endl;
                running = false;
                break;

            case ENET_EVENT_TYPE_RECEIVE:
                HandleScriptPacket(event.packet);
                enet_packet_destroy(event.packet);
                break;
            }
        }
    }

    void HandleScriptPacket(ENetPacket* packet)
    {
        try
        {
            // 反序列化脚本
            msgpack::object_handle oh = msgpack::unpack(
                reinterpret_cast<const char*>(packet->data),
                packet->dataLength
            );

            Script script = oh.get().as<Script>();

            std::cout << "\n=== Received script v" << script.version << " ===" << std::endl;

            // 执行Lua脚本
            if (lua.LoadScript(script.data, script.version))
            {
                std::cout << "Script loaded successfully" << std::endl;
            }
        }
        catch (const std::exception& e)
        {
            std::cerr << "Failed to process script: " << e.what() << std::endl;
        }
    }

    void Render()
    {
        SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
        SDL_RenderClear(renderer);

        RectangleParams params = lua.GetParams();

        float t = time * 2.0f;
        float currentSize = params.size + 50.0f * sin(t);

        int windowWidth, windowHeight;
        SDL_GetWindowSize(window, &windowWidth, &windowHeight);

        int rectX = (windowWidth - (int)currentSize) / 2;
        int rectY = (windowHeight - (int)currentSize) / 2;

        SDL_Rect rect = { rectX, rectY, (int)currentSize, (int)currentSize };

        SDL_SetRenderDrawColor(renderer, (int)(params.r), (int)(params.g), (int)(params.b), (int)(params.a));
        SDL_RenderFillRect(renderer, &rect);

        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderDrawRect(renderer, &rect);

        SDL_RenderPresent(renderer);
    }

public:
    ~HotUpdateClient()
    {
        if (server)
            enet_peer_disconnect(server, 0);

        if (client)
            enet_host_destroy(client);

        if (renderer)
            SDL_DestroyRenderer(renderer);

        if (window)
            SDL_DestroyWindow(window);

        enet_deinitialize();
        SDL_Quit();

        std::cout << "Client shutdown" << std::endl;
    }
};

int main(int argc, char* argv[])
{
    const char* serverAddress = "127.0.0.1";
    enet_uint16 port = 25565;

    if (argc > 1) serverAddress = argv[1];
    if (argc > 2) port = static_cast<enet_uint16>(atoi(argv[2]));

    HotUpdateClient client;

    if (client.Initialize("Lua Hot Update Demo"))
    {
        if (client.Connect(serverAddress, port))
            client.Run();
    }

    return 0;
}