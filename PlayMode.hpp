#include "Mode.hpp"

#include "Scene.hpp"
#include "WalkMesh.hpp"

#include <glm/glm.hpp>

#include <vector>
#include <deque>
#include <queue>

struct bullet_info {
	float age = 0.0f;
	Scene::Transform *t;
	glm::vec3 dir;
};

struct enemy_info {
	Scene::Transform *t;
	glm::vec3 dir;
};

struct PlayMode : Mode {
	PlayMode();
	virtual ~PlayMode();

	//functions called by main loop:
	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;
	virtual void shoot();
	virtual void move_bullets(float elapsed);
	virtual void generate_bot(float elapsed);
	virtual void move_enemies();
	virtual void enemy_die();
	virtual void cargo_taken();
	virtual void robot_damage(float elapsed);

	//----- game state -----

	GLuint vertex_buffer = 0;
	//input tracking:
	struct Button {
		uint8_t downs = 0;
		uint8_t pressed = 0;
	} left, right, down, up, shot;

	Scene::Drawable *bullet = nullptr;
	Scene::Drawable *enemy = nullptr;
	Scene::Transform *bTrans = nullptr;
	Scene::Drawable::Pipeline bPipe;
	Scene::Transform *robot = nullptr;
	Scene::Transform *eTrans = nullptr;
	Scene::Drawable::Pipeline ePipe;
	std::deque<bullet_info *> bullets;
	std::vector<enemy_info *> enemies;
	std::vector<Scene::Transform *> cargo;

	float bot_time = 0.0f;
	float bot_gen = 0.0f;

	float hit_invinc = 0.0f;
	float hit_time = 0.0f;

	uint8_t robot_health = 10;

	bool win = false;
	bool lose = false;

	//local copy of the game scene (so code can change it during gameplay):
	Scene scene;

	//player info:
	struct Player {
		WalkPoint at;
		//transform is at player's feet and will be yawed by mouse left/right motion:
		Scene::Transform *transform = nullptr;
		//camera is at player's head and will be pitched by mouse up/down motion:
		Scene::Camera *camera = nullptr;
	} player;
};
