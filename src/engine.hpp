#pragma once

#include <SDL3/SDL_render.h>

#include <algorithm>
#include <any>
#include <deque>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeindex>
#include <utility>
#include <variant>
#include <vector>

//------------------------------------------------------------------------------
// Engine Forward Declarations & Type Aliases
//------------------------------------------------------------------------------
struct Node;
using NodePtr = std::shared_ptr<Node>;

template <typename T>
struct State;

template <typename Test, template <typename...> class Ref>
struct is_specialization : std::false_type {};

template <template <typename...> class Ref, typename... Args>
struct is_specialization<Ref<Args...>, Ref> : std::true_type {};

template <typename T>
using Prop = std::variant<T, State<T>, std::function<T()>>;

//------------------------------------------------------------------------------
// State Slots (Internal Implementation for state)
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
            return false;
        }
        T currentValue = state.get();
        if (!lastValue.has_value()) {
            return true;
        }
        if constexpr (std::is_same_v<T, std::string> || std::is_arithmetic_v<T> ||
                      std::is_enum_v<T>) {
            return currentValue != *lastValue;
        } else if constexpr (is_specialization<T, std::vector>::value) {
            return currentValue.size() != lastValue->size();
        } else if constexpr (requires(const T& a, const T& b) { a == b; }) {
            return !(currentValue == *lastValue);
        } else {
            return true;
        }
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

    EffectHook(std::function<void()> effectFn) : _effectFn(std::move(effectFn)) {
    }
    EffectHook(EffectHook&& other) noexcept = default;
    EffectHook& operator=(EffectHook&& other) noexcept = default;
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

template <typename FirstDep, typename... RestDeps>
void addDependenciesToEffectHook(EffectHook& eh, const FirstDep& first, const RestDeps&... rest) {
    if constexpr (is_specialization<std::decay_t<FirstDep>, State>::value) {
        eh.addDependencyInternal(first);
    } else if constexpr (is_specialization<std::decay_t<FirstDep>, Prop>::value) {
        std::visit(
            [&eh](auto&& arg) {
                using ArgType = std::decay_t<decltype(arg)>;
                if constexpr (is_specialization<ArgType, State>::value) {
                    eh.addDependencyInternal(arg);
                }
            },
            first
        );
    }
    if constexpr (sizeof...(rest) > 0) {
        addDependenciesToEffectHook(eh, rest...);
    }
}

inline void addDependenciesToEffectHook(EffectHook&) {
}

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
        if (!childToRemove) {
            return;
        }
        children.erase(
            std::remove_if(
                children.begin(),
                children.end(),
                [&](const NodePtr& child) {
                    if (child == childToRemove) {
                        child->parent = nullptr;
                        return true;
                    }
                    return false;
                }
            ),
            children.end()
        );
    }

    void SetChildren(std::initializer_list<NodePtr> newChildrenList) {
        for (NodePtr& oldChild : children) {
            if (oldChild) {
                oldChild->parent = nullptr;
            }
        }
        children.clear();
        children.reserve(newChildrenList.size());
        for (const NodePtr& child_ptr : newChildrenList) {
            if (child_ptr) {
                children.push_back(child_ptr);
                child_ptr->parent = this;
            }
        }
    }

    void SetChildren(const std::vector<NodePtr>& newChildren) {
        for (NodePtr& oldChild : children) {
            if (oldChild) {
                oldChild->parent = nullptr;
            }
        }
        children.clear();
        children.reserve(newChildren.size());
        for (const NodePtr& child_ptr : newChildren) {
            if (child_ptr) {
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

    //--------------------------------------------------------------------------
    // Hook Methods
    //--------------------------------------------------------------------------
    template <typename T>
    State<T> state(const T& initialValue) {
        auto typedSlot = std::make_shared<TypedStateSlot<T>>(initialValue);
        this->hookData.stateSlots.push_back(typedSlot);
        return State<T>(typedSlot);
    }

    void update(const std::function<void(double)>& fn) {
        this->hookData.updateEffects.push_back(fn);
    }

    void render(const std::function<void(SDL_Renderer*)>& fn) {
        this->hookData.renderEffects.push_back(fn);
    }

    void event(const std::function<void(SDL_Event*)>& fn) {
        this->hookData.eventEffects.push_back(fn);
    }

    template <typename... DepTypes>
    void effect(std::function<void()> effectFn, const DepTypes&... deps) {
        EffectHook eh(std::move(effectFn));
        addDependenciesToEffectHook(eh, deps...);
        this->hookData.effects.push_back(std::move(eh));
    }

    template <typename F, typename... DepTypes>
    auto derived(F&& computeFn, const DepTypes&... deps) {
        using R = decltype(computeFn());
        R initialValue = computeFn();
        State<R> computedState = this->state<R>(initialValue);
        this->effect(
            [computedState, computeFn = std::forward<F>(computeFn)]() mutable {
                R newValue = computeFn();
                computedState.set(newValue);
            },
            deps...
        );
        return computedState;
    }
};

inline NodePtr createNode(Node* parent = nullptr) {
    return std::make_shared<Node>(parent);
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
    auto node = std::make_shared<Node>();
    node->effect(
        [node_wptr = std::weak_ptr<Node>(node), condition, childComponent]() mutable {
            auto node_sptr = node_wptr.lock();
            if (!node_sptr || !childComponent) {
                return;
            }
            bool isCurrentlyChild = false;
            for (const auto& c : node_sptr->children) {
                if (c == childComponent) {
                    isCurrentlyChild = true;
                    break;
                }
            }
            if (condition.get()) {
                if (!isCurrentlyChild) {
                    node_sptr->AddChild(childComponent);
                }
            } else {
                if (isCurrentlyChild) {
                    node_sptr->RemoveChild(childComponent);
                }
            }
        },
        condition
    );
    return node;
}

//------------------------------------------------------------------------------
// Fragment Component (Groups children without adding behavior)
//------------------------------------------------------------------------------
inline NodePtr Fragment(std::initializer_list<NodePtr> children_init) {
    auto node = std::make_shared<Node>();
    for (const NodePtr& child_ptr : children_init) {
        if (child_ptr) {
            node->AddChild(child_ptr);
        }
    }
    return node;
}

//------------------------------------------------------------------------------
// Tree Traversal
//------------------------------------------------------------------------------
inline void updateTree(const NodePtr& node, double dt) {
    if (!node) {
        return;
    }
    // Run effects for the current node
    for (auto& fn : node->hookData.updateEffects) {
        fn(dt);
    }
    // Run useEffect hooks for the current node
    for (auto& effectHook : node->hookData.effects) {
        effectHook.runIfChanged();
    }
    // Recursively update children
    for (auto& child : node->children) {
        updateTree(child, dt);
    }
}

inline void renderTree(const NodePtr& node, SDL_Renderer* renderer) {
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

inline void eventTree(const NodePtr& node, SDL_Event* event) {
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
