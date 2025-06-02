#include "game.hpp"

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
    auto node = createNode();
    auto pipeData = node->state(
        PipeData{
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

    node->render([pipeData](SDL_Renderer* renderer) {
        PipeData data = pipeData.get();
        SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);  // Green pipes
        SDL_RenderFillRect(renderer, &data.topRect);
        SDL_RenderFillRect(renderer, &data.bottomRect);
    });

    return node;
}

//------------------------------------------------------------------------------
// Pipe Manager Component
//------------------------------------------------------------------------------
NodePtr Pipes(
    State<GameStatus> gameStatus,
    Prop<SDL_FRect> birdRect,  // Pipes reads this
    State<int> score
) {
    auto node = createNode();
    auto activePipes = node->state(std::deque<NodePtr>{});
    auto spawnTimer = PIPE_SPAWN_INTERVAL;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(0, MAX_PIPE_HEIGHT_OFFSET);

    node->effect(
        [activePipes, spawnTimer, node_ptr = node.get(), gameStatus]() mutable {
            GameStatus currentStatus = gameStatus.get();
            if (currentStatus != GameStatus::Playing) {
                // Clear pipes and reset spawn timer if not playing
                auto& pipes = activePipes.getRef();
                if (!pipes.empty()) {
                    pipes.clear();
                    node_ptr->SetChildren({});  // Clear children from the node
                }
                spawnTimer = PIPE_SPAWN_INTERVAL;  // Reset spawn timer
            }
        },
        gameStatus  // Dependency for the effect
    );

    node->update(
        [activePipes, spawnTimer, node_ptr = node.get(), distrib, gen, gameStatus, birdRect, score](
            double dt
        ) mutable {
            GameStatus currentStatus = gameStatus.get();

            if (currentStatus != GameStatus::Playing) {
                return;  // Only run pipe logic if the game is in Playing state
            }

            // If playing, proceed with pipe logic
            spawnTimer -= static_cast<float>(dt);

            if (spawnTimer <= 0) {
                float topPipeOpeningY = static_cast<float>(MIN_PIPE_HEIGHT + distrib(gen));
                NodePtr newPipe = PipePair(WINDOW_WIDTH + PIPE_WIDTH / 2, topPipeOpeningY);
                activePipes.getRef().push_back(newPipe);
                node_ptr->AddChild(newPipe);  // Add new pipe as a child of Pipes
                spawnTimer = PIPE_SPAWN_INTERVAL;
            }

            auto& pipes = activePipes.getRef();

            auto currentBirdRect = val(birdRect);

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
                    // gameStatus.set will trigger the effect if status changes
                    gameStatus.set(GameStatus::GameOver);
                    // No need to check currentStatus == Playing again, as we returned if not
                    // playing
                }

                // Score Check
                if (!currentData.scored && currentData.xPos < currentBirdRect.x) {
                    currentData.scored = true;
                    score.set(score.get() + 1);
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
