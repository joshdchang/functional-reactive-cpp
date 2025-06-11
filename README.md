# Functional Reactive Programming in C++

This project showcases a proof-of-concept implementation of functional reactive programming (FRP) principles in C++, applied to game development. FRP is common in web development but is not used in C++ game development. The main guiding principle is composition over inheritance. Every "component" is just a function that can take parameters and returns a node. Every node can have child nodes, and the game loop just iterates through this tree. Game logic, rendering, and state management are seperated from each other and placed into special "hooks". State changes propagate automatically through the system, making complex interactions more manageable. In some ways, using general functional "components" like this is a nicer developer experience than traditional OOP, or even ECS, becuase it minimizes the mental overhead of managing complex, intertwined state. It does have some potential performance drawbacks. You could build out a context system like in React as an escape hatch to manage global state without prop drilling, but that is not included in this example.

## Features

1. React-like hooks system:
   - `state<T>()` (like React's useState)
   - `effect()` (like React's useEffect)
   - `derived()` (like React's useMemo)
2. Declarative component composition patterns (like JSX)
3. Automatic dependency tracking
4. Conditional rendering
5. Prop system for flexible component parameters

It differs rom React in that the component tree is not re-constructed on every change. This makes for a very nice DX in React, because you can kinda just do whatever you want without having to think about the consequences, but it is probably not sufficiently performant for this use case (although you could try to track prop changes and do something cool idk). Instead, the component tree is built once and then updated as needed, more like SolidJS.
