#define MSGPACK_NO_BOOST

#include <msgpack.hpp>
#include <string>
#include <vector>
#include <map>

// 使用C++类来定义游戏玩家数据
class GamePlayer
{
public:
    int player_id;
    std::string player_name;
    std::vector<float> position;           // x, y, z坐标
    int health;
    int level;
    std::vector<int> inventory;            // 背包物品ID数组
    std::map<std::string, int> skills;     // 技能等级映射

    // MSGPACK_DEFINE宏用于定义序列化字段
    MSGPACK_DEFINE(player_id, player_name, position, health, level, inventory, skills);
};

// 序列化函数
std::string serialize_player_to_msgpack(const GamePlayer& player)
{
    msgpack::sbuffer sbuf;  // 序列化缓冲区

    // 将对象打包到缓冲区
    msgpack::pack(sbuf, player);

    // 将二进制数据转换为字符串（实际使用时可能直接发送二进制数据）
    return std::string(sbuf.data(), sbuf.size());
}

// 反序列化函数
bool deserialize_player_from_msgpack(const std::string& data, GamePlayer& player)
{
    try
    {
        // 解析消息包数据
        msgpack::object_handle oh = msgpack::unpack(data.data(), data.size());
        msgpack::object obj = oh.get();

        // 将消息包对象转换回GamePlayer实例
        obj.convert(player);
        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

// 示例：手动序列化
void manual_msgpack_example()
{
    GamePlayer player;
    player.player_id = 1001;
    player.player_name = "TestPlayer";
    player.position = { 10.5f, 20.3f, 5.7f };
    player.health = 100;
    player.level = 5;
    player.inventory = { 101, 205, 308, 0, 0 };
    player.skills = { {"attack", 3}, {"defense", 2}, {"magic", 4} };

    // 手动创建消息包结构
    msgpack::sbuffer sbuf;
    msgpack::packer<msgpack::sbuffer> packer(sbuf);

    // 打包为数组格式 [id, name, position, health, level, inventory, skills]
    packer.pack_array(7);  // 7个元素

    packer.pack(player.player_id);
    packer.pack(player.player_name);

    // 打包位置数组
    packer.pack_array(3);
    for (float pos : player.position)
    {
        packer.pack(pos);
    }

    packer.pack(player.health);
    packer.pack(player.level);

    // 打包背包数组
    packer.pack_array((uint32_t)player.inventory.size());
    for (int item : player.inventory)
    {
        packer.pack(item);
    }

    // 打包技能映射
    packer.pack_map((uint32_t)player.skills.size());
    for (const auto& skill : player.skills)
    {
        packer.pack(skill.first);   // 键（技能名）
        packer.pack(skill.second);   // 值（技能等级）
    }
}

int main(int argc, char** argv)
{
    GamePlayer game_player;

    std::string data = serialize_player_to_msgpack(game_player);
    deserialize_player_from_msgpack(data, game_player);

    manual_msgpack_example();

    return 0;
}