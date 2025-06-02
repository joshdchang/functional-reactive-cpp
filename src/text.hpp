#pragma once
#include "game.hpp"

NodePtr Text(
    Prop<TTF_Font*> font,        // Font to use for rendering
    Prop<SDL_Color> color,       // Color of the text
    Prop<std::string> text,      // Flexible text property
    Prop<SDL_FPoint> position,   // Flexible position property
    Prop<bool> isVisible = true  // Flexible visibility, defaults to true
);
