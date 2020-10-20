#include "PlayMode.hpp"

#include "LitColorTextureProgram.hpp"

#include "DrawLines.hpp"
#include "Mesh.hpp"
#include "Load.hpp"
#include "gl_errors.hpp"
#include "data_path.hpp"
#include "Sound.hpp"

#include <math.h> 
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>

#include <random>

//Floating point equality from 
//http://www.cs.technion.ac.il/users/yechiel/c++-faq/floating-point-arith.html
inline bool isEqual(float x, float y) { 
  const float epsilon = 0.1f;
  return std::abs(x - y) <= epsilon * std::abs(x);
}

Load< Sound::Sample > big_robot_hit(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("big_robot_hit.wav"));
});

Load< Sound::Sample > enemy_hit(LoadTagDefault, []() -> Sound::Sample const * {
 	return new Sound::Sample(data_path("enemy_hit.wav"));
});

Load< Sound::Sample > cargo_lost(LoadTagDefault, []() -> Sound::Sample const * {
 	return new Sound::Sample(data_path("cargo.wav"));
});

Load< Sound::Sample > pew(LoadTagDefault, []() -> Sound::Sample const * {
 	return new Sound::Sample(data_path("shoot.wav"));
});

GLuint phonebank_meshes_for_lit_color_texture_program = 0;
Load< MeshBuffer > phonebank_meshes(LoadTagDefault, []() -> MeshBuffer const * {
	MeshBuffer const *ret = new MeshBuffer(data_path("place.pnct"));
	phonebank_meshes_for_lit_color_texture_program = ret->make_vao_for_program(lit_color_texture_program->program);
	return ret;
});

Load< Scene > phonebank_scene(LoadTagDefault, []() -> Scene const * {
	return new Scene(data_path("place.scene"), [&](Scene &scene, Scene::Transform *transform, std::string const &mesh_name){
		Mesh const &mesh = phonebank_meshes->lookup(mesh_name);

		scene.drawables.emplace_back(transform);
		Scene::Drawable &drawable = scene.drawables.back();

		drawable.pipeline = lit_color_texture_program_pipeline;

		drawable.pipeline.vao = phonebank_meshes_for_lit_color_texture_program;
		drawable.pipeline.type = mesh.type;
		drawable.pipeline.start = mesh.start;
		drawable.pipeline.count = mesh.count;

	});
});

WalkMesh const *walkmesh = nullptr;
Load< WalkMeshes > phonebank_walkmeshes(LoadTagDefault, []() -> WalkMeshes const * {
	WalkMeshes *ret = new WalkMeshes(data_path("place.w"));
	walkmesh = &ret->lookup("WalkMesh");
	return ret;
});

PlayMode::PlayMode() : scene(*phonebank_scene) {
	//create a player transform:
	for (auto drawable : scene.drawables) {
		if (drawable.transform->name == "Torus") {
			enemy = &drawable;
			eTrans = drawable.transform;
			ePipe = drawable.pipeline;
		} else if (drawable.transform->name == "Icosphere") {
			bullet = &drawable;
			bTrans = drawable.transform;
			bPipe = drawable.pipeline;
		} else if (drawable.transform->name == "Cube.001" || drawable.transform->name == "Cube.006" ||
					drawable.transform->name == "Cube.002" || drawable.transform->name == "Cube.003" || 
					drawable.transform->name == "Cube.004" || drawable.transform->name == "Cube.005") {
			cargo.push_back(drawable.transform);
		} else if (drawable.transform->name == "Robot") {
			robot = drawable.transform;
		}
	}

	glGenBuffers(1, &vertex_buffer);
	scene.transforms.emplace_back();
	player.transform = &scene.transforms.back();

	//create a player camera attached to a child of the player transform:
	scene.transforms.emplace_back();
	scene.cameras.emplace_back(&scene.transforms.back());
	player.camera = &scene.cameras.back();
	player.camera->fovy = glm::radians(60.0f);
	player.camera->near = 0.01f;
	player.camera->transform->parent = player.transform;

	//player's eyes are 1.8 units above the ground:
	player.camera->transform->position = glm::vec3(0.0f, 0.0f, 1.8f);

	//rotate camera facing direction (-z) to player facing direction (+y):
	player.camera->transform->rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
	//start player walking at nearest walk point:
	player.at = walkmesh->nearest_walk_point(player.transform->position);
}

PlayMode::~PlayMode() {
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {

	if (win || lose) return false;

	if (evt.type == SDL_KEYDOWN) {
		if (evt.key.keysym.sym == SDLK_ESCAPE) {
			SDL_SetRelativeMouseMode(SDL_FALSE);
			return true;
		} else if (evt.key.keysym.sym == SDLK_a) {
			left.downs += 1;
			left.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_d) {
			right.downs += 1;
			right.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_w) {
			up.downs += 1;
			up.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_s) {
			down.downs += 1;
			down.pressed = true;
			return true;
		}
	} else if (evt.type == SDL_KEYUP) {
		if (evt.key.keysym.sym == SDLK_a) {
			left.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_d) {
			right.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_w) {
			up.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_s) {
			down.pressed = false;
			return true;
		}
	} else if (evt.type == SDL_MOUSEBUTTONDOWN) {
		shot.pressed = true;
		if (SDL_GetRelativeMouseMode() == SDL_FALSE) {
			SDL_SetRelativeMouseMode(SDL_TRUE);
			//return true;
		}
	} else if (evt.type == SDL_MOUSEMOTION) {
		if (SDL_GetRelativeMouseMode() == SDL_TRUE) {
			glm::vec2 motion = glm::vec2(
				evt.motion.xrel / float(window_size.y),
				-evt.motion.yrel / float(window_size.y)
			);
			glm::vec3 up = walkmesh->to_world_smooth_normal(player.at);
			player.transform->rotation = glm::angleAxis(-motion.x * player.camera->fovy, up) * player.transform->rotation;

			float pitch = glm::pitch(player.camera->transform->rotation);
			pitch += motion.y * player.camera->fovy;
			//camera looks down -z (basically at the player's feet) when pitch is at zero.
			pitch = std::min(pitch, 0.95f * 3.1415926f);
			pitch = std::max(pitch, 0.05f * 3.1415926f);
			player.camera->transform->rotation = glm::angleAxis(pitch, glm::vec3(1.0f, 0.0f, 0.0f));
			//printf("camera2 %f %f %f\n", player.camera->transform->rotation.x, player.camera->transform->rotation.y, player.camera->transform->rotation.z);
			return true;
		}
	}

	return false;
}

void PlayMode::update(float elapsed) {
	//player walking:
	//printf("bullet now %p %d %p\n", bullet, bullet->pipeline.count, bTrans);

	if (win || lose) return;
	{
		//combine inputs into a move:
		constexpr float PlayerSpeed = 3.0f;
		glm::vec2 move = glm::vec2(0.0f);
		if (left.pressed && !right.pressed) move.x =-1.0f;
		if (!left.pressed && right.pressed) move.x = 1.0f;
		if (down.pressed && !up.pressed) move.y =-1.0f;
		if (!down.pressed && up.pressed) move.y = 1.0f;
		if (shot.pressed) {
			shot.pressed = false;
			shoot();
		}

		move_bullets(elapsed);
		generate_bot(elapsed);
		move_enemies();
		enemy_die();
		cargo_taken();
		robot_damage(elapsed);

		//make it so that moving diagonally doesn't go faster:
		if (move != glm::vec2(0.0f)) move = glm::normalize(move) * PlayerSpeed * elapsed;
		move *= 2.0f;
		//get move in world coordinate system:
		glm::vec3 remain = player.transform->make_local_to_world() * glm::vec4(move.x, move.y, 0.0f, 0.0f);
		//printf("%f %f %f\n", remain.x, remain.y, remain.z);
		//using a for() instead of a while() here so that if walkpoint gets stuck in
		// some awkward case, code will not infinite loop:
		for (uint32_t iter = 0; iter < 10; ++iter) {
			if (remain == glm::vec3(0.0f)) break;
			WalkPoint end;
			float time;
			//printf("call walk in triangle %f %f %f\n", player.at.weights.x, player.at.weights.y, player.at.weights.z);
			walkmesh->walk_in_triangle(player.at, remain, &end, &time);
			//printf("done walk in triangle %f %f %f\n", end.weights.x, end.weights.y, end.weights.z);
			player.at = end;
			if (time == 1.0f) {
				//finished within triangle:
				remain = glm::vec3(0.0f);
				break;
			}
			//some step remains:
			remain *= (1.0f - time);
			//try to step over edge:
			glm::quat rotation;
			//printf("call cross edge %f %f %f\n", player.at.weights.x, player.at.weights.y, player.at.weights.z);
			if (walkmesh->cross_edge(player.at, &end, &rotation)) {
				//stepped to a new triangle:
				player.at = end;
				//rotate step to follow surface:
				remain = rotation * remain;
			} else {
				//ran into a wall, bounce / slide along it:
				glm::vec3 const &a = walkmesh->vertices[player.at.indices.x];
				glm::vec3 const &b = walkmesh->vertices[player.at.indices.y];
				glm::vec3 const &c = walkmesh->vertices[player.at.indices.z];
				glm::vec3 along = glm::normalize(b-a);
				glm::vec3 normal = glm::normalize(glm::cross(b-a, c-a));
				glm::vec3 in = glm::cross(normal, along);

				//check how much 'remain' is pointing out of the triangle:
				float d = glm::dot(remain, in);
				if (d < 0.0f) {
					//bounce off of the wall:
					remain += (-1.25f * d) * in;
				} else {
					//if it's just pointing along the edge, bend slightly away from wall:
					remain += 0.01f * d * in;
				}
			}
			//printf("done cross edge, %f %f %f\n", end.weights.x, end.weights.y, end.weights.z);
		}

		if (remain != glm::vec3(0.0f)) {
			std::cout << "NOTE: code used full iteration budget for walking." << std::endl;
		}
		//update player's position to respect walking:
		player.transform->position = walkmesh->to_world_point(player.at);
		{ //update player's rotation to respect local (smooth) up-vector:
			
			glm::quat adjust = glm::rotation(
				player.transform->rotation * glm::vec3(0.0f, 0.0f, 1.0f), //current up vector
				walkmesh->to_world_smooth_normal(player.at) //smoothed up vector at walk location
			);
			player.transform->rotation = glm::normalize(adjust * player.transform->rotation);
		}
		/*
		//glm::vec3 up = frame[1];
		glm::vec3 forward = -frame[2];
		camera->transform->position += move.x * right + move.y * forward;
		*/
		glm::mat4x3 frame = player.camera->transform->make_local_to_parent();
		glm::vec3 right = frame[0];
		Sound::listener.set_position_right(player.transform->position, right, 1.0f / 60.0f);
		
	}
		//printf("%f %f %f\n", player.transform->position.x, player.transform->position.y, player.transform->position.z);

	//reset button press counters:
	left.downs = 0;
	right.downs = 0;
	up.downs = 0;
	down.downs = 0;
}

void PlayMode::shoot() {
	bullet->transform = bTrans;
	Scene::Drawable *new_bullet = new Scene::Drawable();
	new_bullet->pipeline = bPipe;

	Scene::Transform *t = new Scene::Transform;
	t->rotation = player.transform->rotation;
	t->position = player.transform->position;
	t->scale = bullet->transform->scale;
	t->name = "bullet";
	//t->parent = player.transform;
	scene.transforms.emplace_back(*t);

	new_bullet->transform = t;
	bullet_info *bi = new bullet_info;
	bi->t = t;
	//bi->dir = normalize(glm::vec3(inv[2]));;
	//printf("dir %f %f %f\n", bi->dir.x, bi->dir.y, bi->dir.z);
	scene.drawables.emplace_back(*new_bullet);
	bullets.push_back(bi);
	// auto yaw = glm::yaw(t->rotation) * (180.0f / 3.14159265f) * 60.0f;
	// auto roll = glm::roll(t->rotation) * (180.0f / 3.14159265f);
	// auto p = glm::pitch(t->rotation) * (180.0f / 3.14159265f) * 60.0f;
	// printf("yaw %f %f %f\n", yaw, roll, p);
	Sound::play(*pew, 1.0f, 0.0f);
}

void PlayMode::move_bullets(float elapsed) {
	for (size_t i = 0; i < bullets.size(); i++) {
		bullets[i]->age += elapsed;
		auto move = glm::vec3(3.0f, 3.0f, 3.0f);
		glm::mat4x3 frame = bullets[i]->t->make_local_to_parent();
		//glm::vec3 right = frame[0];
		glm::vec3 up = frame[1];
		glm::vec3 forward = -frame[2];
		bullets[i]->t->position += up * move.z - (forward/2.0f) * move.y;
	}
	if (bullets.size() > 0 && bullets.front()->age > 3.0f) {
		bullets.front()->t->scale = glm::vec3(0.0f,0.0f,0.0f);
		bullets.front()->t->position = glm::vec3(0.0f, 0.0f, -100.0f);
		bullets.pop_front();
	}
}

void PlayMode::generate_bot(float elapsed) {
	bot_gen += elapsed;
	if (bot_gen > bot_time && enemies.size() < 10) {
		enemy->transform = eTrans;
		Scene::Drawable *new_enemy = new Scene::Drawable();
		new_enemy->pipeline = ePipe;

		Scene::Transform *t = new Scene::Transform;
		t->rotation = enemy->transform->rotation;
		t->position = enemy->transform->position;
		t->scale = enemy->transform->scale;
		t->name = "enemy";
		//t->parent = player.transform;
		scene.transforms.emplace_back(*t);

		new_enemy->transform = t;
		//printf("%f %f %f\n", player.transform->position.x, player.transform->position.y, player.transform->position.z);
		//printf("%f %f %f\n", new_bullet->transform->position.x, new_bullet->transform->position.y, new_bullet->transform->position.z);

		enemy_info *ei = new enemy_info;
		ei->t = t;
		int target = rand() % cargo.size();
		ei->dir = cargo[target]->position;

		scene.drawables.emplace_back(*new_enemy);
		enemies.push_back(ei);
		bot_time = bot_gen + 4.0f;
	}
}

void PlayMode::move_enemies() {
	for (size_t i = 0; i < enemies.size(); i++) {
		auto target = enemies[i]->dir;
		if (enemies[i]->t->position.x < target.x) {
			enemies[i]->t->position.x += 0.05f;
		} else if (enemies[i]->t->position.x > target.x) {
			enemies[i]->t->position.x -= 0.05f;
		}

		if (enemies[i]->t->position.y < target.y) {
			enemies[i]->t->position.y += 0.05f;
		} else if (enemies[i]->t->position.y > target.y) {
			enemies[i]->t->position.y -= 0.05f;
		}

		if (isEqual(enemies[i]->t->position.y, target.y) &&
			isEqual(enemies[i]->t->position.x, target.x)) {
			if (enemies[i]->t->position.z > target.z) {
				enemies[i]->t->position.z -= 0.05f;
			}
		}
	}
}

void PlayMode::cargo_taken() {
	for (size_t i = 0; i < cargo.size(); i++) {
		for (size_t j = 0; j < enemies.size(); j++) {
			if (std::max(enemies[j]->t->position[0] - 0.8f, cargo[i]->position[0] - 0.8f) <= std::min(enemies[j]->t->position[0] + 0.8f, cargo[i]->position[0] + 0.8f) &&
			std::max(enemies[j]->t->position[1] - 0.8f, cargo[i]->position[1] - 0.8f) <= std::min(enemies[j]->t->position[1] + 0.8f, cargo[i]->position[1] + 0.8f) && 
			std::max(enemies[j]->t->position[2] - 0.8f, cargo[i]->position[2] - 0.8f) <= std::min(enemies[j]->t->position[2] + 0.8f, cargo[i]->position[2] + 0.8f)) {
				cargo[i]->position = glm::vec3(0.0f, 0.0f, -100.0f);
				enemies[j]->t->position = glm::vec3(0.0f, 0.0f, -100.0f);
				cargo[i]->scale = glm::vec3(0.0f, 0.0f, 0.0f);
				enemies[j]->t->scale = glm::vec3(0.0f, 0.0f, 0.0f);
				enemies.erase(enemies.begin() + j);
				cargo.erase(cargo.begin() + i);
				Sound::play(*cargo_lost, 1.0f, 0.0f);
				if (cargo.size() <= 0) {
					lose = true;
					return;
				}
				for (size_t j = 0; j < enemies.size(); j++) {
					int target = rand() % cargo.size();
					enemies[j]->dir = cargo[target]->position;
				}
				return;
			}
		}
	}
}

void PlayMode::robot_damage(float elapsed) {
	hit_invinc += elapsed;
	for (size_t j = 0; j < bullets.size(); j++) {
		if (std::max(bullets[j]->t->position[0] - 0.1f, robot->position[0] - 10.0f) <= std::min(bullets[j]->t->position[0] + 0.1f, robot->position[0] + 10.0f) &&
			std::max(bullets[j]->t->position[1] - 0.1f, robot->position[1] - 10.0f) <= std::min(bullets[j]->t->position[1] + 0.1f, robot->position[1] + 10.0f) && 
			std::max(bullets[j]->t->position[2] - 0.1f, robot->position[2] - 8.0f) <= std::min(bullets[j]->t->position[2] + 0.1f, robot->position[2] + 8.0f)) {
			
			bullets[j]->t->position = glm::vec3(0.0f, 0.0f, -100.0f);
			bullets[j]->t->scale = glm::vec3(0.0f, 0.0f, 0.0f);
			bullets.erase(bullets.begin() + j);
			
			if (hit_invinc > hit_time) {
				robot_health -= 1;
				Sound::play(*big_robot_hit, 1.0f, 0.0f);
				hit_time = hit_invinc + 2.0f;
				if (robot_health <= 0) {
					win = true;
				}
			}
			return;
		}
	}
}

void PlayMode::enemy_die() {
	std::vector<size_t> pop_enemies;
	for (size_t i = 0; i < enemies.size(); i++) {
		for (size_t j = 0; j < bullets.size(); j++) {
			if (std::max(bullets[j]->t->position[0] - 0.1f, enemies[i]->t->position[0] - 0.8f) <= std::min(bullets[j]->t->position[0] + 0.1f, enemies[i]->t->position[0] + 0.8f) &&
			std::max(bullets[j]->t->position[1] - 0.1f, enemies[i]->t->position[1] - 0.8f) <= std::min(bullets[j]->t->position[1] + 0.1f, enemies[i]->t->position[1] + 0.8f) && 
			std::max(bullets[j]->t->position[2] - 0.1f, enemies[i]->t->position[2] - 0.8f) <= std::min(bullets[j]->t->position[2] + 0.1f, enemies[i]->t->position[2] + 0.8f)) {
				enemies[i]->t->position = glm::vec3(0.0f, 0.0f, -100.0f);
				bullets[j]->t->position = glm::vec3(0.0f, 0.0f, -100.0f);
				enemies[i]->t->scale = glm::vec3(0.0f, 0.0f, 0.0f);
				bullets[j]->t->scale = glm::vec3(0.0f, 0.0f, 0.0f);
				bullets.erase(bullets.begin() + j);
				enemies.erase(enemies.begin() + i);
				Sound::play(*enemy_hit, 1.0f, 0.0f);
				return;
			}
		}
	}
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
	//update camera aspect ratio for drawable:
	
	player.camera->aspect = float(drawable_size.x) / float(drawable_size.y);

	//set up light type and position for lit_color_texture_program:
	// TODO: consider using the Light(s) in the scene to do this
	glUseProgram(lit_color_texture_program->program);
	glUniform1i(lit_color_texture_program->LIGHT_TYPE_int, 1);
	glUniform3fv(lit_color_texture_program->LIGHT_DIRECTION_vec3, 1, glm::value_ptr(glm::vec3(0.0f, 0.0f,-1.0f)));
	glUniform3fv(lit_color_texture_program->LIGHT_ENERGY_vec3, 1, glm::value_ptr(glm::vec3(1.0f, 1.0f, 0.95f)));

	glUseProgram(0);

	glClearColor(0.5f, 0.2f, 0.5f, 1.0f);
	glClearDepth(1.0f); //1.0 is actually the default value to clear the depth buffer to, but FYI you can change it.
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS); //this is the default depth comparison function, but FYI you can change it.

	scene.draw(*player.camera);

	{ //use DrawLines to overlay some text:
		glDisable(GL_DEPTH_TEST);
		float aspect = float(drawable_size.x) / float(drawable_size.y);
		DrawLines lines(glm::mat4(
			1.0f / aspect, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		));
		constexpr float H = 0.09f;
		float ofs = 2.0f / drawable_size.y;
		if (win) {
			
			lines.draw_text("You beat the robots!",
				glm::vec3(-aspect / 5.5f, 0.0f, 0.0),
				glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
				glm::u8vec4(0x00, 0x00, 0x00, 0x00));
			lines.draw_text("You beat the robots!",
				glm::vec3(-aspect / 5.5f + ofs, ofs, 0.0),
				glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
				glm::u8vec4(0xff, 0xff, 0xff, 0x00));
		}
		if (lose) {
			lines.draw_text("You lost all your cargo. Game Over!",
				glm::vec3(-aspect / 4.0f, 0.0f, 0.0),
				glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
				glm::u8vec4(0x00, 0x00, 0x00, 0x00));
			lines.draw_text("You lost all your cargo. Game Over!",
				glm::vec3(-aspect / 4.0f + ofs, ofs, 0.0),
				glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
				glm::u8vec4(0xff, 0xff, 0xff, 0x00));
		}
	}

	GL_ERRORS();
}
