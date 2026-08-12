#define SDL_MAIN_HANDLED

#include <cJSON.h>
#include <SDL_net.h>

#include <chrono>
#include <thread>
#include <string>
#include <vector>
#include <iostream>
#include <functional>
#include <string_view>
#include <unordered_map>

struct Client
{
	int id;
	TCPsocket socket;
	std::string buffer;

	float pos = 640;
	std::string skin_id;
	bool is_flip = false;
	bool is_move_left = false;
	bool is_move_right = false;
};

static constexpr char delimiter = 0x1E;
static constexpr int MAX_CLIENT_NUM = 32;
static constexpr size_t RECV_BUFFER_SIZE = 100 * 1024 * 1024;

static int id_next = 0;
static int id_skin_next = 0;

using RPCFunc = std::function<void(Client& client, cJSON* params)>;

static std::unordered_map<int, Client> client_pool;
static std::unordered_map<std::string, RPCFunc> rpc_func_pool;

static const std::vector<std::string> skin_list =
{
	"Assassin", "Bazooka", "Fox", "Glitch_Samurai", "Sword_Master", "The_Sage"
};

static inline void pack(std::string& data)
{
	data += delimiter;
}

static inline void send_client(TCPsocket socket, std::string data)
{
	pack(data);
	SDLNet_TCP_Send(socket, data.c_str(), (int)data.size());
}

static inline void call_client(TCPsocket socket, const char* func, cJSON* params, bool auto_gc = true)
{
	cJSON* json = cJSON_CreateObject();
	cJSON_AddStringToObject(json, "f", func);
	if (auto_gc)
		cJSON_AddItemToObject(json, "p", params);
	else
		cJSON_AddItemReferenceToObject(json, "p", params);
	char* str_json = cJSON_PrintUnformatted(json);
	send_client(socket, str_json);
	free(str_json);
	cJSON_Delete(json);
}

static void process_rpc(Client& client)
{
	size_t offset = 0;
	while (true)
	{
		size_t pos = client.buffer.find_first_of(delimiter, offset);
		if (pos == std::string::npos)
		{
			client.buffer = client.buffer.substr(offset);
			break;
		}
		std::string_view strv_packet(client.buffer.data() + offset, pos - offset);
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
						itor->second(client, json_params);
				}
			}
			cJSON_Delete(json);
		}
		offset = pos + 1;
	}
}

static void rpc_move_left(Client& client, cJSON* params)
{
	client.is_move_left = params->valueint;
}

static void rpc_move_right(Client& client, cJSON* params)
{
	client.is_move_right = params->valueint;
}

static void init_rpc_func()
{
	rpc_func_pool["move_left"] = rpc_move_left;
	rpc_func_pool["move_right"] = rpc_move_right;
}

static void broadcast(const char* func, cJSON* params)
{
	cJSON* json = cJSON_CreateObject();
	cJSON_AddStringToObject(json, "f", func);
	cJSON_AddItemToObject(json, "p", params);
	char* str_json = cJSON_PrintUnformatted(json);
	std::string package = str_json; pack(package);
	free(str_json); cJSON_Delete(json);

	for (const auto& pair : client_pool)
		SDLNet_TCP_Send(pair.second.socket, package.c_str(), (int)package.size());
}

int main(int argc, char** argv)
{
	using namespace std::chrono;

	SDL_Init(SDL_INIT_EVERYTHING);
	SDLNet_Init();

	IPaddress ip;
	SDLNet_ResolveHost(&ip, nullptr, 25565);

	TCPsocket server = SDLNet_TCP_Open(&ip);
	SDLNet_SocketSet socket_set = SDLNet_AllocSocketSet(MAX_CLIENT_NUM);

	init_rpc_func();

	steady_clock::time_point last_tick = steady_clock::now();

	while (true)
	{
		steady_clock::time_point frame_start = steady_clock::now();
		float delta = duration<float>(frame_start - last_tick).count();
		last_tick = frame_start;

		while (TCPsocket new_socket = SDLNet_TCP_Accept(server))
		{
			if (SDLNet_TCP_AddSocket(socket_set, new_socket) < 0)
			{
				SDLNet_TCP_Close(new_socket);
				continue;
			}

			int new_id = id_next++;
			Client& new_client = client_pool[new_id];

			new_client.id = new_id;
			new_client.socket = new_socket;
			new_client.skin_id = skin_list[id_skin_next++ % skin_list.size()];

			call_client(new_client.socket, "set_id", cJSON_CreateNumber(new_client.id));
		}

		if (int num_active_socket = SDLNet_CheckSockets(socket_set, 0))
		{
			int counter_active_socket = 0;
			std::vector<int> invalid_client_id_list;

			for (auto& pair : client_pool)
			{
				int id = pair.first;
				Client& client = pair.second;

				if (!SDLNet_SocketReady(client.socket))
					continue;

				static char recv_buffer[RECV_BUFFER_SIZE];
				int sz_recevied = SDLNet_TCP_Recv(client.socket, recv_buffer, RECV_BUFFER_SIZE);
				if (sz_recevied <= 0)
				{
					invalid_client_id_list.push_back(id);
					continue;
				}

				client.buffer.append(recv_buffer, sz_recevied);
				process_rpc(client);

				++counter_active_socket;
				if (counter_active_socket >= num_active_socket)
					break;
			}

			for (int id : invalid_client_id_list)
			{
				SDLNet_DelSocket(socket_set, (SDLNet_GenericSocket)client_pool[id].socket);
				client_pool.erase(id);
			}
		}

		for (auto& pair : client_pool)
		{
			Client& client = pair.second;
			int dir = client.is_move_right - client.is_move_left;
			if (dir != 0)
			{
				client.is_flip = dir < 0;
				client.pos += 300 * delta * dir;
				if (client.pos < 0) client.pos = 0;
				if (client.pos > 1280) client.pos = 1280;
			}
		}

		cJSON* json_broadcast = cJSON_CreateArray();
		for (const auto& pair : client_pool)
		{
			const Client& client = pair.second;
			cJSON* json_client = cJSON_CreateObject();
			cJSON_AddNumberToObject(json_client, "id", client.id);
			cJSON_AddNumberToObject(json_client, "pos", client.pos);
			cJSON_AddBoolToObject(json_client, "flip", client.is_flip);
			cJSON_AddBoolToObject(json_client, "move", (client.is_move_left - client.is_move_right));
			cJSON_AddStringToObject(json_client, "skin", client.skin_id.c_str());
			cJSON_AddItemToArray(json_broadcast, json_client);
		}
		broadcast("sync", json_broadcast);
	}

	return 0;
}