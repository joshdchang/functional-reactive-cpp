#include "game.hpp"

NodePtr Game(TTF_Font* font) {
    auto node = createNode(nullptr);

    auto status = node->state(GameStatus::MainMenu);
    auto score = node->state(0);
    auto birdRect = node->state(
        SDL_FRect{
            BIRD_X_POSITION - BIRD_WIDTH / 2,
            WINDOW_HEIGHT / 2.0f - BIRD_HEIGHT / 2,
            BIRD_WIDTH,
            BIRD_HEIGHT,
        }
    );

    node->event([status, score](SDL_Event* e) mutable {
        if (e->type == SDL_EVENT_KEY_DOWN) {
            if (e->key.scancode == SDL_SCANCODE_SPACE || e->key.scancode == SDL_SCANCODE_UP) {
                GameStatus currentStatus = status.get();
                if (currentStatus == GameStatus::MainMenu) {
                    status.set(GameStatus::Playing);
                    score.set(0);
                } else if (currentStatus == GameStatus::GameOver) {
                    status.set(GameStatus::MainMenu);
                    score.set(0);
                }
            }
        }
    });

    SDL_Color black = {0, 0, 0, 255};

    node->SetChildren({
        Conditional(
            node->derived([status]() { return status.get() != GameStatus::GameOver; }, status),
            Fragment({
                Pipes(status, birdRect, score),
                Bird(status, birdRect),
            })
        ),
        Conditional(
            node->derived([status]() { return status.get() == GameStatus::MainMenu; }, status),
            Text(font, black, "Press Space to Flap", SDL_FPoint{WINDOW_WIDTH / 2.0f, 100.0f})
        ),
        Conditional(
            node->derived([status]() { return status.get() == GameStatus::GameOver; }, status),
            Fragment({
                Text(font, black, "Game Over", SDL_FPoint{WINDOW_WIDTH / 2.0f, 100.0f}),
                Text(
                    font,
                    black,
                    "Press Space to Restart",
                    SDL_FPoint{WINDOW_WIDTH / 2.0f, 150.0f}
                ),
            })
        ),
        Conditional(
            node->derived([status]() { return status.get() == GameStatus::Playing; }, status),
            Text(
                font,
                black,
                node->derived([score]() { return "Score: " + std::to_string(score.get()); }, score),
                SDL_FPoint{WINDOW_WIDTH / 2.0f, 50.0f}
            )
        ),
    });

    return node;
}
