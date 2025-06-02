#include "engine.hpp"

//------------------------------------------------------------------------------
// Tree Traversal Implementations
//------------------------------------------------------------------------------

void updateTree(const NodePtr& node, double dt) {
    if (!node) {
        return;
    }
    // Run effects for the current node
    for (auto& fn : node->hookData.updateEffects) {
        fn(dt);
    }
    // Recursively update children
    for (auto& child : node->children) {
        updateTree(child, dt);
    }
}

void renderTree(const NodePtr& node, SDL_Renderer* renderer) {
    if (!node) {
        return;
    }
    // Run render effects for the current node
    for (auto& fn : node->hookData.renderEffects) {
        fn(renderer);
    }
    // Recursively render children
    for (auto& child : node->children) {
        renderTree(child, renderer);
    }
}

void eventTree(const NodePtr& node, SDL_Event* event) {
    if (!node || !event) {
        return;
    }
    // Run event effects for the current node
    for (auto& fn : node->hookData.eventEffects) {
        fn(event);
    }
    // Recursively process events for children
    for (auto& child : node->children) {
        eventTree(child, event);
    }
}
