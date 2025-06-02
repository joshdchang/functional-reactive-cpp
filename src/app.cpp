#define SDL_MAIN_USE_CALLBACKS 1
#include "game.hpp"

//------------------------------------------------------------------------------
// SDL AppState and Main Loop
//------------------------------------------------------------------------------
struct AppState {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    TTF_Font* font = nullptr;
    NodePtr root;
    Uint64 lastTime = 0;
};

SDL_AppResult SDL_AppInit(void** appstate, int, char*[]) {
    if (!SDL_SetAppMetadata("FlappyBirdDemo", "1.0", "com.example.FlappyBird")) {
        return SDL_APP_FAILURE;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    if (!TTF_Init()) {
        SDL_Log("TTF_Init failed: %s", SDL_GetError());
        SDL_Quit();
        return SDL_APP_FAILURE;
    }

    auto* as = new AppState();
    *appstate = as;

    if (!SDL_CreateWindowAndRenderer(
            "Flappy Bird Demo",
            WINDOW_WIDTH,
            WINDOW_HEIGHT,
            0,
            &as->window,
            &as->renderer
        )) {
        SDL_Log("SDL_CreateWindowAndRenderer failed: %s", SDL_GetError());
        delete as;
        TTF_Quit();
        SDL_Quit();
        return SDL_APP_FAILURE;
    }

    as->font = TTF_OpenFont("assets/arial.ttf", 24);
    if (!as->font) {
        SDL_LogWarn(
            SDL_LOG_CATEGORY_APPLICATION,
            "Failed to load font 'assets/arial.ttf': %s. UI text will not appear.",
            SDL_GetError()
        );
    }

    as->root = Game(as->font);
    as->lastTime = SDL_GetPerformanceCounter();
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    auto* as = (AppState*)appstate;
    Uint64 now = SDL_GetPerformanceCounter();
    double dt = (now - as->lastTime) / (double)SDL_GetPerformanceFrequency();
    if (dt > 0.1) {
        dt = 0.1;
    }
    as->lastTime = now;

    SDL_SetRenderDrawColor(as->renderer, 135, 206, 235, 255);  // Sky blue
    SDL_RenderClear(as->renderer);

    updateTree(as->root, dt);
    renderTree(as->root, as->renderer);

    SDL_RenderPresent(as->renderer);
    SDL_Delay(1);
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* e) {
    AppState* as = (AppState*)appstate;
    if (e->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }

    if (as->root) {
        eventTree(as->root, e);
    }
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult /* result */) {
    if (appstate) {
        auto* as = (AppState*)appstate;
        if (as->font) {
            TTF_CloseFont(as->font);
        }
        as->root.reset();

        SDL_DestroyRenderer(as->renderer);
        SDL_DestroyWindow(as->window);
        delete as;
    }
    TTF_Quit();
    SDL_Quit();
}
