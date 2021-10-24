# Fibers
Simple fibers library in C11
Only supports x86_64 linux currently

# Usage
See example.c for code

| Function | Description |
| -------- | ----------- |
| FiberYield | Yields execution to another fiber, saving the current fiber's state |
| FiberCreate | Performs the allocation for the fiber's stack and context |
| FiberDelete | Frees the allocated memory for the fiber |
| FiberStorageBind | Binds a pointer to a fiber, such that its current value is preserved across modifications in other fibers (Fiber Local Storage) |
| FiberStorageRelease | Releases a pointer binding |