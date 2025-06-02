#pragma once

#include <SDL3/SDL_render.h>  // For SDL_Renderer* in useRender

#include <any>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <typeindex>
#include <utility>
#include <vector>
#include <optional> // Required for std::optional
#include <variant>  // Required for std::variant
#include <stdexcept> // Required for std::runtime_error
#include <algorithm>   // For std::remove
#include <deque>       // For managing pipes
#include <functional>  // For std::function
#include <iostream>
#include <random>  // For pipe heights
#include <string>  // For std::string and std::to_string

//------------------------------------------------------------------------------
// Engine Forward Declarations & Type Aliases
//------------------------------------------------------------------------------
struct Node;
using NodePtr = std::shared_ptr<Node>;

//------------------------------------------------------------------------------
// State Slots (Internal Implementation for useState)
//------------------------------------------------------------------------------
struct BaseStateSlot {
    virtual ~BaseStateSlot() = default;
};

template <typename T>
struct TypedStateSlot : BaseStateSlot {
    T value;
    TypedStateSlot(const T& val) : value(val) {
    }
    TypedStateSlot(T&& val) : value(std::move(val)) {
    }
};

//------------------------------------------------------------------------------
// HookData (Internal data structure for a Node's hooks)
//------------------------------------------------------------------------------
struct HookData {
    std::vector<std::shared_ptr<BaseStateSlot>> stateSlots;
    std::vector<std::function<void(double)>> updateEffects;
    std::vector<std::function<void(SDL_Renderer*)>> renderEffects;
    std::vector<std::function<void(SDL_Event*)>> eventEffects; // Add this line
};

//------------------------------------------------------------------------------
// Node (The core of the scene graph)
//------------------------------------------------------------------------------
struct Node {
    Node* parent = nullptr;
    std::vector<NodePtr> children;
    HookData hookData;
    std::map<std::type_index, std::any> providedContextsMap;

    Node(Node* p = nullptr) : parent(p) {
    }

    Node(Node* p, std::initializer_list<NodePtr> kids_init) : parent(p) {
        children.reserve(kids_init.size());
        for (const NodePtr& kid_ptr : kids_init) {
            AddChild(kid_ptr);
        }
    }

    void AddChild(NodePtr child) {
        if (child) {
            children.push_back(child);
            child->parent = this;
        }
    }

    void SetChildren(std::initializer_list<NodePtr> newChildrenList) {
        // Unparent old children
        for (NodePtr& oldChild : children) {
            if (oldChild) {
                oldChild->parent = nullptr;
            }
        }
        children.clear();  // Clear the existing list

        // Add new children from the initializer_list
        children.reserve(newChildrenList.size());
        for (const NodePtr& child_ptr : newChildrenList) {
            if (child_ptr) {  // Check if child_ptr is not null
                children.push_back(child_ptr);
                child_ptr->parent = this;
            }
        }
    }

    template <typename T>
    std::shared_ptr<TypedStateSlot<T>> getStateSlot() {
        for (const auto& baseSlotPtr : hookData.stateSlots) {
            if (auto typedSlotPtr = std::dynamic_pointer_cast<TypedStateSlot<T>>(baseSlotPtr)) {
                return typedSlotPtr;
            }
        }
        return nullptr;
    }

    template <typename T>
    void provideContext(const std::type_index& ctxTypeId, T* value) {
        providedContextsMap[ctxTypeId] = value;
    }

    template <typename T>
    T* getContextValue(const std::type_index& ctxTypeId) {
        Node* currentNode = this;
        while (currentNode) {
            auto it = currentNode->providedContextsMap.find(ctxTypeId);
            if (it != currentNode->providedContextsMap.end()) {
                try {
                    return std::any_cast<T*>(it->second);
                } catch (const std::bad_any_cast& e) {
                    // This could be an SDL_Log call
                    return nullptr;
                }
            }
            currentNode = currentNode->parent;
        }
        return nullptr;
    }
};

//------------------------------------------------------------------------------
// State Hook
//------------------------------------------------------------------------------
template <typename T>
struct State {
    std::shared_ptr<TypedStateSlot<T>> slot;
    State() : slot(nullptr) {
    }
    explicit State(std::shared_ptr<TypedStateSlot<T>> s) : slot(s) {
    }

    T get() const {
        if (!slot) {
            throw std::runtime_error("Accessing uninitialized state via get()");
        }
        return slot->value;
    }
    void set(T newVal) {
        if (!slot) {
            throw std::runtime_error("Accessing uninitialized state via set()");
        }
        slot->value = std::move(newVal);
    }
    T& getRef() {
        if (!slot) {
            throw std::runtime_error("Accessing uninitialized state via getRef()");
        }
        return slot->value;
    }
    bool isValid() const {
        return slot != nullptr;
    }
};

template <typename T>
State<T> useState(Node& node, const T& initialValue) {
    auto typedSlot = std::make_shared<TypedStateSlot<T>>(initialValue);
    node.hookData.stateSlots.push_back(typedSlot);
    return State<T>(typedSlot);
}

//------------------------------------------------------------------------------
// Effect Hooks
//------------------------------------------------------------------------------
inline void useUpdate(Node& node, const std::function<void(double)>& fn) {
    node.hookData.updateEffects.push_back(fn);
}

inline void useRender(Node& node, const std::function<void(SDL_Renderer*)>& fn) {
    node.hookData.renderEffects.push_back(fn);
}

inline void useEvent(Node& node, const std::function<void(SDL_Event*)>& fn) {
    node.hookData.eventEffects.push_back(fn);
}

//------------------------------------------------------------------------------
// Prop System (New)
//------------------------------------------------------------------------------
template <typename T>
using Prop = std::variant<T, State<T>, std::function<T()>>;

template <typename T>
T getVal(const Prop<T>& prop) {
    if (std::holds_alternative<T>(prop)) {
        return std::get<T>(prop);
    } else if (std::holds_alternative<State<T>>(prop)) {
        const auto& state = std::get<State<T>>(prop);
        if (state.isValid()) {
            return state.get();
        }
    } else if (std::holds_alternative<std::function<T()>>(prop)) {
        const auto& func = std::get<std::function<T()>>(prop);
        if (func) { // Check if std::function is not empty (null)
            return func();
        }
    }
    throw std::runtime_error("Invalid Prop<T> state or uninitialized provider");
}

//------------------------------------------------------------------------------
// Context API
//------------------------------------------------------------------------------
template <typename T>
struct Context {
    std::type_index typeId;
    T defaultValue;
    Context(T defVal = T{}) : typeId(typeid(Context<T>)), defaultValue(std::move(defVal)) {
    }
};

// Note: The original ContextProvider and useContext were not fully utilized or implemented.
// The core functionality relies on provideContext and getContextValue directly in the Node.

//------------------------------------------------------------------------------
// Tree Traversal
//------------------------------------------------------------------------------
void updateTree(const NodePtr& node, double dt);
void renderTree(const NodePtr& node, SDL_Renderer* renderer);
void eventTree(const NodePtr& node, SDL_Event* event);
