#include <SDL3/SDL.h>
#include <vector>
#include <cstdlib>
#include <ctime>

static const int COLS = 120;
static const int ROWS = 80;
static const int CELL = 8;
static const int WIDTH = COLS * CELL;
static const int HEIGHT = ROWS * CELL;
static const int FPS = 120;

using Grid = std::vector<std::vector<uint8_t>>;

struct AppState {
	Grid grid;
	Grid prev;
	int stuckCount = 0;
	bool paused = false;
	bool fullscreen = false;
	int savedX = 0;
	int savedY = 0;
};

Grid makeGrid() {
	return Grid(ROWS, std::vector<uint8_t>(COLS, 0));
}

void randomize(Grid& g) {
	for (int r = 0; r < ROWS; ++r)
		for (int c = 0; c < COLS; ++c)
			g[r][c] = (rand() % 4 == 0);
}

int neighbors(const Grid& g, int r, int c) {
	int count = 0;
	for (int dr = -1; dr <= 1; ++dr)
		for (int dc = -1; dc <= 1; ++dc) {
			if (dr == 0 && dc == 0) continue;
			int nr = (r + dr + ROWS) % ROWS;
			int nc = (c + dc + COLS) % COLS;
			if (g[nr][nc]) ++count;
		}
	return count;
}

Grid step(const Grid& g) {
	Grid next = makeGrid();
	for (int r = 0; r < ROWS; ++r)
		for (int c = 0; c < COLS; ++c) {
			int n = neighbors(g, r, c);
			if (g[r][c])
				next[r][c] = (n == 2 || n == 3);
			else
				next[r][c] = (n == 3);
		}
	return next;
}

void resetGrid(AppState& state, bool randomized) {
	state.grid = makeGrid();
	if (randomized) {
		randomize(state.grid);
	}
	state.prev = state.grid;
	state.stuckCount = 0;
}

void toggleFullscreen(SDL_Window* window, AppState& state) {
	state.fullscreen = !state.fullscreen;
	if (state.fullscreen) {
		SDL_GetWindowPosition(window, &state.savedX, &state.savedY);
	}

	if (!SDL_SetWindowFullscreen(window, state.fullscreen)) {
		SDL_Log("SDL_SetWindowFullscreen failed: %s", SDL_GetError());
		state.fullscreen = !state.fullscreen;
		return;
	}

	if (!state.fullscreen) {
		SDL_SetWindowPosition(window, state.savedX, state.savedY);
		SDL_SetWindowSize(window, WIDTH, HEIGHT);
	}
}

void paintCellUnderMouse(AppState& state) {
	float mx, my;
	SDL_GetMouseState(&mx, &my);
	int c = static_cast<int>(mx) / CELL;
	int r = static_cast<int>(my) / CELL;
	if (r >= 0 && r < ROWS && c >= 0 && c < COLS) {
		state.grid[r][c] = true;
	}
}

void updateSimulation(AppState& state) {
	Grid next = step(state.grid);
	if (next == state.grid || next == state.prev) {
		// still life or period-2 oscillator
		if (++state.stuckCount >= FPS) {
			resetGrid(state, true);
		}
		return;
	}

	state.stuckCount = 0;
	state.prev = state.grid;
	state.grid = std::move(next);
}

int main(int argc, char* argv[]) {
	srand((unsigned)time(nullptr));

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		SDL_Log("SDL_Init failed: %s", SDL_GetError());
		return 1;
	}

	SDL_Window* window = SDL_CreateWindow("Conway's Game of Life", WIDTH, HEIGHT, 0);
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

	if (!SDL_SetRenderLogicalPresentation(renderer, WIDTH, HEIGHT,
		SDL_LOGICAL_PRESENTATION_LETTERBOX)) {
		SDL_Log("SDL_SetRenderLogicalPresentation failed: %s", SDL_GetError());
		SDL_DestroyRenderer(renderer);
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}

	bool running = true;
	AppState state{};
	resetGrid(state, true);
	Uint64 last = SDL_GetTicks();
	const Uint64 interval = 1000 / FPS;

	while (running) {
		SDL_Event e;
		while (SDL_PollEvent(&e)) {
			if (e.type == SDL_EVENT_QUIT) running = false;
			if (e.type == SDL_EVENT_KEY_DOWN) {
				switch (e.key.key) {
				case SDLK_ESCAPE: running = false; break;
				case SDLK_SPACE:  state.paused = !state.paused; break;
				case SDLK_R:      resetGrid(state, true); break;
				case SDLK_C:      resetGrid(state, false); break;
				case SDLK_F11:    toggleFullscreen(window, state); break;
				}
			}
			if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_MOTION) {
				if (e.type == SDL_EVENT_MOUSE_MOTION && !(e.motion.state & SDL_BUTTON_LMASK))
					break;
				paintCellUnderMouse(state);
			}
		}

		Uint64 now = SDL_GetTicks();
		if (!state.paused && now - last >= interval) {
			updateSimulation(state);
			last = now;
		}

		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
		SDL_RenderClear(renderer);

		SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
		for (int r = 0; r < ROWS; ++r)
			for (int c = 0; c < COLS; ++c)
				if (state.grid[r][c]) {
					SDL_FRect rect{ (float)(c * CELL + 1), (float)(r * CELL + 1),
									(float)(CELL - 1), (float)(CELL - 1) };
					SDL_RenderFillRect(renderer, &rect);
				}

		SDL_RenderPresent(renderer);
	}

	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}
