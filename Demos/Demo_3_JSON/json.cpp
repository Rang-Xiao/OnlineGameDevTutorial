#include "cJSON.h"

#include <string>
#include <fstream>
#include <iostream>

// 定义要序列化的数据结构
struct GamePlayer
{
    int player_id;
    std::string player_name;
    float position[3];       // x, y, z坐标
    int health;
    int level;
    int inventory[5];        // 背包物品ID数组
    int skill_levels[3];     // 技能等级数组
};

// 序列化函数
std::string serialize_player_to_json(const struct GamePlayer* player)
{
    // 创建cJSON根对象
    cJSON* root = cJSON_CreateObject();

    // 添加基本类型字段
    cJSON_AddNumberToObject(root, "player_id", player->player_id);
    cJSON_AddStringToObject(root, "player_name", player->player_name.c_str());
    cJSON_AddNumberToObject(root, "health", player->health);
    cJSON_AddNumberToObject(root, "level", player->level);

    // 添加浮点数数组（位置坐标）
    cJSON* position_array = cJSON_CreateArray();
    for (int i = 0; i < 3; i++)
    {
        cJSON_AddItemToArray(position_array, cJSON_CreateNumber(player->position[i]));
    }
    cJSON_AddItemToObject(root, "position", position_array);

    // 添加整数数组（背包物品）
    cJSON* inventory_array = cJSON_CreateArray();
    for (int i = 0; i < 5; i++)
    {
        cJSON_AddItemToArray(inventory_array, cJSON_CreateNumber(player->inventory[i]));
    }
    cJSON_AddItemToObject(root, "inventory", inventory_array);

    // 添加技能等级字典（使用对象模拟字典）
    cJSON* skills_object = cJSON_CreateObject();
    const char* skill_names[] = { "attack", "defense", "magic" };
    for (int i = 0; i < 3; i++)
    {
        cJSON_AddNumberToObject(skills_object, skill_names[i], player->skill_levels[i]);
    }
    cJSON_AddItemToObject(root, "skills", skills_object);

    // 将cJSON对象转换为字符串
    char* json_buffer = cJSON_PrintUnformatted(root);
    std::string json_string = std::string(json_buffer);

    // 清理cJSON字符串缓冲
    free(json_buffer);

    // 清理cJSON对象
    cJSON_Delete(root);

    return json_string;
}

// 反序列化函数
int deserialize_player_from_json(const std::string& json_string, struct GamePlayer* player)
{
    // 解析JSON字符串
    cJSON* root = cJSON_Parse(json_string.c_str());
    if (!root) return false;

    // 提取基本类型字段
    cJSON* player_id_item = cJSON_GetObjectItem(root, "player_id");
    if (cJSON_IsNumber(player_id_item))
    {
        player->player_id = player_id_item->valueint;
    }

    cJSON* name_item = cJSON_GetObjectItem(root, "player_name");
    if (cJSON_IsString(name_item))
    {
        player->player_name = name_item->valuestring;
    }

    cJSON* health_item = cJSON_GetObjectItem(root, "health");
    if (cJSON_IsNumber(health_item))
    {
        player->health = health_item->valueint;
    }

    cJSON* level_item = cJSON_GetObjectItem(root, "level");
    if (cJSON_IsNumber(level_item))
    {
        player->level = level_item->valueint;
    }

    // 提取位置数组
    cJSON* position_array = cJSON_GetObjectItem(root, "position");
    if (cJSON_IsArray(position_array))
    {
        int array_size = cJSON_GetArraySize(position_array);
        int copy_size = (array_size < 3) ? array_size : 3;
        for (int i = 0; i < copy_size; i++)
        {
            cJSON* item = cJSON_GetArrayItem(position_array, i);
            if (cJSON_IsNumber(item))
            {
                player->position[i] = (float)item->valuedouble;
            }
        }
    }

    // 提取背包数组
    cJSON* inventory_array = cJSON_GetObjectItem(root, "inventory");
    if (cJSON_IsArray(inventory_array))
    {
        int array_size = cJSON_GetArraySize(inventory_array);
        int copy_size = (array_size < 5) ? array_size : 5;
        for (int i = 0; i < copy_size; i++)
        {
            cJSON* item = cJSON_GetArrayItem(inventory_array, i);
            if (cJSON_IsNumber(item))
            {
                player->inventory[i] = item->valueint;
            }
        }
    }

    // 提取技能字典
    cJSON* skills_object = cJSON_GetObjectItem(root, "skills");
    if (cJSON_IsObject(skills_object))
    {
        const char* skill_names[] = { "attack", "defense", "magic" };
        for (int i = 0; i < 3; i++)
        {
            cJSON* skill_item = cJSON_GetObjectItem(skills_object, skill_names[i]);
            if (cJSON_IsNumber(skill_item))
            {
                player->skill_levels[i] = skill_item->valueint;
            }
        }
    }

    cJSON_Delete(root);
    return true;
}

int main(int argc, char** argv)
{
    GamePlayer game_player = {
        1001,
        "Alice",
        { 12.5f, 64.0f, -34.25f },
        95,
        7,
        { 101, 102, 0, 0, 205 },
        { 5, 3, 8 }
    };

    std::string json_string = serialize_player_to_json(&game_player);
    std::cout << json_string << std::endl;

    cJSON* root = cJSON_Parse(json_string.c_str());
    char* pretty = cJSON_Print(root);

    std::ofstream file("test.json");
    file << pretty;
    file.close();

    free(pretty);
    cJSON_Delete(root);

    GamePlayer restored{};
    deserialize_player_from_json(json_string, &restored);
    std::cout << restored.player_name << " lv" << restored.level
        << " hp" << restored.health << std::endl;

    return 0;
}