#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "engine.hpp"

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
// Game Status Enum
//------------------------------------------------------------------------------
enum class GameStatus { MainMenu, Playing, GameOver };

//------------------------------------------------------------------------------
// Bird Component
//------------------------------------------------------------------------------
NodePtr Bird(
    State<GameStatus> gameStatusProp,
    State<SDL_FRect> birdRectProp,  // Bird writes its rect here
    State<bool> gameOverFlagProp,   // Bird sets this to true on collision/out of bounds
    State<std::function<void()>>
        setOnFlapRequestedProp  // Game provides this for Bird to set its flap action
) {
    auto node = std::make_shared<Node>();

    auto yPosState = useState<float>(*node, WINDOW_HEIGHT / 2.0f);
    auto yVelState = useState<float>(*node, 0.0f);
    auto rotationState = useState<float>(*node, 0.0f);

    // Set the flap request callback. This is called once when Bird is created.
    setOnFlapRequestedProp.set([yVelState]() mutable { yVelState.set(FLAP_VELOCITY); });

    useUpdate(
        *node,
        [yPosState, yVelState, rotationState, gameStatusProp, birdRectProp, gameOverFlagProp](
            double dt
        ) mutable {
            GameStatus currentStatus = gameStatusProp.get();

            if (currentStatus == GameStatus::Playing) {
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

                birdRectProp.set(
                    {// Update the shared birdRect state
                     BIRD_X_POSITION - BIRD_WIDTH / 2,
                     yPos - BIRD_HEIGHT / 2,
                     BIRD_WIDTH,
                     BIRD_HEIGHT
                    }
                );

                // Check bounds
                if (yPos + BIRD_HEIGHT / 2 > WINDOW_HEIGHT || yPos - BIRD_HEIGHT / 2 < 0) {
                    if (currentStatus ==
                        GameStatus::Playing) {  // Ensure we only set game over if playing
                        gameOverFlagProp.set(true);
                    }
                }
            } else if (currentStatus == GameStatus::MainMenu ||
                       currentStatus == GameStatus::GameOver) {
                // Reset bird position and physics for MainMenu or GameOver
                float initialYPos = WINDOW_HEIGHT / 2.0f;
                yPosState.set(initialYPos);
                yVelState.set(0.0f);
                rotationState.set(0.0f);
                birdRectProp.set(
                    {// Update the shared birdRect state
                     BIRD_X_POSITION - BIRD_WIDTH / 2,
                     initialYPos - BIRD_HEIGHT / 2,
                     BIRD_WIDTH,
                     BIRD_HEIGHT
                    }
                );
            }
        }
    );

    useRender(
        *node,
        [yPosState, rotationState /* unused but kept for potential visual rotation */](
            SDL_Renderer* renderer
        ) {
            SDL_FRect bird_render_rect = {
                BIRD_X_POSITION - BIRD_WIDTH / 2,
                yPosState.get() - BIRD_HEIGHT / 2,
                BIRD_WIDTH,
                BIRD_HEIGHT
            };
            SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255);  // Yellow
            SDL_RenderFillRect(renderer, &bird_render_rect);
        }
    );

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
NodePtr PipeManager(
    State<GameStatus> gameStatusProp,
    State<SDL_FRect> birdRectProp,  // PipeManager reads this
    State<int> scoreProp,           // PipeManager writes to this
    State<bool> gameOverFlagProp    // PipeManager writes to this
) {
    auto node = std::make_shared<Node>();
    auto activePipesState = useState<std::deque<NodePtr>>(*node, {});
    auto spawnTimerState = useState<float>(*node, PIPE_SPAWN_INTERVAL);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(0, MAX_PIPE_HEIGHT_OFFSET);

    useUpdate(
        *node,
        [activePipesState,
         spawnTimerState,
         node_ptr = node.get(),
         distrib,
         gen,
         gameStatusProp,
         birdRectProp,
         scoreProp,
         gameOverFlagProp](double dt) mutable {
            GameStatus currentStatus = gameStatusProp.get();

            if (currentStatus != GameStatus::Playing) {
                // Clear pipes and reset spawn timer if not playing
                auto& pipes = activePipesState.getRef();
                if (!pipes.empty()) {
                    pipes.clear();
                    node_ptr->SetChildren({});  // Clear children from the node and unparent
                }
                spawnTimerState.set(PIPE_SPAWN_INTERVAL);  // Reset spawn timer
                return;
            }

            // If playing, proceed with pipe logic
            float spawnTimer = spawnTimerState.get();
            spawnTimer -= static_cast<float>(dt);

            if (spawnTimer <= 0) {
                float topPipeOpeningY = static_cast<float>(MIN_PIPE_HEIGHT + distrib(gen));
                NodePtr newPipe = PipePair(WINDOW_WIDTH + PIPE_WIDTH / 2, topPipeOpeningY);
                activePipesState.getRef().push_back(newPipe);
                node_ptr->AddChild(newPipe);  // Add new pipe as a child of PipeManager
                spawnTimer = PIPE_SPAWN_INTERVAL;
            }
            spawnTimerState.set(spawnTimer);

            auto& pipes = activePipesState.getRef();
            SDL_FRect currentBirdRect = birdRectProp.get();  // Get current bird rect

            for (size_t i = 0; i < pipes.size(); ++i) {
                NodePtr pipeNode = pipes[i];
                // getStateSlot returns a shared_ptr to the TypedStateSlot
                auto pipeDataStateSlotPtr = pipeNode->getStateSlot<PipeData>();
                if (!pipeDataStateSlotPtr) {  // Check if the slot exists
                    continue;
                }

                PipeData currentData = pipeDataStateSlotPtr->value;  // Make a copy to modify
                currentData.xPos -= PIPE_SPEED * static_cast<float>(dt);
                currentData.topRect.x = currentData.xPos - PIPE_WIDTH / 2;
                currentData.bottomRect.x = currentData.xPos - PIPE_WIDTH / 2;

                // Collision Check
                if (SDL_HasRectIntersectionFloat(&currentBirdRect, &currentData.topRect) ||
                    SDL_HasRectIntersectionFloat(&currentBirdRect, &currentData.bottomRect)) {
                    if (currentStatus == GameStatus::Playing) {  // Should be true here
                        gameOverFlagProp.set(true);
                    }
                }

                // Score Check
                if (!currentData.scored && currentData.xPos < currentBirdRect.x) {
                    currentData.scored = true;
                    scoreProp.set(scoreProp.get() + 1);
                }
                pipeDataStateSlotPtr->value = currentData;  // Update the pipe's state
            }

            // Remove off-screen pipes
            if (!pipes.empty()) {
                NodePtr pipeToRemoveNode = pipes.front();
                auto firstPipeDataStateSlotPtr = pipeToRemoveNode->getStateSlot<PipeData>();
                if (firstPipeDataStateSlotPtr &&
                    firstPipeDataStateSlotPtr->value.xPos < -PIPE_WIDTH) {
                    pipes.pop_front();
                    // Remove the child from the node's children list
                    // This directly manipulates the children vector.
                    // For more complex scenarios, a RemoveChild method on Node or rebuilding
                    // children might be preferred.
                    auto& children = node_ptr->children;
                    children.erase(
                        std::remove(children.begin(), children.end(), pipeToRemoveNode),
                        children.end()
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

NodePtr Game(TTF_Font* font) {
    auto gameNode = std::make_shared<Node>(nullptr);

    // Individual states for game data
    auto statusState = useState<GameStatus>(*gameNode, GameStatus::MainMenu);
    auto scoreState = useState<int>(*gameNode, 0);
    auto birdRectState = useState<SDL_FRect>(
        *gameNode,
        {
            BIRD_X_POSITION - BIRD_WIDTH / 2,
            WINDOW_HEIGHT / 2.0f - BIRD_HEIGHT / 2,
            BIRD_WIDTH,
            BIRD_HEIGHT,
        }
    ); 
    auto gameOverFlagState = useState<bool>(*gameNode, false);
    // This state will hold the callback provided by the Bird component
    auto onFlapRequestedCallbackState = useState<std::function<void()>>(*gameNode, nullptr);

    // Game logic update
    useUpdate(*gameNode, [statusState, gameOverFlagState](double /*dt*/) mutable {
        if (gameOverFlagState.get() && statusState.get() == GameStatus::Playing) {
            statusState.set(GameStatus::GameOver);
            // gameOverFlagState is reset when transitioning to MainMenu or Playing
        }
    });

    useEvent(
        *gameNode,
        [statusState, scoreState, gameOverFlagState, onFlapRequestedCallbackState](
            SDL_Event* e
        ) mutable {
            if (e->type == SDL_EVENT_KEY_DOWN) {
                if (e->key.scancode == SDL_SCANCODE_SPACE || e->key.scancode == SDL_SCANCODE_UP) {
                    GameStatus currentStatus = statusState.get();
                    if (currentStatus == GameStatus::MainMenu) {
                        statusState.set(GameStatus::Playing);
                        scoreState.set(0);
                        gameOverFlagState.set(false);  // Reset game over flag
                    } else if (currentStatus == GameStatus::Playing) {
                        auto flapCb = onFlapRequestedCallbackState.get();
                        if (flapCb) {
                            flapCb();
                        }
                    } else if (currentStatus == GameStatus::GameOver) {
                        statusState.set(GameStatus::MainMenu);
                        scoreState.set(0);
                        gameOverFlagState.set(false);  // Reset game over flag
                    }
                }
            }
        }
    );

    // --- UI Text Elements ---
    SDL_Color textColor = {0, 0, 0, 255};                     // Black text
    SDL_FPoint scorePosition = {WINDOW_WIDTH / 2.0f, 50.0f};  // Fixed position for score

    // Create children by passing the states
    auto birdNode =
        Bird(statusState, birdRectState, gameOverFlagState, onFlapRequestedCallbackState);
    auto pipeManagerNode = PipeManager(statusState, birdRectState, scoreState, gameOverFlagState);

    gameNode->SetChildren({
        birdNode,
        pipeManagerNode,
        Text(
            font,
            textColor,
            [statusState, scoreState]() -> std::string {  // Text provider
                GameStatus status = statusState.get();
                int currentScore = scoreState.get();
                if (status == GameStatus::MainMenu) {
                    return "Flap to Start";
                } else if (status == GameStatus::GameOver) {
                    return "Game Over! Score: " + std::to_string(currentScore) + ".";
                }
                return "";  // Empty if no message for current state
            },
            [statusState]() -> SDL_FPoint {  // Position provider
                GameStatus status = statusState.get();
                if (status == GameStatus::GameOver) {
                    return {WINDOW_WIDTH / 2.0f, WINDOW_HEIGHT / 2.0f - 25.0f};
                }
                // Default position for main menu message
                return {WINDOW_WIDTH / 2.0f, 100.0f};
            },
            [statusState]() -> bool {  // Visibility provider
                GameStatus status = statusState.get();
                return status == GameStatus::MainMenu || status == GameStatus::GameOver;
            }
        ),
        Text(
            font,
            textColor,
            [statusState, scoreState]() -> std::string {  // Text provider
                if (statusState.get() == GameStatus::Playing) {
                    return "Score: " + std::to_string(scoreState.get());
                }
                return "";  // Don't show score text if not playing or game over
            },
            [scorePosition]() -> SDL_FPoint {  // Position provider (fixed for score)
                return scorePosition;
            },
            [statusState]() -> bool {  // Visibility provider
                return statusState.get() == GameStatus::Playing;
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
