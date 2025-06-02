#pragma once
#include "game.hpp"

NodePtr Pipes(
    State<GameStatus> gameStatus,
    Prop<SDL_FRect> birdRect,  // Pipes reads this
    State<int> score
);
