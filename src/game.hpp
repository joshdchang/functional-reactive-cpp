#pragma once

#include <SDL3/SDL_main.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "engine.hpp"

//------------------------------------------------------------------------------
// Window Constants
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

#include "bird.hpp"
#include "pipes.hpp"
#include "text.hpp"

NodePtr Game(TTF_Font* font);
