#define SDL_MAIN_HANDLED

#include "animation.h"
#include "net_manager.h"

#include <SDL.h>

#include <string>
#include <chrono>
#include <thread>
#include <iostream>
#include <unordered_map>
#include <unordered_set>

struct Character
{
	int id = -1;
	float position = 0;
	Animation animation_idle;
	Animation animation_move;
	Animation* current_anim = nullptr;
};

struct Skin
{
	Atlas idle;
	Atlas move;
};

int main(int argc, char** argv)
{
	using namespace std::chrono;

	SDL_Init(SDL_INIT_EVERYTHING);
	SDLNet_Init();

	SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");

	SDL_Window* window = SDL_CreateWindow(u8"Mini Online Game",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, SDL_WINDOW_ALLOW_HIGHDPI);
	SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

	int id_self = -1;

	std::unordered_map<int, Character> chara_pool;
	std::unordered_map<std::string, Skin> skin_pool;

	skin_pool["Assassin"].idle.load(renderer, "Resources\\Character\\Assassin\\Assassin_idle_%d.png", 9);
	skin_pool["Assassin"].move.load(renderer, "Resources\\Character\\Assassin\\Assassin_run_%d.png", 8);
	skin_pool["Bazooka"].idle.load(renderer, "Resources\\Character\\Bazooka\\Bazooka_Panda_idle_%d.png", 12);
	skin_pool["Bazooka"].move.load(renderer, "Resources\\Character\\Bazooka\\Bazooka_Panda_run_%d.png", 12);
	skin_pool["Fox"].idle.load(renderer, "Resources\\Character\\Fox\\Fox_Sprite_Sheet_idle_%d.png", 5);
	skin_pool["Fox"].move.load(renderer, "Resources\\Character\\Fox\\Fox_Sprite_Sheet_run_%d.png", 5);
	skin_pool["Glitch_Samurai"].idle.load(renderer, "Resources\\Character\\Glitch_Samurai\\Glitch_Samurai_idle_%d.png", 11);
	skin_pool["Glitch_Samurai"].move.load(renderer, "Resources\\Character\\Glitch_Samurai\\Glitch_Samurai_run_%d.png", 12);
	skin_pool["Sword_Master"].idle.load(renderer, "Resources\\Character\\Sword_Master\\Sword_Master_Sprite_Sheet_idle_%d.png", 9);
	skin_pool["Sword_Master"].move.load(renderer, "Resources\\Character\\Sword_Master\\Sword_Master_Sprite_Sheet_run_%d.png", 8);
	skin_pool["The_Sage"].idle.load(renderer, "Resources\\Character\\The_Sage\\The_Sage_idle_%d.png", 9);
	skin_pool["The_Sage"].move.load(renderer, "Resources\\Character\\The_Sage\\The_Sage_run_%d.png", 8);

	SDL_Texture* texture_world = IMG_LoadTexture(renderer, "Resources\\BG.png");
	SDL_Texture* texture_cursor = IMG_LoadTexture(renderer, "Resources\\Cursor.png");

	NetManager::instance()->register_rpc("set_id", [&](cJSON* params)
		{
			if (params->type != cJSON_Number)
				return;

			id_self = params->valueint;
		});

	NetManager::instance()->register_rpc("sync", [&](cJSON* params)
		{
			if (params->type != cJSON_Array)
				return;

			cJSON* json_chara = nullptr;
			std::unordered_set<int> valid_id_pool;
			cJSON_ArrayForEach(json_chara, params)
			{
				cJSON* json_id = cJSON_GetObjectItem(json_chara, "id");
				cJSON* json_pos = cJSON_GetObjectItem(json_chara, "pos");
				cJSON* json_flip = cJSON_GetObjectItem(json_chara, "flip");
				cJSON* json_move = cJSON_GetObjectItem(json_chara, "move");
				cJSON* json_skin = cJSON_GetObjectItem(json_chara, "skin");

				valid_id_pool.insert(json_id->valueint);

				Character* chara = nullptr;
				auto itor = chara_pool.find(json_id->valueint);
				if (itor != chara_pool.end())
					chara = &itor->second;
				else
				{
					chara = &chara_pool[json_id->valueint];
					chara->animation_idle.add_frame(&skin_pool[json_skin->valuestring].idle);
					chara->animation_move.add_frame(&skin_pool[json_skin->valuestring].move);
					chara->animation_idle.set_interval(0.1f);
					chara->animation_move.set_interval(0.1f);
				}

				chara->position = (float)json_pos->valuedouble;
				chara->current_anim = json_move->valueint ? &chara->animation_move : &chara->animation_idle;
				chara->current_anim->set_position(Vector2((float)json_pos->valuedouble, 600));
				chara->current_anim->set_flip(json_flip->valueint);
			}

			std::vector<int> current_id_list;
			for (const auto& pair : chara_pool)
				current_id_list.push_back(pair.first);

			for (int id : current_id_list)
			{
				if (valid_id_pool.find(id) == valid_id_pool.end())
					chara_pool.erase(id);
			}
		});

	NetManager::instance()->set_on_disconnect([=]()
		{
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, u8"连接断开", u8"游戏将结束运行！", window);
			exit(-1);
		});

	if (!NetManager::instance()->connect("localhost", 25565))
	{
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, u8"连接失败", u8"无法连接到服务器！", window);
		exit(-1);
	}

	const nanoseconds frame_duration(1000000000 / 144);
	steady_clock::time_point last_tick = steady_clock::now();

	bool is_quit = false;

	while (!is_quit)
	{
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			if (event.type == SDL_QUIT)
				is_quit = true;
			else if (event.type == SDL_KEYDOWN)
			{
				switch (event.key.keysym.sym)
				{
				case SDLK_a:
				case SDLK_LEFT:
					NetManager::instance()->rpc_call("move_left", cJSON_CreateBool(true));
					break;
				case SDLK_d:
				case SDLK_RIGHT:
					NetManager::instance()->rpc_call("move_right", cJSON_CreateBool(true));
					break;
				}
			}
			else if (event.type == SDL_KEYUP)
			{
				switch (event.key.keysym.sym)
				{
				case SDLK_a:
				case SDLK_LEFT:
					NetManager::instance()->rpc_call("move_left", cJSON_CreateBool(false));
					break;
				case SDLK_d:
				case SDLK_RIGHT:
					NetManager::instance()->rpc_call("move_right", cJSON_CreateBool(false));
					break;
				}
			}
		}

		steady_clock::time_point frame_start = steady_clock::now();
		float delta = duration<float>(frame_start - last_tick).count();

		NetManager::instance()->process_rpc();

		for (const auto& pair : chara_pool)
			pair.second.current_anim->on_update(delta);

		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
		SDL_RenderClear(renderer);

		const SDL_Rect rect_dst_world = { 0, 0, 1280, 720 };
		SDL_RenderCopy(renderer, texture_world, nullptr, &rect_dst_world);
		for (const auto& pair : chara_pool)
			pair.second.current_anim->on_render(renderer);

		const Character& chara_self = chara_pool[id_self];
		const SDL_FRect rect_cursor = { chara_self.position - 200 / 2, 300, 200, 200 };
		SDL_RenderCopyF(renderer, texture_cursor, nullptr, &rect_cursor);

		SDL_RenderPresent(renderer);

		last_tick = frame_start;
		nanoseconds sleep_duration = frame_duration - (steady_clock::now() - frame_start);
		if (sleep_duration > nanoseconds(0))
			std::this_thread::sleep_for(sleep_duration);
	}

	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);

	SDLNet_Quit();
	SDL_Quit();

	return 0;
}