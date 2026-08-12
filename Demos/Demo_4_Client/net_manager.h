#pragma once

#include <cJSON.h>
#include <SDL_net.h>

#include <string>
#include <functional>
#include <unordered_map>

using RPCFunc = std::function<void(cJSON* params)>;

class NetManager
{
public:
	static NetManager* instance();

	bool connect(const char* address, int port);
	void register_rpc(const std::string& name, RPCFunc func);
	void unregister_rpc(const std::string& name);
	void process_rpc();
	void rpc_call(const char* func, cJSON* params, bool auto_gc = true);
	void set_on_disconnect(std::function<void()> callback);

private:
	NetManager();
	~NetManager();

private:
	static NetManager* manager;

	std::string buffer;
	TCPsocket socket = nullptr;
	SDLNet_SocketSet socket_set = nullptr;
	std::function<void()> on_disconnect;
	std::unordered_map<std::string, RPCFunc> rpc_func_pool;

};