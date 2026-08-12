#include "net_manager.h"

static constexpr char delimiter = 0x1E;
static constexpr size_t RECV_BUFFER_SIZE = 100 * 1024 * 1024;

NetManager* NetManager::manager = nullptr;

NetManager* NetManager::instance()
{
    if (!manager)
        manager = new NetManager();

    return manager;
}

bool NetManager::connect(const char* address, int port)
{
    IPaddress ip;
    if (SDLNet_ResolveHost(&ip, address, port))
        return false;

    if (!(socket = SDLNet_TCP_Open(&ip)))
        return false;

    socket_set = SDLNet_AllocSocketSet(1);
    SDLNet_TCP_AddSocket(socket_set, socket);

    return true;
}

void NetManager::register_rpc(const std::string& name, RPCFunc func)
{
    rpc_func_pool[name] = func;
}

void NetManager::unregister_rpc(const std::string& name)
{
    rpc_func_pool.erase(name);
}

void NetManager::process_rpc()
{
    if (SDLNet_CheckSockets(socket_set, 0) <= 0)
        return;

    static char recv_buffer[RECV_BUFFER_SIZE];
    int sz_recevied = SDLNet_TCP_Recv(socket, recv_buffer, RECV_BUFFER_SIZE);
    if (sz_recevied <= 0 && on_disconnect)
    {
        on_disconnect();
        return;
    }

    buffer.append(recv_buffer, sz_recevied);

    size_t offset = 0;
    while (true)
    {
        size_t pos = buffer.find_first_of(delimiter, offset);
        if (pos == std::string::npos)
        {
            buffer = buffer.substr(offset);
            break;
        }
        std::string_view strv_packet(buffer.data() + offset, pos - offset);
        cJSON* json = cJSON_ParseWithLength(strv_packet.data(), strv_packet.size());
        if (json)
        {
            if (json->type == cJSON_Object)
            {
                cJSON* json_func = cJSON_GetObjectItem(json, "f");
                cJSON* json_params = cJSON_GetObjectItem(json, "p");
                if (json_func && json_params && json_func->type == cJSON_String)
                {
                    const auto& itor = rpc_func_pool.find(json_func->valuestring);
                    if (itor != rpc_func_pool.end())
                        itor->second(json_params);
                }
            }
            cJSON_Delete(json);
        }
        offset = pos + 1;
    }
}

void NetManager::rpc_call(const char* func, cJSON* params, bool auto_gc)
{
    cJSON* json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "f", func);
    if (auto_gc)
        cJSON_AddItemToObject(json, "p", params);
    else
        cJSON_AddItemReferenceToObject(json, "p", params);
    char* str_json = cJSON_PrintUnformatted(json);
    std::string package = str_json; package += delimiter;
    free(str_json); cJSON_Delete(json);
    SDLNet_TCP_Send(socket, package.c_str(), (int)package.size());
}

void NetManager::set_on_disconnect(std::function<void()> callback)
{
    this->on_disconnect = callback;
}

NetManager::NetManager() = default;

NetManager::~NetManager() = default;