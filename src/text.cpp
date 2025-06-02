#include "text.hpp"

//------------------------------------------------------------------------------
// Text Component
//------------------------------------------------------------------------------
NodePtr Text(
    Prop<TTF_Font*> font,       // Font to use for rendering
    Prop<SDL_Color> color,      // Color of the text
    Prop<std::string> text,     // Flexible text property
    Prop<SDL_FPoint> position,  // Flexible position property
    Prop<bool> isVisible
) {
    auto node = createNode();

    node->render([font, color, text, position, isVisible](SDL_Renderer* renderer) {
        if (!val(isVisible)) {
            return;
        }

        if (val(text).empty()) {
            return;
        }

        auto currentPosition = val(position);

        SDL_Surface* surface =
            TTF_RenderText_Solid(val(font), val(text).c_str(), val(text).length(), val(color));
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
