#ifndef FIBERS_H
#define FIBERS_H

#include <stddef.h>

typedef struct Fiber Fiber;

// Yields current execution to another fiber
// fromFiber can be NULL, toFiber cannot be NULL
void Fiber_Yield(Fiber* fromFiber, Fiber* toFiber);

// Creates a new fiber given an entry point and stack size which can be yielded to in order to begin execution
// Exit func is the function to return to from when the fiber returns, and is responsible for clean up
Fiber* Fiber_Create(void (*entryPoint)(void*), size_t stackSize, void (*exitFunc)(void));

// Deletes a fiber, freeing its allocated memory and storage
void Fiber_Delete(Fiber* fiber);

// Binds a pointer to the current fiber, returns the pointer
void* Fiber_Storage_Bind(Fiber* fiber, void* var, size_t size);

// Releases a pointers binding, making it non-local to the fiber
void Fiber_Storage_Release(Fiber* fiber, void* var);

#endif
