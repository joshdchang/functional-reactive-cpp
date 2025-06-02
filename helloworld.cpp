#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <functional>
#include <initializer_list>
#include <iostream>
#include <map>
#include <memory>
#include <typeindex>  // For identifying types in getStateSlot and context
#include <utility>
#include <vector>

constexpr int SDL_WINDOW_WIDTH = 640;
constexpr int SDL_WINDOW_HEIGHT = 480;

//------------------------------------------------------------------------------
// Forward declare Node and alias NodePtr
//------------------------------------------------------------------------------
struct Node;
using NodePtr = std::shared_ptr<Node>;

//------------------------------------------------------------------------------
// Base and Typed State Slots for type-safe state management
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
// Hook data: holds state slots and callbacks
//------------------------------------------------------------------------------
struct HookData {
    std::vector<std::shared_ptr<BaseStateSlot>> stateSlots;
    std::vector<std::function<void(double)>> updateEffects;
    std::vector<std::function<void(SDL_Renderer*)>> renderEffects;
    std::vector<std::function<void(NodePtr)>> collisionCallbacks;
};

//------------------------------------------------------------------------------
// Node: holds children, hook data, and parent pointer
//------------------------------------------------------------------------------
struct Node {
    Node* parent = nullptr;
    std::vector<NodePtr> children;
    HookData hookData;

    Node(Node* p = nullptr) : parent(p) {
    }  // Default constructor

    // Constructor for initializing with children, sets their parent pointers.
    Node(Node* p, std::initializer_list<NodePtr> kids_init) : parent(p) {
        children.reserve(kids_init.size());
        for (const NodePtr& kid_ptr : kids_init) {
            children.push_back(kid_ptr);
            if (kid_ptr) {
                kid_ptr->parent = this;
            }
        }
    }

    void AddChild(NodePtr child) {
        if (child) {
            children.push_back(child);
            child->parent = this;
        }
    }

    template <typename T>
    std::shared_ptr<TypedStateSlot<T>> getStateSlot() {
        for (const auto& baseSlotPtr : hookData.stateSlots) {
            if (auto typedSlotPtr = std::dynamic_pointer_cast<TypedStateSlot<T>>(baseSlotPtr)) {
                return typedSlotPtr;  // Return the first one found
            }
        }
        return nullptr;  // Not found
    }
};

// Factory for group nodes, ensuring parent pointers are set.
static NodePtr MakeGroup(Node* parent, std::initializer_list<NodePtr> kids) {
    auto group = std::make_shared<Node>(parent);  // Group's parent is 'parent'
    for (const auto& kid : kids) {
        group->AddChild(kid);  // AddChild sets kid's parent to 'group'
    }
    return group;
}
// Overload for root-level groups
static NodePtr MakeGroup(std::initializer_list<NodePtr> kids) {
    return MakeGroup(nullptr, kids);
}

//------------------------------------------------------------------------------
// useState hook: stores a value of type T
//------------------------------------------------------------------------------
template <typename T>
struct State {
    std::shared_ptr<TypedStateSlot<T>> slot;

    // Default constructor for uninitialized state
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
// useUpdate hook: register a callback(double dt)
//------------------------------------------------------------------------------
void useUpdate(Node& node, const std::function<void(double)>& fn) {
    node.hookData.updateEffects.push_back(fn);
}

//------------------------------------------------------------------------------
// useRender hook: register a callback(SDL_Renderer*)
//------------------------------------------------------------------------------
void useRender(Node& node, const std::function<void(SDL_Renderer*)>& fn) {
    node.hookData.renderEffects.push_back(fn);
}

//------------------------------------------------------------------------------
// useCollision hook: register a callback(NodePtr other)
//------------------------------------------------------------------------------
void useCollision(Node& node, const std::function<void(NodePtr)>& fn) {
    node.hookData.collisionCallbacks.push_back(fn);
}

//------------------------------------------------------------------------------
// updateTree: depth-first update
//------------------------------------------------------------------------------
void updateTree(const NodePtr& node, double dt) {
    if (!node) {
        return;
    }
    for (auto& fn : node->hookData.updateEffects) {
        fn(dt);
    }
    for (auto& child : node->children) {
        updateTree(child, dt);
    }
}

//------------------------------------------------------------------------------
// renderTree: depth-first render
//------------------------------------------------------------------------------
void renderTree(const NodePtr& node, SDL_Renderer* renderer) {
    if (!node) {
        return;
    }
    for (auto& fn : node->hookData.renderEffects) {
        fn(renderer);
    }
    for (auto& child : node->children) {
        renderTree(child, renderer);
    }
}

//------------------------------------------------------------------------------
// MotionData struct and useMotion hook
//------------------------------------------------------------------------------
struct MotionData {
    std::pair<double, double> pos;
    std::pair<double, double> vel;
    double radius;
};

State<MotionData> useMotion(Node& node, double x0, double y0, double vx0, double vy0, double r) {
    State<MotionData> motionState = useState<MotionData>(node, {{x0, y0}, {vx0, vy0}, r});

    useUpdate(
        node,
        [motionState](double dt) mutable {  // mutable to allow calling set on motionState
            MotionData data = motionState.get();
            // move
            data.pos.first += data.vel.first * dt;
            data.pos.second += data.vel.second * dt;
            // gravity
            data.vel.second += 500.0 * dt;
            // bounce X
            if (data.pos.first - data.radius < 0.0) {
                data.pos.first = data.radius;
                data.vel.first = -data.vel.first;
            } else if (data.pos.first + data.radius > SDL_WINDOW_WIDTH) {
                data.pos.first = SDL_WINDOW_WIDTH - data.radius;
                data.vel.first = -data.vel.first;
            }
            // bounce Y
            if (data.pos.second - data.radius < 0.0) {
                data.pos.second = data.radius;
                data.vel.second = -data.vel.second;
            } else if (data.pos.second + data.radius > SDL_WINDOW_HEIGHT) {
                data.pos.second = SDL_WINDOW_HEIGHT - data.radius;
                data.vel.second = -data.vel.second;
            }
            motionState.set(data);
        }
    );
    return motionState;
}

//------------------------------------------------------------------------------
// Player: red square, motion + collision response + render
//------------------------------------------------------------------------------
static NodePtr Player() {
    auto node = std::make_shared<Node>();  // Parent set when added to Collision group

    State<MotionData> motionState = useMotion(*node, 100.0, 100.0, 50.0, 0.0, 25.0);

    useUpdate(*node, [motionState](double dt) mutable {
        const Uint8* kb = (const Uint8*)SDL_GetKeyboardState(NULL);
        MotionData data = motionState.get();
        double thrust = 800.0;
        if (kb[SDL_SCANCODE_LEFT]) {
            data.vel.first -= thrust * dt;
        }
        if (kb[SDL_SCANCODE_RIGHT]) {
            data.vel.first += thrust * dt;
        }
        if (kb[SDL_SCANCODE_UP]) {
            data.vel.second -= thrust * dt;
        }
        if (kb[SDL_SCANCODE_DOWN]) {
            data.vel.second += thrust * dt;
        }
        motionState.set(data);
    });

    useCollision(*node, [motionState](NodePtr /*other*/) mutable {
        MotionData data = motionState.get();
        data.vel.first = -data.vel.first;
        data.vel.second = -data.vel.second;
        motionState.set(data);
    });

    useRender(*node, [motionState](SDL_Renderer* renderer) {
        MotionData data = motionState.get();
        SDL_FRect rect = {
            float(data.pos.first - data.radius),
            float(data.pos.second - data.radius),
            float(data.radius * 2.0),
            float(data.radius * 2.0)
        };
        SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
        SDL_RenderFillRect(renderer, &rect);
    });

    return node;
}

//------------------------------------------------------------------------------
// Ball: blue circle, motion + collision response + render
//------------------------------------------------------------------------------
static NodePtr Ball(double x0, double y0, double vx0, double vy0, double size) {
    auto node = std::make_shared<Node>();  // Parent set when added to Collision group

    State<MotionData> motionState = useMotion(*node, x0, y0, vx0, vy0, size);

    useCollision(*node, [motionState](NodePtr /*other*/) mutable {
        MotionData data = motionState.get();
        data.vel.first = -data.vel.first;
        data.vel.second = -data.vel.second;
        motionState.set(data);
    });

    useRender(*node, [motionState](SDL_Renderer* renderer) {
        MotionData data = motionState.get();
        SDL_SetRenderDrawColor(renderer, 0, 0, 255, 255);
        for (double dy = -data.radius; dy <= data.radius; ++dy) {
            for (double dx = -data.radius; dx <= data.radius; ++dx) {
                if (dx * dx + dy * dy <= data.radius * data.radius) {
                    SDL_RenderPoint(renderer, data.pos.first + dx, data.pos.second + dy);
                }
            }
        }
    });
    return node;
}

//------------------------------------------------------------------------------
// Collision container: checks bounding-circle collisions among all children
//------------------------------------------------------------------------------
static NodePtr CreateCollisionGroup(std::initializer_list<NodePtr> kids_for_collision) {
    // The collision group node itself doesn't have a visual parent in this simple structure,
    // but its children (Player, Ball) will have it as their parent.
    auto col_node = MakeGroup(nullptr, kids_for_collision);  // children's parent set to col_node

    useUpdate(*col_node, [node_ptr = col_node.get()](double /*dt*/) {
        auto& arr = node_ptr->children;
        int n = (int)arr.size();
        for (int i = 0; i < n; ++i) {
            auto& a_node = arr[i];
            auto motionStateSlotA = a_node->getStateSlot<MotionData>();
            if (!motionStateSlotA) {
                continue;
            }
            const MotionData& dataA = motionStateSlotA->value;

            for (int j = i + 1; j < n; ++j) {
                auto& b_node = arr[j];
                auto motionStateSlotB = b_node->getStateSlot<MotionData>();
                if (!motionStateSlotB) {
                    continue;
                }
                const MotionData& dataB = motionStateSlotB->value;

                double dx = dataA.pos.first - dataB.pos.first;
                double dy = dataA.pos.second - dataB.pos.second;
                double dist2 = dx * dx + dy * dy;
                double sumR = dataA.radius + dataB.radius;
                if (dist2 <= sumR * sumR) {
                    for (auto& fn : a_node->hookData.collisionCallbacks) {
                        fn(b_node);
                    }
                    for (auto& fn : b_node->hookData.collisionCallbacks) {
                        fn(a_node);
                    }
                }
            }
        }
    });
    return col_node;
}

// Scene: groups children (root of the renderable/updatable tree)
static NodePtr Scene(std::initializer_list<NodePtr> kids) {
    return MakeGroup(nullptr, kids);  // Parent is nullptr for the root scene node
}

// App: compose the scene
static NodePtr App() {
    return Scene({
        CreateCollisionGroup({
            Player(),
            Ball(300.0, 100.0, 100.0, 0.0, 25.0),
            Ball(400.0, 200.0, -100.0, 0.0, 25.0),
            Ball(500.0, 300.0, 50.0, 0.0, 25.0),
            Ball(200.0, 400.0, -50.0, 0.0, 25.0),
        }),
    });
}

//------------------------------------------------------------------------------
// SDL Boilerplate
//------------------------------------------------------------------------------
struct AppState {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    NodePtr root;
    Uint64 lastTime = 0;
};

SDL_AppResult SDL_AppInit(void** appstate, int, char*[]) {
    if (!SDL_SetAppMetadata("demo_robust", "1.0", "com.example.DemoRobust")) {
        return SDL_APP_FAILURE;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    auto* as = new AppState();
    *appstate = as;
    if (!SDL_CreateWindowAndRenderer(
            "demo_robust",
            SDL_WINDOW_WIDTH,
            SDL_WINDOW_HEIGHT,
            0,
            &as->window,
            &as->renderer
        )) {
        return SDL_APP_FAILURE;
    }
    as->root = App();  // App() creates the node hierarchy
    as->lastTime = SDL_GetPerformanceCounter();
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    auto* as = (AppState*)appstate;
    Uint64 now = SDL_GetPerformanceCounter();
    double dt = (now - as->lastTime) / (double)SDL_GetPerformanceFrequency();
    if (dt > 0.25) {
        dt = 0.25;  // Cap dt
    }
    as->lastTime = now;

    SDL_SetRenderDrawColor(as->renderer, 0, 0, 0, 255);
    SDL_RenderClear(as->renderer);

    updateTree(as->root, dt);
    renderTree(as->root, as->renderer);

    SDL_RenderPresent(as->renderer);
    SDL_Delay(1);  // Minimal delay to prevent 100% CPU on some systems
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void*, SDL_Event* e) {
    return (e->type == SDL_EVENT_QUIT ? SDL_APP_SUCCESS : SDL_APP_CONTINUE);
}

void SDL_AppQuit(void* appstate, SDL_AppResult) {
    if (appstate) {
        auto* as = (AppState*)appstate;
        // root NodePtr and its children will be auto-deleted by shared_ptr
        SDL_DestroyRenderer(as->renderer);
        SDL_DestroyWindow(as->window);
        delete as;
    }
    SDL_Quit();
}
