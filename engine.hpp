#pragma once

#include <SDL3/SDL_render.h>  // For SDL_Renderer* in useRender

#include <algorithm>  // For std::remove
#include <any>
#include <deque>  // For managing pipes
#include <functional>
#include <functional>  // For std::function
#include <initializer_list>
#include <iostream>
#include <map>
#include <memory>
#include <optional>   // Required for std::optional
#include <random>     // For pipe heights
#include <stdexcept>  // Required for std::runtime_error
#include <string>
#include <string>  // For std::string and std::to_string
#include <typeindex>
#include <type_traits> // For type trait utilities
#include <utility>
#include <variant>  // Required for std::variant
#include <vector>

//------------------------------------------------------------------------------
// Engine Forward Declarations & Type Aliases
//------------------------------------------------------------------------------
struct Node;
using NodePtr = std::shared_ptr<Node>;

// Forward declarations
template <typename T>
struct State;

// Type trait to check if a type is a specialization of a template
template<typename Test, template<typename...> class Ref>
struct is_specialization : std::false_type {};

template<template<typename...> class Ref, typename... Args>
struct is_specialization<Ref<Args...>, Ref> : std::true_type {};

// Define Prop early since we'll need it for dependencies
template <typename T>
using Prop = std::variant<T, State<T>, std::function<T()>>;

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

namespace Detail {
// Interface for type-erased dependency checking
struct IDependency {
    virtual ~IDependency() = default;
    virtual bool hasChanged() = 0;
    virtual void updateLastValue() = 0;
};

template <typename T>
struct Dependency : IDependency {
    State<T> state;
    std::optional<T> lastValue;

    Dependency(const State<T>& s) : state(s) {
    }

    bool hasChanged() override {
        if (!state.isValid()) {
            return false;  // Or throw, but for safety, treat invalid state as unchanged
        }
        T currentValue = state.get();
        if (!lastValue.has_value()) {  // First check after initialization
            return true;
        }
        // Requires T to have operator==
        if (!(currentValue == *lastValue)) {
            return true;
        }
        return false;
    }

    void updateLastValue() override {
        if (state.isValid()) {
            lastValue = state.get();
        } else {
            lastValue.reset();
        }
    }
};
}  // namespace Detail

struct EffectHook {
    std::function<void()> _effectFn;
    std::vector<std::unique_ptr<Detail::IDependency>> _dependencies;
    bool _isFirstRun = true;

    EffectHook(std::function<void()> effectFn) : _effectFn(std::move(effectFn)) {}
    EffectHook(EffectHook&& other) noexcept = default;
    EffectHook& operator=(EffectHook&& other) noexcept = default;

    // Disable copy constructor and assignment operator
    EffectHook(const EffectHook&) = delete;
    EffectHook& operator=(const EffectHook&) = delete;

    template <typename T>
    void addDependencyInternal(const State<T>& dep) {
        _dependencies.push_back(std::make_unique<Detail::Dependency<T>>(dep));
    }

    void runIfChanged() {
        bool dependenciesChanged = false;
        if (_isFirstRun) {
            dependenciesChanged = true;
        } else {
            for (const auto& dep : _dependencies) {
                if (dep->hasChanged()) {
                    dependenciesChanged = true;
                    break;
                }
            }
        }

        if (dependenciesChanged) {
            _effectFn();
            for (auto& dep : _dependencies) {
                dep->updateLastValue();
            }
            _isFirstRun = false;
        }
    }
};

//------------------------------------------------------------------------------
// HookData (Internal data structure for a Node's hooks)
//------------------------------------------------------------------------------
struct HookData {
    std::vector<std::shared_ptr<BaseStateSlot>> stateSlots;
    std::vector<std::function<void(double)>> updateEffects;
    std::vector<std::function<void(SDL_Renderer*)>> renderEffects;
    std::vector<std::function<void(SDL_Event*)>> eventEffects;
    std::vector<EffectHook> effects;
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

    void RemoveChild(const NodePtr& childToRemove) {
        if (!childToRemove) return;
        children.erase(
            std::remove_if(children.begin(), children.end(),
                         [&](const NodePtr& child) {
                             if (child == childToRemove) {
                                 child->parent = nullptr; // Unparent
                                 return true;
                             }
                             return false;
                         }),
            children.end()
        );
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
// useEffect - handles dependencies and runs effects
//------------------------------------------------------------------------------

// Base case for recursive dependency addition
inline void addDependenciesToEffectHook(EffectHook& /*eh*/) {
    // Base case: no more dependencies to add
}

// Helper for useEffect variadic template with support for Props
template <typename FirstDep, typename... RestDeps>
void addDependenciesToEffectHook(EffectHook& eh, const FirstDep& first, const RestDeps&... rest) {
    // If FirstDep is State<T>
    if constexpr (is_specialization<std::decay_t<FirstDep>, State>::value) {
        eh.addDependencyInternal(first);
    }
    // If FirstDep is Prop<T>
    else if constexpr (is_specialization<std::decay_t<FirstDep>, Prop>::value) {
        std::visit([&eh](auto&& arg) {
            using ArgType = std::decay_t<decltype(arg)>;
            if constexpr (is_specialization<ArgType, State>::value) {
                eh.addDependencyInternal(arg);
            }
            // Other variants in Prop<T> are not dependencies
        }, first);
    }
    // For other types, they are not tracked as dependencies

    // Process the rest of the dependencies
    if constexpr (sizeof...(rest) > 0) {
        addDependenciesToEffectHook(eh, rest...);
    }
}

// unified useEffect - handles both with and without dependencies
template <typename... DepTypes>
void useEffect(Node& node, std::function<void()> effectFn, const DepTypes&... deps) {
    EffectHook eh(std::move(effectFn));
    addDependenciesToEffectHook(eh, deps...);  // This works for zero deps too
    node.hookData.effects.push_back(std::move(eh));
}

//------------------------------------------------------------------------------
// Computed Hook (Derived State)
//------------------------------------------------------------------------------
template <typename R, typename... DepTypes>
State<R> useComputed(Node& node, std::function<R()> computeFn, const DepTypes&... deps) {
    // 1. Compute the initial value for the state.
    R initialValue = computeFn();

    // 2. Create a state to hold the computed value.
    //    This state will be updated by the effect.
    State<R> computedState = useState<R>(node, initialValue);

    // 3. Use an effect to re-run the computation and update the state
    //    whenever any of the specified dependencies change.
    //    The effect also runs once initially.
    useEffect(
        node,
        [computedState, computeFn]() mutable {
            // Re-compute the value
            R newValue = computeFn();
            // Update the state with the new computed value
            computedState.set(newValue);
        },
        deps... // Dependencies for the effect
    );

    // 4. Return the state that holds the computed value.
    return computedState;
}

//------------------------------------------------------------------------------
// Generic Props
//------------------------------------------------------------------------------
template <typename T>
inline T val(const Prop<T>& prop) {
    if (std::holds_alternative<T>(prop)) {
        return std::get<T>(prop);
    } else if (std::holds_alternative<State<T>>(prop)) {
        const auto& state = std::get<State<T>>(prop);
        if (state.isValid()) {
            return state.get();
        }
    } else if (std::holds_alternative<std::function<T()>>(prop)) {
        const auto& func = std::get<std::function<T()>>(prop);
        if (func) {
            return func();
        }
    }
    throw std::runtime_error("Invalid Prop<T> state or uninitialized provider");
}

//------------------------------------------------------------------------------
// Conditional Node
//------------------------------------------------------------------------------
inline NodePtr Conditional(State<bool> condition, NodePtr childComponent) {
    auto node = std::make_shared<Node>(); // This is the Conditional node itself

    // This effect hook manages the presence of childComponent in Conditional's children list
    useEffect(
        *node,
        [node_wptr = std::weak_ptr<Node>(node), condition, childComponent]() mutable {
            auto node_sptr = node_wptr.lock();
            if (!node_sptr || !childComponent) return; // Node itself or childComponent is no longer valid

            bool isCurrentlyChild = false;
            for (const auto& c : node_sptr->children) {
                if (c == childComponent) {
                    isCurrentlyChild = true;
                    break;
                }
            }

            if (condition.get()) { // If condition is true
                if (!isCurrentlyChild) {
                    node_sptr->AddChild(childComponent);
                }
            } else { // If condition is false
                if (isCurrentlyChild) {
                    node_sptr->RemoveChild(childComponent);
                }
            }
        },
        condition // Dependency: run this effect when condition changes (and on first run)
    );

    return node;
}

//------------------------------------------------------------------------------
// Fragment Component (Groups children without adding behavior)
//------------------------------------------------------------------------------
inline NodePtr Fragment(std::initializer_list<NodePtr> children_init) {
    auto node = std::make_shared<Node>(); // The Fragment node itself
    // The parent of this Fragment node will be set when it's added as a child
    // to another node (e.g., via parent->AddChild(fragmentNode) or parent->SetChildren({...})).

    for (const NodePtr& child_ptr : children_init) {
        if (child_ptr) { // Ensure child_ptr is not null before adding
            node->AddChild(child_ptr); // AddChild sets child_ptr->parent = the fragment node
        }
    }
    return node;
}

//------------------------------------------------------------------------------
// Tree Traversal
//------------------------------------------------------------------------------
void updateTree(const NodePtr& node, double dt);
void renderTree(const NodePtr& node, SDL_Renderer* renderer);
void eventTree(const NodePtr& node, SDL_Event* event);
