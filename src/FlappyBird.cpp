#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "engine.hpp"  // Include our new engine library

#include <algorithm>  // For std::remove
#include <deque>      // For managing pipes
#include <iostream>
#include <random>  // For pipe heights

//------------------------------------------------------------------------------
// Engine Constants & Forward Declarations
//------------------------------------------------------------------------------
constexpr int WINDOW_WIDTH = 384;   // Typical Flappy Bird width (e.g., 288 * 1.33)
constexpr int WINDOW_HEIGHT = 600;  // Typical Flappy Bird height (e.g., 512 * 1.17)

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
    SDL_FRect birdRect = {0, 0, 0, 0};       // Bird updates this
    bool flap_requested_this_frame = false;  // Input system updates this
    bool game_over_flag = false;             // Set by bird or pipes on collision
    std::function<void()> onFlapRequested;   // Bird registers this
};

Context<GameContextData> g_gameContext(
    {GameStatus::MainMenu, 0, {0, 0, 0, 0}, false, false, nullptr}
);

// Helper to get the game context data pointer
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
    // NEW: Add a state to track if we have registered our context-dependent functions.
    auto isInitializedState = useState<bool>(*node, false);

    // NOTE: We've removed the context access from here, as it's too early.

    useUpdate(
        *node,
        // Add isInitializedState to the lambda's capture list
        [yPosState, yVelState, rotationState, isInitializedState, node_ptr = node.get()](
            double dt
        ) mutable {
            GameContextData* gameData = getGameData(*node_ptr);
            if (!gameData) {
                return;  // Can't do anything without game data
            }

            // --- START OF MODIFICATION ---
            // Perform one-time setup inside the first update call.
            // At this point, the node is in the tree and getGameData() will succeed.
            if (!isInitializedState.get()) {
                // Register the flap handler now.
                gameData->onFlapRequested = [yVelState]() mutable { yVelState.set(FLAP_VELOCITY); };
                isInitializedState.set(true);  // Ensure this only runs once.
            }
            // --- END OF MODIFICATION ---

            if (gameData->status == GameStatus::Playing) {
                float yVel = yVelState.get();
                float yPos = yPosState.get();

                // Apply gravity
                yVel += GRAVITY * static_cast<float>(dt);
                yPos += yVel * static_cast<float>(dt);

                yPosState.set(yPos);
                yVelState.set(yVel);

                // Rotation (simple)
                float rotation = yVel * 0.05f;  // Adjust multiplier for desired tilt
                if (rotation > 30.0f) {
                    rotation = 30.0f;
                }
                if (rotation < -30.0f) {
                    rotation = -30.0f;
                }
                rotationState.set(rotation);

                // Update birdRect in context
                gameData->birdRect = {
                    BIRD_X_POSITION - BIRD_WIDTH / 2,
                    yPos - BIRD_HEIGHT / 2,
                    BIRD_WIDTH,
                    BIRD_HEIGHT
                };

                // Check bounds collision
                if (yPos + BIRD_HEIGHT / 2 > WINDOW_HEIGHT || yPos - BIRD_HEIGHT / 2 < 0) {
                    if (gameData->status == GameStatus::Playing) {
                        gameData->game_over_flag = true;
                    }
                }
            } else if (gameData->status == GameStatus::MainMenu ||
                       gameData->status == GameStatus::GameOver) {
                // Reset bird position for new game, or keep it static
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
        SDL_FPoint center = {BIRD_WIDTH / 2, BIRD_HEIGHT / 2};
        SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255);  // Yellow
        SDL_RenderFillRect(renderer, &bird_rect);            // Simple rect, rotation needs texture
        // For rotated rect: SDL_RenderTextureRotated or manual drawing
    });

    return node;
}

//------------------------------------------------------------------------------
// Pipe Component
//------------------------------------------------------------------------------
struct PipeData {
    float xPos;
    float topPipeHeight;  // Height of the top opening of the bottom pipe
    bool scored = false;
    SDL_FRect topRect;
    SDL_FRect bottomRect;
};

NodePtr PipePair(float initialX, float topPipeOpeningY) {
    auto node = std::make_shared<Node>();
    // This node itself doesn't need much state if PipeManager manages PipeData externally
    // Or, it can hold its own PipeData via useState. Let's try that.
    auto pipeState = useState<PipeData>(
        *node,
        {
            initialX,
            topPipeOpeningY,
            false,
            {initialX, 0, PIPE_WIDTH, topPipeOpeningY},  // Top pipe rect
            {initialX,
             topPipeOpeningY + PIPE_GAP_HEIGHT,
             PIPE_WIDTH,
             WINDOW_HEIGHT - (topPipeOpeningY + PIPE_GAP_HEIGHT)}  // Bottom pipe rect
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
    // Store deque of PipeData directly, not NodePtrs to pipes, for easier management here.
    // Or, store NodePtrs and have each pipe update its own state.
    // Let's use NodePtrs to individual pipe components for consistency with engine.
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
                if (gameData && gameData->status !=
                                    GameStatus::Playing) {  // Clear pipes on game over/main menu
                    auto& pipes = activePipesState.getRef();
                    pipes.clear();
                    node_ptr->children.clear();                // Also remove from scene graph
                    spawnTimerState.set(PIPE_SPAWN_INTERVAL);  // Reset spawn timer
                }
                return;
            }

            float spawnTimer = spawnTimerState.get();
            spawnTimer -= static_cast<float>(dt);

            // Spawn new pipes
            if (spawnTimer <= 0) {
                float topPipeOpeningY = MIN_PIPE_HEIGHT + distrib(gen);
                NodePtr newPipe = PipePair(WINDOW_WIDTH + PIPE_WIDTH / 2, topPipeOpeningY);
                activePipesState.getRef().push_back(newPipe);
                node_ptr->AddChild(newPipe);  // Add to scene graph for rendering/updates
                spawnTimer = PIPE_SPAWN_INTERVAL;
            }
            spawnTimerState.set(spawnTimer);

            // Move and check pipes
            auto& pipes = activePipesState.getRef();
            for (int i = 0; i < pipes.size(); ++i) {
                NodePtr pipeNode = pipes[i];
                auto pipeDataState = pipeNode->getStateSlot<PipeData>();  // Assume PipePair
                                                                          // uses useState<PipeData>
                if (!pipeDataState) {
                    continue;
                }

                PipeData currentData = pipeDataState->value;  // Get a copy
                currentData.xPos -= PIPE_SPEED * static_cast<float>(dt);
                currentData.topRect.x = currentData.xPos - PIPE_WIDTH / 2;
                currentData.bottomRect.x = currentData.xPos - PIPE_WIDTH / 2;
                pipeDataState->value = currentData;  // Set it back

                // Collision check
                if (SDL_HasRectIntersectionFloat(&gameData->birdRect, &currentData.topRect) ||
                    SDL_HasRectIntersectionFloat(&gameData->birdRect, &currentData.bottomRect)) {
                    if (gameData->status == GameStatus::Playing) {
                        gameData->game_over_flag = true;
                    }
                }

                // Scoring
                if (!currentData.scored && currentData.xPos < gameData->birdRect.x) {
                    currentData.scored = true;
                    pipeDataState->value = currentData;  // Update scored status
                    gameData->score++;
                }
            }

            // Despawn pipes
            if (!pipes.empty()) {
                // MODIFICATION START
                NodePtr pipeToRemove = pipes.front();  // Store the node before popping
                auto firstPipeDataState = pipeToRemove->getStateSlot<PipeData>();
                if (firstPipeDataState && firstPipeDataState->value.xPos < -PIPE_WIDTH) {
                    pipes.pop_front();
                    // Also remove from node_ptr->children.
                    node_ptr->children.erase(
                        std::remove(
                            node_ptr->children.begin(),
                            node_ptr->children.end(),
                            pipeToRemove  // Use the stored pointer here
                        ),
                        node_ptr->children.end()
                    );
                }
                // MODIFICATION END
            }
        }
    );
    // Rendering of pipes is handled by renderTree as pipes are children of PipeManager.

    return node;
}

//------------------------------------------------------------------------------
// UI Component (Basic)
//------------------------------------------------------------------------------
NodePtr UIManager(TTF_Font* font) {
    auto node = std::make_shared<Node>();

    useRender(*node, [font, node_ptr = node.get()](SDL_Renderer* renderer) {
        GameContextData* gameData = getGameData(*node_ptr);
        if (!gameData || !font) {
            return;
        }

        SDL_Color white = {255, 255, 255, 255};
        SDL_Color black = {0, 0, 0, 255};
        std::string text;

        if (gameData->status == GameStatus::MainMenu) {
            text = "Flap to Start";
        } else if (gameData->status == GameStatus::Playing) {
            text = "Score: " + std::to_string(gameData->score);
        } else if (gameData->status == GameStatus::GameOver) {
            text = "Game Over! Score: " + std::to_string(gameData->score) + ".";
        }

        if (!text.empty()) {
            SDL_Surface* surface = TTF_RenderText_Solid(font, text.c_str(), text.length(), black);
            if (surface) {
                SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
                if (texture) {
                    SDL_FRect dstRect = {
                        WINDOW_WIDTH / 2.0f - surface->w / 2.0f,
                        50.0f,
                        (float)surface->w,
                        (float)surface->h
                    };
                    if (gameData->status == GameStatus::GameOver) {  // Move game over text lower
                        dstRect.y = WINDOW_HEIGHT / 2.0f - surface->h / 2.0f;
                    }
                    SDL_RenderTexture(renderer, texture, nullptr, &dstRect);
                    SDL_DestroyTexture(texture);
                }
                SDL_DestroySurface(surface);
            }
        }
    });
    return node;
}

//------------------------------------------------------------------------------
// Main Game Logic Component
//------------------------------------------------------------------------------
NodePtr Game(TTF_Font* font) {
    // This node will be the ContextProvider for GameContextData
    auto gameNode = std::make_shared<Node>(nullptr);  // Root game node

    auto gameContextState = useState<GameContextData>(*gameNode, g_gameContext.defaultValue);

    gameNode->provideContext<GameContextData>(g_gameContext.typeId, &gameContextState.getRef());

    gameNode->AddChild(Bird());
    gameNode->AddChild(PipeManager());
    gameNode->AddChild(UIManager(font));

    useUpdate(*gameNode, [gameContextState, gameNode_ptr = gameNode.get()](double /*dt*/) mutable {
        GameContextData& gameData = gameContextState.getRef();  // Get reference to modify

        if (gameData.flap_requested_this_frame) {
            if (gameData.status == GameStatus::MainMenu) {
                gameData.status = GameStatus::Playing;
                gameData.score = 0;
                gameData.game_over_flag = false;
            } else if (gameData.status == GameStatus::Playing) {
                if (gameData.onFlapRequested) {
                    gameData.onFlapRequested();
                }
            } else if (gameData.status == GameStatus::GameOver) {
                gameData.status = GameStatus::MainMenu;  // Go to main menu, will reset bird etc.
                gameData.score = 0;
                gameData.game_over_flag = false;
            }
            gameData.flap_requested_this_frame = false;  // Consume flap
        }

        if (gameData.game_over_flag && gameData.status == GameStatus::Playing) {
            gameData.status = GameStatus::GameOver;
        }

        // Update the shared context value if gameData was modified
        // Since gameContextState.getRef() gives direct access, modifications are already in
        // the state. However, if provideContext stored a copy, we'd need to update it:
        // gameNode_ptr->provideContext<GameContextData>(g_gameContext.typeId, gameData);
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
    NodePtr root;  // Will be the Game Node
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
        delete as;
        TTF_Quit();
        SDL_Quit();
        return SDL_APP_FAILURE;
    }

    as->font = TTF_OpenFont("assets/arial.ttf", 24);  // Provide a path to a .ttf font
    if (!as->font) {
        SDL_LogWarn(
            SDL_LOG_CATEGORY_APPLICATION,
            "Failed to load font: %s. UI text will not appear.",
            SDL_GetError()
        );
        // Game can continue without font, UI will just be blank.
    }

    as->root = Game(as->font);  // Create the main game node
    as->lastTime = SDL_GetPerformanceCounter();
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    auto* as = (AppState*)appstate;
    Uint64 now = SDL_GetPerformanceCounter();
    double dt = (now - as->lastTime) / (double)SDL_GetPerformanceFrequency();
    if (dt > 0.1) {
        dt = 0.1;  // Cap dt to prevent large jumps
    }
    as->lastTime = now;

    SDL_SetRenderDrawColor(as->renderer, 135, 206, 235, 255);  // Sky blue background
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

    if (e->type == SDL_EVENT_KEY_DOWN) {
        if (e->key.scancode == SDL_SCANCODE_SPACE || e->key.scancode == SDL_SCANCODE_UP) {
            // Access game context directly from root if it's the provider
            // This is a bit of a shortcut; ideally, input events would be dispatched more
            // formally.
            if (as->root) {
                auto gameContextState =
                    as->root->getStateSlot<GameContextData>();  // Assuming root node used
                                                                // useState for GameContextData
                if (gameContextState) {
                    gameContextState->value.flap_requested_this_frame = true;
                } else {  // Fallback if the root itself is not the direct state holder for
                          // GameContextData
                    GameContextData* gameData =
                        getGameData(*(as->root));  // Try to get from context provider system
                    if (gameData) {
                        gameData->flap_requested_this_frame = true;
                    }
                }
            }
        }
    } else if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        if (e->button.button == SDL_BUTTON_LEFT) {
            if (as->root) {
                auto gameContextState = as->root->getStateSlot<GameContextData>();
                if (gameContextState) {
                    gameContextState->value.flap_requested_this_frame = true;
                } else {
                    GameContextData* gameData = getGameData(*(as->root));
                    if (gameData) {
                        gameData->flap_requested_this_frame = true;
                    }
                }
            }
        }
    }
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult) {
    if (appstate) {
        auto* as = (AppState*)appstate;
        if (as->font) {
            TTF_CloseFont(as->font);
        }
        SDL_DestroyRenderer(as->renderer);
        SDL_DestroyWindow(as->window);
        delete as;
    }
    TTF_Quit();
    SDL_Quit();
}
