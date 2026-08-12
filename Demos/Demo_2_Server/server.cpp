#define SDL_MAIN_HANDLED

#include <SDL.h>
#include <SDL_net.h>

#include <string>
#include <unordered_map>

// 数据包分隔符
static constexpr char delimiter = 0x1E;

// 客户端结构体
struct Client
{
	size_t id = -1;				// 用户ID
	TCPsocket socket = nullptr;	// socket
	std::string buffer_parse;	// 数据包缓冲区
};

// 客户端对象池
std::unordered_map<size_t, Client> client_pool;

void send_pkg(TCPsocket socket, const std::string& msg)
{
	std::string str_pkg = msg + delimiter;
	SDLNet_TCP_Send(socket, str_pkg.c_str(), (int)str_pkg.size());
}

void broadcast(const std::string& msg)
{
	std::string str_pkg = msg + delimiter;
	for (const auto& pair : client_pool)
	{
		const Client& client = pair.second;
		SDLNet_TCP_Send(client.socket, str_pkg.c_str(), (int)str_pkg.size());
	}
}

int main(int argc, char** argv)
{
	// 初始化SDL和网络库
	SDL_Init(SDL_INIT_EVERYTHING);
	SDLNet_Init();

	// 解析服务器地址
	IPaddress ip;


	// 把指定的主机名和端口号转换为一个IP地址结构，nullptr 表示0.0.0.0本机所有可用的网络接口。
	// 如果需要连接到远程主机，需要提供远程主机的名称或 IP 地址。
	if (SDLNet_ResolveHost(&ip, nullptr, 25565))
	{
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", u8"无法解析主机地址！", nullptr);
		return -1;
	}

	// 启动监听socket
	TCPsocket server;
	if (!(server = SDLNet_TCP_Open(&ip)))
	{
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", u8"无法启动服务器！", nullptr);
		return -1;
	}

	// 下一个用户ID
	size_t id_user_next = 0;

	// 创建socket集合
	SDLNet_SocketSet socket_set = SDLNet_AllocSocketSet(64);

	// 服务器主循环
	while (true)
	{
		// 接收新的socket连接
		while (TCPsocket new_socket = SDLNet_TCP_Accept(server))
		{
			// 如果添加新的socket失败则列表已满，断开连接
			if (SDLNet_TCP_AddSocket(socket_set, new_socket) < 0)
			{
				SDLNet_TCP_Close(new_socket);
				continue;
			}

			// 创建新的设备对象
			size_t new_id = id_user_next++;
			Client& new_client = client_pool[new_id];
			new_client.id = new_id;
			new_client.socket = new_socket;

			// 告知客户端用户ID
			send_pkg(new_client.socket, std::to_string(new_client.id));

			// 向所有用户广播新用户加入
			broadcast(u8"用户【" + std::to_string(new_client.id) + u8"】加入聊天室！");
		}

		// 获取活跃的socket数量
		if (int num_active_socket = SDLNet_CheckSockets(socket_set, 0))
		{
			// 当前帧断开连接的设备ID列表
			static std::vector<size_t> invalid_device_id_list;
			invalid_device_id_list.clear();

			// 当前已经检查过的活跃socket计数
			int counter_active_socket = 0;
			for (auto& pair : client_pool)
			{
				size_t id = pair.first;
				Client& client = pair.second;
				// 检查是否是当前socket活跃
				if (!SDLNet_SocketReady(client.socket))
					continue;
				// 检查了 socket 是否有数据可读，确保后续流程是非阻塞的

				// 接收数据
				static char recv_buffer[1024];
				int sz_recevied = SDLNet_TCP_Recv(client.socket, recv_buffer, 1024);
				// 这里的Recv默认是阻塞的，但是由于先前确认了有数据可读，达到了非阻塞的效果
				if (sz_recevied <= 0)
				{
					// 处理连接断开
					invalid_device_id_list.push_back(id);
					continue;
				}

				// 处理数据接收、解析
				// 每一个client有一个buffer_parse，包含了所有未被公布的信息
				client.buffer_parse.append(recv_buffer, sz_recevied);
				size_t offset = 0;
				while (true)
				{
					size_t pos = client.buffer_parse.find_first_of(delimiter, offset);
					if (pos == std::string::npos)
					{
						client.buffer_parse = client.buffer_parse.substr(offset);
						break;
					}
					std::string str_packet(client.buffer_parse.data() + offset, pos - offset);
					// 向所有用户广播收到的消息
					broadcast(u8"用户【" + std::to_string(client.id) + u8"】：" + str_packet);
					offset = pos + 1;
				}

				// 如果所有活跃socket都已处理则结束循环
				++counter_active_socket;
				if (counter_active_socket >= num_active_socket)
					break;
			}

			// 移除断开连接的设备
			for (size_t id : invalid_device_id_list)
			{
				
				SDLNet_DelSocket(socket_set, (SDLNet_GenericSocket)client_pool[id].socket);
				client_pool.erase(id);
				broadcast(u8"用户【" + std::to_string(id) + u8"】退出聊天室！");
				
			}
		}
	}

	// 销毁socket集合
	SDLNet_FreeSocketSet(socket_set);

	// 退出网络库和SDL
	SDLNet_Quit();
	SDL_Quit();

	return 0;
}