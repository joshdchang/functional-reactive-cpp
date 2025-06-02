#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "engine.hpp"  // Include our new engine library

#include <algorithm>   // For std::remove
#include <deque>       // For managing pipes
#include <functional>  // For std::function
#include <iostream>
#include <random>  // For pipe heights
#include <string>  // For std::string and std::to_string

//------------------------------------------------------------------------------
// Engine Constants & Forward Declarations
//------------------------------------------------------------------------------
constexpr int WINDOW_WIDTH = 384;
constexpr int WINDOW_HEIGHT = 600;

//------------------------------------------------------------------------------
// Flappy Bird Game Constants
//------------------------------------------------------------------------------
const float GRAVITY = 1200.0f;
const float FLAP_VELOCITY = -400.0f;
const float BIRD_X_POSITION = WINDOW_WIDTH / 4.0f;
const float BIRD_WIDTH = 34.0f;
const float BIRD_HEIGHT = 24.0f;

const float PIPE_WIDTH = 70.0f;
const float PIPE_GAP_HEIGHT = 150.0f;
const float PIPE_SPEED = 150.0f;
const float PIPE_SPAWN_INTERVAL = 2.0f;  // seconds
const int MIN_PIPE_HEIGHT = 80;
const int MAX_PIPE_HEIGHT_OFFSET = WINDOW_HEIGHT - PIPE_GAP_HEIGHT - MIN_PIPE_HEIGHT * 2;

//------------------------------------------------------------------------------
// Game State and Context
//------------------------------------------------------------------------------
enum class GameStatus { MainMenu, Playing, GameOver };

struct GameContextData {
    GameStatus status = GameStatus::MainMenu;
    int score = 0;
    SDL_FRect birdRect = {0, 0, 0, 0};
    bool flap_requested_this_frame = false;
    bool game_over_flag = false;
    std::function<void()> onFlapRequested;
};

Context<GameContextData> g_gameContext(
    {GameStatus::MainMenu, 0, {0, 0, 0, 0}, false, false, nullptr}
);

GameContextData* getGameData(Node& n) {
    return n.getContextValue<GameContextData>(g_gameContext.typeId);
}

//------------------------------------------------------------------------------
// Bird Component
//------------------------------------------------------------------------------
NodePtr Bird() {
    auto node = std::make_shared<Node>();

    auto yPosState = useState<float>(*node, WINDOW_HEIGHT / 2.0f);
    auto yVelState = useState<float>(*node, 0.0f);
    auto rotationState = useState<float>(*node, 0.0f);
    auto isInitializedState = useState<bool>(*node, false);

    useUpdate(
        *node,
        [yPosState, yVelState, rotationState, isInitializedState, node_ptr = node.get()](
            double dt
        ) mutable {
            GameContextData* gameData = getGameData(*node_ptr);
            if (!gameData) {
                return;
            }

            if (!isInitializedState.get()) {
                gameData->onFlapRequested = [yVelState]() mutable { yVelState.set(FLAP_VELOCITY); };
                isInitializedState.set(true);
            }

            if (gameData->status == GameStatus::Playing) {
                float yVel = yVelState.get();
                float yPos = yPosState.get();

                yVel += GRAVITY * static_cast<float>(dt);
                yPos += yVel * static_cast<float>(dt);

                yPosState.set(yPos);
                yVelState.set(yVel);

                float rotation = yVel * 0.05f;
                if (rotation > 30.0f) {
                    rotation = 30.0f;
                }
                if (rotation < -30.0f) {
                    rotation = -30.0f;
                }
                rotationState.set(rotation);

                gameData->birdRect = {
                    BIRD_X_POSITION - BIRD_WIDTH / 2,
                    yPos - BIRD_HEIGHT / 2,
                    BIRD_WIDTH,
                    BIRD_HEIGHT
                };

                if (yPos + BIRD_HEIGHT / 2 > WINDOW_HEIGHT || yPos - BIRD_HEIGHT / 2 < 0) {
                    if (gameData->status == GameStatus::Playing) {
                        gameData->game_over_flag = true;
                    }
                }
            } else if (gameData->status == GameStatus::MainMenu ||
                       gameData->status == GameStatus::GameOver) {
                if (gameData->status == GameStatus::MainMenu) {
                    yPosState.set(WINDOW_HEIGHT / 2.0f);
                    yVelState.set(0.0f);
                    rotationState.set(0.0f);
                    gameData->birdRect = {
                        BIRD_X_POSITION - BIRD_WIDTH / 2,
                        WINDOW_HEIGHT / 2.0f - BIRD_HEIGHT / 2,
                        BIRD_WIDTH,
                        BIRD_HEIGHT
                    };
                }
            }
        }
    );

    useRender(*node, [yPosState, rotationState](SDL_Renderer* renderer) {
        SDL_FRect bird_rect = {
            BIRD_X_POSITION - BIRD_WIDTH / 2,
            yPosState.get() - BIRD_HEIGHT / 2,
            BIRD_WIDTH,
            BIRD_HEIGHT
        };
        SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255);  // Yellow
        SDL_RenderFillRect(renderer, &bird_rect);
    });

    return node;
}

//------------------------------------------------------------------------------
// Pipe Component
//------------------------------------------------------------------------------
struct PipeData {
    float xPos;
    float topPipeHeight;
    bool scored = false;
    SDL_FRect topRect;
    SDL_FRect bottomRect;
};

NodePtr PipePair(float initialX, float topPipeOpeningY) {
    auto node = std::make_shared<Node>();
    auto pipeState = useState<PipeData>(
        *node,
        {
            initialX,
            topPipeOpeningY,
            false,
            {
                initialX,
                0,
                PIPE_WIDTH,
                topPipeOpeningY,
            },
            {
                initialX,
                topPipeOpeningY + PIPE_GAP_HEIGHT,
                PIPE_WIDTH,
                WINDOW_HEIGHT - (topPipeOpeningY + PIPE_GAP_HEIGHT),
            },
        }
    );

    useRender(*node, [pipeState](SDL_Renderer* renderer) {
        PipeData data = pipeState.get();
        SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);  // Green pipes
        SDL_RenderFillRect(renderer, &data.topRect);
        SDL_RenderFillRect(renderer, &data.bottomRect);
    });
    return node;
}

//------------------------------------------------------------------------------
// Pipe Manager Component
//------------------------------------------------------------------------------
NodePtr PipeManager() {
    auto node = std::make_shared<Node>();
    auto activePipesState = useState<std::deque<NodePtr>>(*node, {});
    auto spawnTimerState = useState<float>(*node, PIPE_SPAWN_INTERVAL);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(0, MAX_PIPE_HEIGHT_OFFSET);

    useUpdate(
        *node,
        [activePipesState, spawnTimerState, node_ptr = node.get(), distrib, gen](
            double dt
        ) mutable {
            GameContextData* gameData = getGameData(*node_ptr);
            if (!gameData || gameData->status != GameStatus::Playing) {
                if (gameData && gameData->status != GameStatus::Playing) {
                    auto& pipes = activePipesState.getRef();
                    pipes.clear();
                    node_ptr->children.clear();
                    spawnTimerState.set(PIPE_SPAWN_INTERVAL);
                }
                return;
            }

            float spawnTimer = spawnTimerState.get();
            spawnTimer -= static_cast<float>(dt);

            if (spawnTimer <= 0) {
                float topPipeOpeningY = static_cast<float>(MIN_PIPE_HEIGHT + distrib(gen));
                NodePtr newPipe = PipePair(WINDOW_WIDTH + PIPE_WIDTH / 2, topPipeOpeningY);
                activePipesState.getRef().push_back(newPipe);
                node_ptr->AddChild(newPipe);
                spawnTimer = PIPE_SPAWN_INTERVAL;
            }
            spawnTimerState.set(spawnTimer);

            auto& pipes = activePipesState.getRef();
            for (size_t i = 0; i < pipes.size(); ++i) {  // Use size_t for loop counter
                NodePtr pipeNode = pipes[i];
                auto pipeDataState = pipeNode->getStateSlot<PipeData>();
                if (!pipeDataState) {
                    continue;
                }

                PipeData currentData = pipeDataState->value;
                currentData.xPos -= PIPE_SPEED * static_cast<float>(dt);
                currentData.topRect.x = currentData.xPos - PIPE_WIDTH / 2;
                currentData.bottomRect.x = currentData.xPos - PIPE_WIDTH / 2;
                pipeDataState->value = currentData;

                if (SDL_HasRectIntersectionFloat(&gameData->birdRect, &currentData.topRect) ||
                    SDL_HasRectIntersectionFloat(&gameData->birdRect, &currentData.bottomRect)) {
                    if (gameData->status == GameStatus::Playing) {
                        gameData->game_over_flag = true;
                    }
                }

                if (!currentData.scored && currentData.xPos < gameData->birdRect.x) {
                    currentData.scored = true;
                    pipeDataState->value = currentData;
                    gameData->score++;
                }
            }

            if (!pipes.empty()) {
                NodePtr pipeToRemove = pipes.front();
                auto firstPipeDataState = pipeToRemove->getStateSlot<PipeData>();
                if (firstPipeDataState && firstPipeDataState->value.xPos < -PIPE_WIDTH) {
                    pipes.pop_front();
                    node_ptr->children.erase(
                        std::remove(
                            node_ptr->children.begin(),
                            node_ptr->children.end(),
                            pipeToRemove
                        ),
                        node_ptr->children.end()
                    );
                }
            }
        }
    );
    return node;
}

//------------------------------------------------------------------------------
// Text Component
//------------------------------------------------------------------------------
NodePtr Text(
    Prop<TTF_Font*> font,        // Font to use for rendering
    Prop<SDL_Color> color,       // Color of the text
    Prop<std::string> text,      // Flexible text property
    Prop<SDL_FPoint> position,   // Flexible position property
    Prop<bool> isVisible = true  // Flexible visibility, defaults to true
) {
    auto node = std::make_shared<Node>();

    useRender(*node, [font, color, text, position, isVisible](SDL_Renderer* renderer) {
        auto currentIsVisible = getVal(isVisible);
        if (!currentIsVisible) {
            return;
        }

        auto currentText = getVal(text);
        if (currentText.empty()) {
            return;
        }

        auto currentPosition = getVal(position);

        SDL_Surface* surface = TTF_RenderText_Solid(
            getVal(font),
            currentText.c_str(),
            currentText.length(),
            getVal(color)
        );
        if (!surface) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_APPLICATION,
                "TTF_RenderText_Solid failed: %s",
                SDL_GetError()
            );
            return;
        }

        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
        if (!texture) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_APPLICATION,
                "SDL_CreateTextureFromSurface failed: %s",
                SDL_GetError()
            );
            SDL_DestroySurface(surface);
            return;
        }

        SDL_FRect dstRect;
        dstRect.w = static_cast<float>(surface->w);
        dstRect.h = static_cast<float>(surface->h);
        dstRect.x = currentPosition.x - dstRect.w / 2.0f;
        dstRect.y = currentPosition.y;

        SDL_RenderTexture(renderer, texture, nullptr, &dstRect);
        SDL_DestroyTexture(texture);
        SDL_DestroySurface(surface);
    });

    return node;
}

NodePtr Game(TTF_Font* font) {  // Font is passed in for UI elements
    auto gameNode = std::make_shared<Node>(nullptr);

    // Initialize and provide game context
    auto gameContextState = useState<GameContextData>(*gameNode, g_gameContext.defaultValue);
    gameNode->provideContext<GameContextData>(g_gameContext.typeId, &gameContextState.getRef());

    // Game logic update
    useUpdate(*gameNode, [gameContextState](double /*dt*/) mutable {
        GameContextData& gameData = gameContextState.getRef();  // Modifiable access

        if (gameData.game_over_flag && gameData.status == GameStatus::Playing) {
            gameData.status = GameStatus::GameOver;
        }
    });

    useEvent(*gameNode, [gameContextState](SDL_Event* e) mutable {
        if (e->type == SDL_EVENT_KEY_DOWN) {
            if (e->key.scancode == SDL_SCANCODE_SPACE || e->key.scancode == SDL_SCANCODE_UP) {
                GameContextData& gameData = gameContextState.getRef();
                if (gameData.status == GameStatus::MainMenu) {
                    gameData.status = GameStatus::Playing;
                    gameData.score = 0;
                    gameData.game_over_flag = false;
                } else if (gameData.status == GameStatus::Playing) {
                    if (gameData.onFlapRequested) {
                        gameData.onFlapRequested();
                    }
                } else if (gameData.status == GameStatus::GameOver) {
                    gameData.status = GameStatus::MainMenu;
                    gameData.score = 0;
                    gameData.game_over_flag = false;
                }
            }
        }
    });

    // --- UI Text Elements ---
    SDL_Color textColor = {0, 0, 0, 255};  // Black text

    // Score Text (displayed during gameplay)
    SDL_FPoint scorePosition = {WINDOW_WIDTH / 2.0f, 50.0f};  // Fixed position for score

    // Set game elements as children in a more declarative way
    gameNode->SetChildren({
        Bird(),
        PipeManager(),
        Text(
            font,
            textColor,
            [gameContextState]() -> std::string {               // Text provider
                const auto& gameData = gameContextState.get();  // Read-only access
                if (gameData.status == GameStatus::MainMenu) {
                    return "Flap to Start";
                } else if (gameData.status == GameStatus::GameOver) {
                    return "Game Over! Score: " + std::to_string(gameData.score) + ".";
                }
                return "";  // Empty if no message for current state
            },
            [gameContextState]() -> SDL_FPoint {                // Position provider
                const auto& gameData = gameContextState.get();  // Read-only access
                if (gameData.status == GameStatus::GameOver) {
                    // Center game over message
                    return {
                        WINDOW_WIDTH / 2.0f,
                        WINDOW_HEIGHT / 2.0f - 25.0f
                    };  // Adjusted Y slightly
                }
                // Default position for main menu message
                return {WINDOW_WIDTH / 2.0f, 100.0f};
            },
            [gameContextState]() -> bool {                      // Visibility provider
                const auto& gameData = gameContextState.get();  // Read-only access
                return gameData.status == GameStatus::MainMenu ||
                       gameData.status == GameStatus::GameOver;
            }
        ),
        Text(
            font,
            textColor,
            [gameContextState]() -> std::string {               // Text provider
                const auto& gameData = gameContextState.get();  // Read-only access
                // Always show score, even if 0, when playing
                return "Score: " + std::to_string(gameData.score);
            },
            [scorePosition]() -> SDL_FPoint {  // Position provider (fixed for score)
                return scorePosition;
            },
            [gameContextState]() -> bool {                      // Visibility provider
                const auto& gameData = gameContextState.get();  // Read-only access
                return gameData.status == GameStatus::Playing;
            }
        ),
    });

    return gameNode;
}

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
            0,  // SDL_WINDOW_RESIZABLE could be an option
            &as->window,
            &as->renderer
        )) {
        SDL_Log("SDL_CreateWindowAndRenderer failed: %s", SDL_GetError());
        delete as;
        TTF_Quit();
        SDL_Quit();
        return SDL_APP_FAILURE;
    }

    as->font = TTF_OpenFont("assets/arial.ttf", 24);  // Ensure this path is correct
    if (!as->font) {
        SDL_LogWarn(
            SDL_LOG_CATEGORY_APPLICATION,
            "Failed to load font 'assets/arial.ttf': %s. UI text will not appear.",
            SDL_GetError()
        );
        // Game can technically continue, but UI will be missing.
    }

    as->root = Game(as->font);  // Pass font to Game component
    as->lastTime = SDL_GetPerformanceCounter();
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    auto* as = (AppState*)appstate;
    Uint64 now = SDL_GetPerformanceCounter();
    double dt = (now - as->lastTime) / (double)SDL_GetPerformanceFrequency();
    if (dt > 0.1) {  // Delta time clamping
        dt = 0.1;
    }
    as->lastTime = now;

    // Clear screen
    SDL_SetRenderDrawColor(as->renderer, 135, 206, 235, 255);  // Sky blue
    SDL_RenderClear(as->renderer);

    // Update and Render game tree
    updateTree(as->root, dt);
    renderTree(as->root, as->renderer);

    SDL_RenderPresent(as->renderer);
    SDL_Delay(1);  // Small delay to prevent 100% CPU usage, adjust as needed
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* e) {
    AppState* as = (AppState*)appstate;
    if (e->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;  // Signal to exit the main loop
    }

    // Propagate event to the node tree
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
        // renderTree(as->root, nullptr); // Optional: A chance for nodes to clean up SDL resources
        // if they hold any directly
        as->root.reset();  // Release the root node and its children

        SDL_DestroyRenderer(as->renderer);
        SDL_DestroyWindow(as->window);
        delete as;
    }
    TTF_Quit();
    SDL_Quit();
}
