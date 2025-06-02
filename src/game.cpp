#include "game.hpp"

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
