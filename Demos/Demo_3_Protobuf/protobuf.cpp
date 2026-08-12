#include "game_player.pb.h"  // 由protoc生成的头文件

#include <string>

class ProtobufSerializer
{
public:
    // 序列化GamePlayer对象到字符串
    static std::string serialize_to_protobuf(const game::GamePlayer& player)
    {
        std::string serialized_data;
        player.SerializeToString(&serialized_data);
        return serialized_data;
    }

    // 从字符串反序列化到GamePlayer对象
    static bool deserialize_from_protobuf(const std::string& data, game::GamePlayer* player)
    {
        return player->ParseFromString(data);
    }
};

int main(int argc, char** argv)
{
    // 创建并配置玩家对象
    game::GamePlayer player;
    player.set_player_id(2001);
    player.set_player_name("Alice");
    player.set_health(85);
    player.set_level(3);

    // 设置位置
    game::Vec3* pos = player.mutable_position();
    pos->set_x(15.2f);
    pos->set_y(8.7f);
    pos->set_z(12.4f);

    // 添加背包物品
    player.add_inventory(150);
    player.add_inventory(0);
    player.add_inventory(0);
    player.add_inventory(0);
    player.add_inventory(0);

    // 设置技能
    (*player.mutable_skills())["attack"] = 2;
    (*player.mutable_skills())["defense"] = 3;

    // 序列化
    std::string binary_data = ProtobufSerializer::serialize_to_protobuf(player);

    std::cout << "Serialized data size: " << binary_data.size() << " bytes" << std::endl;

    // 反序列化
    game::GamePlayer new_player;
    if (ProtobufSerializer::deserialize_from_protobuf(binary_data, &new_player))
    {
        std::cout << "Deserialized player: " << new_player.player_name()
            << ", HP: " << new_player.health() << std::endl;
    }
}