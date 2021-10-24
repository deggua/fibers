#ifndef FIBERS_H
#define FIBERS_H

#include <stdint.h>
#include <stddef.h>

typedef void* Fiber_t;

// Yields current execution to another fiber
void FiberYield(Fiber_t fiber);

// Creates a new fiber given an entry point and stack size which can be yielded to in order to begin execution
Fiber_t FiberCreate(void (*entryPoint)(void *arg), size_t stackSize);

// Deletes a fiber, freeing its allocated memory
void FiberDelete(Fiber_t fiber);

// Binds a pointer to the current fiber, returns the pointer
void* FiberStorageBind(void** var);

// Releases a pointers binding, making it non-local to the fiber
void FiberStorageRelease(void** var);

#endif