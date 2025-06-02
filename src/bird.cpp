#include "bird.hpp"

//------------------------------------------------------------------------------
// Bird Component
//------------------------------------------------------------------------------
NodePtr Bird(State<GameStatus> gameStatus, State<SDL_FRect> birdRect) {
    auto node = createNode();

    auto yPosState = node->state(WINDOW_HEIGHT / 2.0f);
    auto yVelState = node->state(0.0f);
    auto rotationState = node->state(0.0f);

    node->event([yVelState, gameStatus](SDL_Event* e) mutable {
        if (e->type == SDL_EVENT_KEY_DOWN) {
            if (e->key.scancode == SDL_SCANCODE_SPACE || e->key.scancode == SDL_SCANCODE_UP) {
                GameStatus currentStatus = gameStatus.get();
                if (currentStatus == GameStatus::Playing) {
                    yVelState.set(FLAP_VELOCITY);
                }
            }
        }
    });

    node->effect(
        [yPosState, yVelState, rotationState, gameStatus, birdRect]() mutable {
            GameStatus currentStatus = gameStatus.get();
            if (currentStatus == GameStatus::MainMenu || currentStatus == GameStatus::GameOver) {
                // Reset bird position and physics for MainMenu or GameOver
                float initialYPos = WINDOW_HEIGHT / 2.0f;
                yPosState.set(initialYPos);
                yVelState.set(0.0f);
                rotationState.set(0.0f);
                birdRect.set({
                    BIRD_X_POSITION - BIRD_WIDTH / 2,
                    initialYPos - BIRD_HEIGHT / 2,
                    BIRD_WIDTH,
                    BIRD_HEIGHT,
                });
            }
        },
        gameStatus
    );

    node->update([yPosState, yVelState, rotationState, gameStatus, birdRect](double dt) mutable {
        GameStatus currentStatus = gameStatus.get();

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

            birdRect.set(
                {// Update the shared birdRect state
                 BIRD_X_POSITION - BIRD_WIDTH / 2,
                 yPos - BIRD_HEIGHT / 2,
                 BIRD_WIDTH,
                 BIRD_HEIGHT
                }
            );

            // Check bounds
            if (yPos + BIRD_HEIGHT / 2 > WINDOW_HEIGHT || yPos - BIRD_HEIGHT / 2 < 0) {
                gameStatus.set(GameStatus::GameOver);
            }
        }
    });

    node->render([yPosState, rotationState](SDL_Renderer* renderer) {
        SDL_FRect bird_render_rect = {
            BIRD_X_POSITION - BIRD_WIDTH / 2,
            yPosState.get() - BIRD_HEIGHT / 2,
            BIRD_WIDTH,
            BIRD_HEIGHT
        };
        SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255);  // Yellow
        SDL_RenderFillRect(renderer, &bird_render_rect);
    });

    return node;
}
