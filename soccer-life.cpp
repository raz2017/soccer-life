#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

static const int WIDTH = 960;
static const int HEIGHT = 540;
static const int FPS = 60;
static const float SIDEBAR_WIDTH = 140.0f;
static const float PLAYER_RADIUS = 10.0f;
static const float BALL_RADIUS = 6.0f;
static const float PLAYER_ACCEL = 0.45f;
static const float PLAYER_MAX_SPEED = 4.6f;
static const float PLAYER_DRAG = 0.84f;
static const float BALL_DRAG = 0.992f;
static const float KICK_POWER = 7.8f;
static const float BUMP_POWER = 1.35f;
static const float DRIBBLE_OFFSET = 14.0f;
static const int GOAL_DEPTH = 24;
static const int GOAL_HEIGHT = 170;
static const float FIELD_MARGIN = 12.0f;
static const float PITCH_LEFT = SIDEBAR_WIDTH + FIELD_MARGIN;
static const float PITCH_RIGHT = WIDTH - FIELD_MARGIN;
static const float PITCH_TOP = FIELD_MARGIN;
static const float PITCH_BOTTOM = HEIGHT - FIELD_MARGIN;
static const float PITCH_CENTER_X = (PITCH_LEFT + PITCH_RIGHT) * 0.5f;
static const float PITCH_CENTER_Y = HEIGHT * 0.5f;

struct Color {
	Uint8 r;
	Uint8 g;
	Uint8 b;
	Uint8 a;
};

static const Color COLOR_GRASS = { 38, 132, 64, 255 };
static const Color COLOR_GRASS_DARK = { 30, 108, 52, 255 };
static const Color COLOR_LINE = { 240, 240, 230, 255 };
static const Color COLOR_RED = { 214, 62, 62, 255 };
static const Color COLOR_BLUE = { 66, 123, 224, 255 };
static const Color COLOR_BALL = { 245, 236, 196, 255 };
static const Color COLOR_SHADOW = { 0, 0, 0, 80 };
static const Color COLOR_SKIN = { 242, 199, 160, 255 };
static const Color COLOR_DARK = { 24, 24, 24, 255 };

struct Vector2 {
	float x;
	float y;

	Vector2(float xValue = 0.0f, float yValue = 0.0f) : x(xValue), y(yValue) {}

	Vector2 operator+(const Vector2& other) const { return Vector2(x + other.x, y + other.y); }
	Vector2 operator-(const Vector2& other) const { return Vector2(x - other.x, y - other.y); }
	Vector2 operator*(float scalar) const { return Vector2(x * scalar, y * scalar); }
	Vector2& operator+=(const Vector2& other) {
		x += other.x;
		y += other.y;
		return *this;
	}

	float magnitude() const { return std::sqrt(x * x + y * y); }

	Vector2 normalized() const {
		float mag = magnitude();
		if (mag <= 0.0001f) {
			return Vector2(0.0f, 0.0f);
		}
		return Vector2(x / mag, y / mag);
	}
};

struct Player {
	Vector2 pos;
	Vector2 vel;
	Vector2 facing;
	Color color;
	int score;

	Player(float x, float y, Color playerColor)
		: pos(x, y), vel(0.0f, 0.0f), facing(1.0f, 0.0f), color(playerColor), score(0) {}
};

struct Ball {
	Vector2 pos;
	Vector2 vel;

	Ball(float x, float y) : pos(x, y), vel(0.0f, 0.0f) {}
};

struct GameState {
	Player leftPlayer;
	Player rightPlayer;
	Ball ball;
	int possession;
	int possessionCooldown;
	int goalFlashTimer;
	int kickoffDirection;
	int lastTouchTeam;
	bool kickLatchP1;
	bool kickLatchP2;

	GameState()
		: leftPlayer(PITCH_CENTER_X, PITCH_CENTER_Y + 60.0f, COLOR_RED),
		  rightPlayer(PITCH_CENTER_X, PITCH_CENTER_Y - 60.0f, COLOR_BLUE),
		  ball(PITCH_CENTER_X, PITCH_CENTER_Y),
		  possession(-1),
		  possessionCooldown(0),
		  goalFlashTimer(0),
		  kickoffDirection(1),
		  lastTouchTeam(-1),
		  kickLatchP1(false),
		  kickLatchP2(false) {}
};

static float clampf(float value, float minValue, float maxValue) {
	return std::max(minValue, std::min(value, maxValue));
}

static void setColor(SDL_Renderer* renderer, Color color) {
	SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

static void fillRect(SDL_Renderer* renderer, float x, float y, float w, float h, Color color) {
	setColor(renderer, color);
	SDL_FRect rect = { x, y, w, h };
	SDL_RenderFillRect(renderer, &rect);
}

static void drawRect(SDL_Renderer* renderer, float x, float y, float w, float h, Color color) {
	setColor(renderer, color);
	SDL_FRect rect = { x, y, w, h };
	SDL_RenderRect(renderer, &rect);
}

static void drawLine(SDL_Renderer* renderer, float x1, float y1, float x2, float y2, Color color) {
	setColor(renderer, color);
	SDL_RenderLine(renderer, x1, y1, x2, y2);
}

static void drawCircle(SDL_Renderer* renderer, int cx, int cy, int radius, Color color) {
	setColor(renderer, color);
	for (int y = -radius; y <= radius; ++y) {
		for (int x = -radius; x <= radius; ++x) {
			if (x * x + y * y <= radius * radius) {
				SDL_RenderPoint(renderer, cx + x, cy + y);
			}
		}
	}
}

static void fillCenteredRect(SDL_Renderer* renderer, float cx, float cy, float w, float h, Color color) {
	fillRect(renderer, cx - w * 0.5f, cy - h * 0.5f, w, h, color);
}

static void drawDigit(SDL_Renderer* renderer, int digit, float x, float y, Color color) {
	static const unsigned char segmentMasks[10] = {
		0b1110111,
		0b0100100,
		0b1011101,
		0b1101101,
		0b0101110,
		0b1101011,
		0b1111011,
		0b0100101,
		0b1111111,
		0b1101111
	};

	if (digit < 0 || digit > 9) {
		return;
	}

	const float width = 24.0f;
	const float height = 36.0f;
	const float thickness = 4.0f;
	const float segmentLength = width - thickness * 2.0f;
	const float upperHeight = 12.0f;
	const float lowerTop = 20.0f;

	unsigned char mask = segmentMasks[digit];

	if (mask & (1 << 0)) fillRect(renderer, x + thickness, y, segmentLength, thickness, color);
	if (mask & (1 << 1)) fillRect(renderer, x, y + thickness, thickness, upperHeight, color);
	if (mask & (1 << 2)) fillRect(renderer, x + width - thickness, y + thickness, thickness, upperHeight, color);
	if (mask & (1 << 3)) fillRect(renderer, x + thickness, y + 16.0f, segmentLength, thickness, color);
	if (mask & (1 << 4)) fillRect(renderer, x, y + lowerTop, thickness, upperHeight, color);
	if (mask & (1 << 5)) fillRect(renderer, x + width - thickness, y + lowerTop, thickness, upperHeight, color);
	if (mask & (1 << 6)) fillRect(renderer, x + thickness, y + height - thickness, segmentLength, thickness, color);
}

static void drawScore(SDL_Renderer* renderer, int redScore, int blueScore) {
	Color panel = { 8, 16, 14, 170 };
	float panelX = 28.0f;
	float redPanelY = 88.0f;
	float bluePanelY = 162.0f;
	float panelW = 84.0f;
	float panelH = 54.0f;

	fillRect(renderer, 0.0f, 0.0f, SIDEBAR_WIDTH, static_cast<float>(HEIGHT), { 16, 40, 28, 255 });
	drawLine(renderer, SIDEBAR_WIDTH, 0.0f, SIDEBAR_WIDTH, static_cast<float>(HEIGHT), COLOR_LINE);

	fillRect(renderer, panelX, redPanelY, panelW, panelH, panel);
	fillRect(renderer, panelX, bluePanelY, panelW, panelH, panel);
	drawDigit(renderer, std::abs(redScore) % 10, panelX + 30.0f, redPanelY + 9.0f, COLOR_RED);
	drawDigit(renderer, std::abs(blueScore) % 10, panelX + 30.0f, bluePanelY + 9.0f, COLOR_BLUE);
	fillRect(renderer, panelX + 18.0f, 148.0f, 48.0f, 4.0f, COLOR_LINE);
}

static void resetPositions(GameState& game, int kickoffDirection) {
	game.leftPlayer.pos = Vector2(PITCH_CENTER_X, PITCH_CENTER_Y + 60.0f);
	game.leftPlayer.vel = Vector2();
	game.leftPlayer.facing = Vector2(0.0f, -1.0f);

	game.rightPlayer.pos = Vector2(PITCH_CENTER_X, PITCH_CENTER_Y - 60.0f);
	game.rightPlayer.vel = Vector2();
	game.rightPlayer.facing = Vector2(0.0f, 1.0f);

	game.ball.pos = Vector2(PITCH_CENTER_X, PITCH_CENTER_Y);
	game.ball.vel = Vector2(0.0f, 1.8f * kickoffDirection);

	game.possession = -1;
	game.possessionCooldown = 0;
	game.goalFlashTimer = 0;
	game.lastTouchTeam = -1;
	game.kickLatchP1 = false;
	game.kickLatchP2 = false;
}

static Vector2 clampPointToField(Vector2 point) {
	point.x = clampf(point.x, PITCH_LEFT + PLAYER_RADIUS, PITCH_RIGHT - PLAYER_RADIUS);
	point.y = clampf(point.y, PITCH_TOP + PLAYER_RADIUS, PITCH_BOTTOM - PLAYER_RADIUS);
	return point;
}

static Vector2 clampPointToWindow(Vector2 point) {
	point.x = clampf(point.x, PITCH_LEFT - PLAYER_RADIUS, PITCH_RIGHT + PLAYER_RADIUS);
	point.y = clampf(point.y, PITCH_TOP - PLAYER_RADIUS, PITCH_BOTTOM + PLAYER_RADIUS);
	return point;
}

static void updateWindowTitle(SDL_Window* window, const GameState& game) {
	char title[64];
	std::snprintf(title, sizeof(title), "Soccer Life  |  Red %d - %d Blue", game.leftPlayer.score, game.rightPlayer.score);
	SDL_SetWindowTitle(window, title);
}

static void toggleFullscreen(SDL_Window* window) {
	SDL_WindowFlags flags = SDL_GetWindowFlags(window);
	bool isFullscreen = (flags & SDL_WINDOW_FULLSCREEN) != 0;
	if (!SDL_SetWindowFullscreen(window, !isFullscreen)) {
		SDL_Log("SDL_SetWindowFullscreen failed: %s", SDL_GetError());
	}
}

static void applyInput(Player& player, const bool* keys, SDL_Scancode up, SDL_Scancode down, SDL_Scancode left, SDL_Scancode right) {
	Vector2 move;
	if (keys[up]) {
		move.y -= 1.0f;
	}
	if (keys[down]) {
		move.y += 1.0f;
	}
	if (keys[left]) {
		move.x -= 1.0f;
	}
	if (keys[right]) {
		move.x += 1.0f;
	}

	if (move.magnitude() > 0.0f) {
		move = move.normalized();
		player.facing = move;
		player.vel += move * PLAYER_ACCEL;
	}

	float speed = player.vel.magnitude();
	if (speed > PLAYER_MAX_SPEED) {
		player.vel = player.vel.normalized() * PLAYER_MAX_SPEED;
	}
}

static void updatePlayerPhysics(Player& player) {
	player.pos += player.vel;
	player.vel = player.vel * PLAYER_DRAG;

	player.pos.x = clampf(player.pos.x, PLAYER_RADIUS + 18.0f, WIDTH - PLAYER_RADIUS - 18.0f);
	player.pos.y = clampf(player.pos.y, PLAYER_RADIUS + 18.0f, HEIGHT - PLAYER_RADIUS - 18.0f);
}

static void separatePlayers(Player& a, Player& b) {
	Vector2 delta = b.pos - a.pos;
	float distance = delta.magnitude();
	float minDistance = PLAYER_RADIUS * 2.0f;

	if (distance <= 0.001f) {
		delta = Vector2(1.0f, 0.0f);
		distance = 1.0f;
	}

	if (distance < minDistance) {
		Vector2 normal = delta * (1.0f / distance);
		float overlap = minDistance - distance;
		a.pos += normal * (-overlap * 0.5f);
		b.pos += normal * (overlap * 0.5f);
		a.vel += normal * (-0.35f);
		b.vel += normal * (0.35f);
	}
}

static void freeBallCollision(Player& player, Ball& ball, bool kickPressed, bool& kickLatch, int team, int& lastTouchTeam) {
	Vector2 delta = ball.pos - player.pos;
	float distance = delta.magnitude();
	float minDistance = PLAYER_RADIUS + BALL_RADIUS;

	if (distance <= 0.001f) {
		delta = player.facing;
		distance = 1.0f;
	}

	if (distance < minDistance) {
		Vector2 normal = delta * (1.0f / distance);
		float overlap = minDistance - distance;

		ball.pos += normal * overlap;
		ball.vel = ball.vel * 0.7f + player.vel * 0.55f + normal * BUMP_POWER;
		lastTouchTeam = team;

		if (kickPressed && !kickLatch) {
			Vector2 kickDir = player.facing.magnitude() > 0.0f ? player.facing.normalized() : normal;
			ball.vel = kickDir * KICK_POWER + player.vel * 0.35f;
			lastTouchTeam = team;
			kickLatch = true;
		}
	}

	if (!kickPressed) {
		kickLatch = false;
	}
}

static void updateDribbleBall(const Player& player, Ball& ball) {
	Vector2 facing = player.facing.magnitude() > 0.0f ? player.facing.normalized() : Vector2(0.0f, -1.0f);
	ball.pos = player.pos + facing * DRIBBLE_OFFSET;
	ball.vel = player.vel;
}

static bool inGoalSpan(float x) {
	float goalLeft = PITCH_CENTER_X - GOAL_HEIGHT * 0.5f;
	float goalRight = goalLeft + GOAL_HEIGHT;
	return x >= goalLeft && x <= goalRight;
}

static void setRestart(GameState& game, int team, Vector2 ballPos, Vector2 facing, Vector2 opponentPos) {
	Vector2 direction = facing.magnitude() > 0.0f ? facing.normalized() : Vector2(0.0f, -1.0f);
	game.possession = team;
	game.possessionCooldown = 0;
	game.ball.pos = ballPos;
	game.ball.vel = Vector2(0.0f, 0.0f);
	game.kickLatchP1 = false;
	game.kickLatchP2 = false;
	game.lastTouchTeam = team;

	if (team == 0) {
		game.leftPlayer.facing = direction;
		game.leftPlayer.vel = Vector2();
		game.leftPlayer.pos = clampPointToWindow(ballPos - direction * DRIBBLE_OFFSET);
		game.rightPlayer.vel = Vector2();
		game.rightPlayer.pos = clampPointToField(opponentPos);
	}
	else {
		game.rightPlayer.facing = direction;
		game.rightPlayer.vel = Vector2();
		game.rightPlayer.pos = clampPointToWindow(ballPos - direction * DRIBBLE_OFFSET);
		game.leftPlayer.vel = Vector2();
		game.leftPlayer.pos = clampPointToField(opponentPos);
	}

	updateDribbleBall(team == 0 ? game.leftPlayer : game.rightPlayer, game.ball);
}

static bool handleOutOfBounds(GameState& game) {
	float topY = PITCH_TOP;
	float bottomY = PITCH_BOTTOM;
	float leftX = PITCH_LEFT;
	float rightX = PITCH_RIGHT;
	float clampedX = clampf(game.ball.pos.x, leftX, rightX);
	float clampedY = clampf(game.ball.pos.y, topY, bottomY);
	float sixYardLeft = PITCH_CENTER_X - 58.0f;
	float sixYardRight = PITCH_CENTER_X + 58.0f;
	int receivingTeam = game.lastTouchTeam == 0 ? 1 : 0;

	if (game.ball.pos.x + BALL_RADIUS < leftX || game.ball.pos.x - BALL_RADIUS > rightX) {
		bool outLeft = game.ball.pos.x + BALL_RADIUS < leftX;
		float restartX = outLeft ? leftX : rightX;
		Vector2 facing = outLeft ? Vector2(1.0f, 0.0f) : Vector2(-1.0f, 0.0f);
		Vector2 ballPos(restartX, clampedY);
		Vector2 opponentPos(ballPos.x - facing.x * 96.0f, ballPos.y + 34.0f);
		setRestart(game, receivingTeam, ballPos, facing, opponentPos);
		return true;
	}

	if (game.ball.pos.y + BALL_RADIUS < topY && !inGoalSpan(game.ball.pos.x)) {
		if (game.lastTouchTeam == 0) {
			Vector2 ballPos(game.ball.pos.x < WIDTH * 0.5f ? leftX : rightX, topY);
			Vector2 facing = game.ball.pos.x < WIDTH * 0.5f ? Vector2(0.65f, 1.0f) : Vector2(-0.65f, 1.0f);
			Vector2 opponentPos(PITCH_CENTER_X, 154.0f);
			setRestart(game, 1, ballPos, facing, opponentPos);
		}
		else {
			Vector2 ballPos((sixYardLeft + sixYardRight) * 0.5f, 41.0f);
			Vector2 opponentPos(PITCH_CENTER_X, 166.0f);
			setRestart(game, 0, ballPos, Vector2(0.0f, 1.0f), opponentPos);
		}
		return true;
	}

	if (game.ball.pos.y - BALL_RADIUS > bottomY && !inGoalSpan(game.ball.pos.x)) {
		if (game.lastTouchTeam == 1) {
			Vector2 ballPos(game.ball.pos.x < WIDTH * 0.5f ? leftX : rightX, bottomY);
			Vector2 facing = game.ball.pos.x < WIDTH * 0.5f ? Vector2(0.65f, -1.0f) : Vector2(-0.65f, -1.0f);
			Vector2 opponentPos(PITCH_CENTER_X, HEIGHT - 154.0f);
			setRestart(game, 0, ballPos, facing, opponentPos);
		}
		else {
			Vector2 ballPos((sixYardLeft + sixYardRight) * 0.5f, HEIGHT - 41.0f);
			Vector2 opponentPos(PITCH_CENTER_X, HEIGHT - 166.0f);
			setRestart(game, 1, ballPos, Vector2(0.0f, -1.0f), opponentPos);
		}
		return true;
	}

	return false;
}

static void updateBallPhysics(Ball& ball) {
	ball.pos += ball.vel;
	ball.vel = ball.vel * BALL_DRAG;

	if (inGoalSpan(ball.pos.x)) {
		if (ball.pos.y - BALL_RADIUS < -GOAL_DEPTH) {
			ball.pos.y = -GOAL_DEPTH + BALL_RADIUS;
			ball.vel.y = -ball.vel.y * 0.75f;
		}
		if (ball.pos.y + BALL_RADIUS > HEIGHT + GOAL_DEPTH) {
			ball.pos.y = HEIGHT + GOAL_DEPTH - BALL_RADIUS;
			ball.vel.y = -ball.vel.y * 0.75f;
		}
	}
}

static void update(GameState& game, SDL_Window* window, const bool* keys) {
	if (game.goalFlashTimer == 0 &&
		game.ball.pos.y + BALL_RADIUS < PITCH_TOP && inGoalSpan(game.ball.pos.x)) {
		game.rightPlayer.score++;
		game.goalFlashTimer = 90;
		game.kickoffDirection = 1;
		game.ball.vel = Vector2(0.0f, 0.0f);
		game.possession = -1;
		updateWindowTitle(window, game);
	}

	if (game.goalFlashTimer == 0 &&
		game.ball.pos.y - BALL_RADIUS > PITCH_BOTTOM && inGoalSpan(game.ball.pos.x)) {
		game.leftPlayer.score++;
		game.goalFlashTimer = 90;
		game.kickoffDirection = -1;
		game.ball.vel = Vector2(0.0f, 0.0f);
		game.possession = -1;
		updateWindowTitle(window, game);
	}

	if (game.goalFlashTimer > 0) {
		game.goalFlashTimer--;
		if (game.goalFlashTimer == 0) {
			resetPositions(game, game.kickoffDirection);
		}
		return;
	}

	if (game.possession >= 0) {
		game.lastTouchTeam = game.possession;
	}

	applyInput(game.leftPlayer, keys, SDL_SCANCODE_W, SDL_SCANCODE_S, SDL_SCANCODE_A, SDL_SCANCODE_D);
	applyInput(game.rightPlayer, keys, SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT);

	updatePlayerPhysics(game.leftPlayer);
	updatePlayerPhysics(game.rightPlayer);
	separatePlayers(game.leftPlayer, game.rightPlayer);

	bool kickP1 = keys[SDL_SCANCODE_SPACE];
	bool kickP2 = keys[SDL_SCANCODE_SPACE];

	if (game.possessionCooldown > 0) {
		game.possessionCooldown--;
	}

	if (game.possession == 0) {
		updateDribbleBall(game.leftPlayer, game.ball);
		Vector2 kickDir = game.leftPlayer.facing.magnitude() > 0.0f ? game.leftPlayer.facing.normalized() : Vector2(0.0f, -1.0f);
		if (kickP1 && !game.kickLatchP1) {
			game.ball.vel = kickDir * KICK_POWER + game.leftPlayer.vel * 0.35f;
			game.possession = -1;
			game.possessionCooldown = 8;
			game.lastTouchTeam = 0;
			game.kickLatchP1 = true;
		}
		if (!kickP1) {
			game.kickLatchP1 = false;
		}
		game.kickLatchP2 = false;
	}
	else if (game.possession == 1) {
		updateDribbleBall(game.rightPlayer, game.ball);
		Vector2 kickDir = game.rightPlayer.facing.magnitude() > 0.0f ? game.rightPlayer.facing.normalized() : Vector2(0.0f, 1.0f);
		if (kickP2 && !game.kickLatchP2) {
			game.ball.vel = kickDir * KICK_POWER + game.rightPlayer.vel * 0.35f;
			game.possession = -1;
			game.possessionCooldown = 8;
			game.lastTouchTeam = 1;
			game.kickLatchP2 = true;
		}
		if (!kickP2) {
			game.kickLatchP2 = false;
		}
		game.kickLatchP1 = false;
	}
	else {
		freeBallCollision(game.leftPlayer, game.ball, kickP1, game.kickLatchP1, 0, game.lastTouchTeam);
		freeBallCollision(game.rightPlayer, game.ball, kickP2, game.kickLatchP2, 1, game.lastTouchTeam);
		updateBallPhysics(game.ball);

		if (handleOutOfBounds(game)) {
			return;
		}

		if (game.possessionCooldown == 0) {
			float leftDistance = (game.ball.pos - game.leftPlayer.pos).magnitude();
			float rightDistance = (game.ball.pos - game.rightPlayer.pos).magnitude();
			float controlDistance = PLAYER_RADIUS + BALL_RADIUS + 2.0f;

			if (leftDistance <= controlDistance && leftDistance <= rightDistance) {
				game.possession = 0;
				game.lastTouchTeam = 0;
				updateDribbleBall(game.leftPlayer, game.ball);
			}
			else if (rightDistance <= controlDistance) {
				game.possession = 1;
				game.lastTouchTeam = 1;
				updateDribbleBall(game.rightPlayer, game.ball);
			}
		}
	}

	if (game.possession >= 0 && handleOutOfBounds(game)) {
		return;
	}

}

static void drawPitch(SDL_Renderer* renderer) {
	fillRect(renderer, 0.0f, 0.0f, static_cast<float>(WIDTH), static_cast<float>(HEIGHT), COLOR_GRASS);

	for (int stripe = 0; stripe < 10; ++stripe) {
		if (stripe % 2 == 0) {
			fillRect(renderer, 0.0f, stripe * (HEIGHT / 10.0f), static_cast<float>(WIDTH), HEIGHT / 10.0f, COLOR_GRASS_DARK);
		}
	}

	float goalLeft = PITCH_CENTER_X - GOAL_HEIGHT * 0.5f;
	float penaltyLeft = PITCH_CENTER_X - 105.0f;
	float penaltyWidth = 210.0f;
	float sixYardLeft = PITCH_CENTER_X - 58.0f;
	float sixYardWidth = 116.0f;

	drawRect(renderer, PITCH_LEFT, PITCH_TOP, PITCH_RIGHT - PITCH_LEFT, PITCH_BOTTOM - PITCH_TOP, COLOR_LINE);
	drawLine(renderer, PITCH_LEFT, PITCH_CENTER_Y, PITCH_RIGHT, PITCH_CENTER_Y, COLOR_LINE);
	drawCircle(renderer, static_cast<int>(PITCH_CENTER_X), static_cast<int>(PITCH_CENTER_Y), 54, COLOR_LINE);
	drawCircle(renderer, static_cast<int>(PITCH_CENTER_X), static_cast<int>(PITCH_CENTER_Y), 3, COLOR_LINE);

	drawRect(renderer, penaltyLeft, PITCH_TOP, penaltyWidth, 130.0f, COLOR_LINE);
	drawRect(renderer, penaltyLeft, PITCH_BOTTOM - 130.0f, penaltyWidth, 130.0f, COLOR_LINE);
	drawRect(renderer, sixYardLeft, PITCH_TOP, sixYardWidth, 58.0f, COLOR_LINE);
	drawRect(renderer, sixYardLeft, PITCH_BOTTOM - 58.0f, sixYardWidth, 58.0f, COLOR_LINE);

	drawRect(renderer, goalLeft, PITCH_TOP - GOAL_DEPTH, GOAL_HEIGHT, GOAL_DEPTH, COLOR_LINE);
	drawRect(renderer, goalLeft, PITCH_BOTTOM, GOAL_HEIGHT, GOAL_DEPTH, COLOR_LINE);
	drawLine(renderer, goalLeft, PITCH_TOP, goalLeft + GOAL_HEIGHT, PITCH_TOP, COLOR_LINE);
	drawLine(renderer, goalLeft, PITCH_BOTTOM, goalLeft + GOAL_HEIGHT, PITCH_BOTTOM, COLOR_LINE);
}

static void drawPlayer(SDL_Renderer* renderer, const Player& player) {
	Vector2 facing = player.facing.magnitude() > 0.0f ? player.facing.normalized() : Vector2(1.0f, 0.0f);
	Vector2 right(-facing.y, facing.x);

	fillCenteredRect(renderer, player.pos.x + 1.0f, player.pos.y + 2.0f, 12.0f, 12.0f, COLOR_SHADOW);

	Vector2 torsoCenter = player.pos;
	Vector2 shortsCenter = player.pos + facing * 3.0f;
	Vector2 headCenter = player.pos - facing * 4.5f;
	Vector2 leftFoot = player.pos + facing * 7.0f - right * 2.5f;
	Vector2 rightFoot = player.pos + facing * 7.0f + right * 2.5f;

	fillCenteredRect(renderer, headCenter.x, headCenter.y, 6.0f, 6.0f, COLOR_SKIN);
	fillCenteredRect(renderer, torsoCenter.x, torsoCenter.y, 10.0f, 8.0f, player.color);
	fillCenteredRect(renderer, shortsCenter.x, shortsCenter.y, 8.0f, 4.0f, COLOR_DARK);
	fillCenteredRect(renderer, leftFoot.x, leftFoot.y, 3.0f, 4.0f, COLOR_DARK);
	fillCenteredRect(renderer, rightFoot.x, rightFoot.y, 3.0f, 4.0f, COLOR_DARK);

	Vector2 indicator = player.pos + facing * 9.0f;
	fillCenteredRect(renderer, indicator.x, indicator.y, 3.0f, 3.0f, COLOR_LINE);
}

static void drawBall(SDL_Renderer* renderer, const Ball& ball) {
	fillCenteredRect(renderer, ball.pos.x + 1.0f, ball.pos.y + 1.0f, 9.0f, 9.0f, COLOR_SHADOW);
	fillCenteredRect(renderer, ball.pos.x, ball.pos.y, 8.0f, 8.0f, COLOR_BALL);
	fillCenteredRect(renderer, ball.pos.x, ball.pos.y, 3.0f, 3.0f, COLOR_DARK);
}

static void render(SDL_Renderer* renderer, const GameState& game) {
	setColor(renderer, { 12, 40, 20, 255 });
	SDL_RenderClear(renderer);

	drawPitch(renderer);
	drawPlayer(renderer, game.leftPlayer);
	drawPlayer(renderer, game.rightPlayer);
	drawBall(renderer, game.ball);
	drawScore(renderer, game.leftPlayer.score, game.rightPlayer.score);

	if (game.goalFlashTimer > 0) {
		fillRect(renderer, 0.0f, 0.0f, static_cast<float>(WIDTH), static_cast<float>(HEIGHT), { 255, 255, 255, 60 });
	}

	SDL_RenderPresent(renderer);
}

int main(int argc, char* argv[]) {
	if (!SDL_Init(SDL_INIT_VIDEO)) {
		SDL_Log("SDL_Init failed: %s", SDL_GetError());
		return 1;
	}

	SDL_Window* window = SDL_CreateWindow("Soccer Life", WIDTH, HEIGHT, 0);
	if (!window) {
		SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
		SDL_Quit();
		return 1;
	}

	SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
	if (!renderer) {
		SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}

	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

	if (!SDL_SetRenderLogicalPresentation(renderer, WIDTH, HEIGHT, SDL_LOGICAL_PRESENTATION_LETTERBOX)) {
		SDL_Log("SDL_SetRenderLogicalPresentation failed: %s", SDL_GetError());
		SDL_DestroyRenderer(renderer);
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}

	GameState game;
	updateWindowTitle(window, game);

	bool running = true;
	Uint64 lastTick = SDL_GetTicks();
	const Uint64 frameMs = 1000 / FPS;

	while (running) {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_EVENT_QUIT) {
				running = false;
			}
			if (event.type == SDL_EVENT_KEY_DOWN) {
				if (event.key.key == SDLK_ESCAPE) {
					running = false;
				}
				if (event.key.key == SDLK_F8) {
					toggleFullscreen(window);
				}
				if (event.key.key == SDLK_R) {
					int redScore = game.leftPlayer.score;
					int blueScore = game.rightPlayer.score;
					resetPositions(game, 1);
					game.leftPlayer.score = redScore;
					game.rightPlayer.score = blueScore;
				}
				if (event.key.key == SDLK_F) {
					game.leftPlayer.score = 0;
					game.rightPlayer.score = 0;
					resetPositions(game, 1);
					updateWindowTitle(window, game);
				}
			}
		}

		int numKeys = 0;
		const bool* keys = SDL_GetKeyboardState(&numKeys);
		(void)numKeys;

		Uint64 now = SDL_GetTicks();
		if (now - lastTick >= frameMs) {
			update(game, window, keys);
			lastTick = now;
		}

		render(renderer, game);
		SDL_Delay(1);
	}

	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}
