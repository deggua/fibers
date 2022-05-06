#include "fibers.h"

#include <assert.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define naked           __attribute__((naked))
#define nopadding       __attribute__((packed, aligned(1)))
#define STACK_ALIGNMENT 16u
#define RED_ZONE        128u
#define STACK_MINIMUM   (RED_ZONE)

typedef struct FiberLocalStorage {
    struct FiberLocalStorage* next;
    void*                     variable;
    size_t                    size;
    uint8_t                   value[];
} FiberLocalStorage;

typedef struct FiberContext {
    union {
        uint64_t rip;
        void (*entryPoint)(void*);
    };

    union {
        uint64_t rsp;
        void*    stackPointer;
    };

    uint64_t rbp;
    uint64_t rbx;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
} nopadding FiberContext;

typedef struct FiberStack {
    size_t size;
    alignas(STACK_ALIGNMENT) uint8_t data[];
} FiberStack;

typedef struct Fiber {
    FiberLocalStorage* fls;
    FiberContext       ctx;
    FiberStack         stack;
} Fiber;

static inline void MemoryClobber(void)
{
    asm volatile("" : : : "memory");
}

// performs the context switch, if curCtx is NULL skips the store of the current context
static naked void ContextSwitch(FiberContext* curCtx, FiberContext* newCtx)
{
    asm(
        // check if thisCtx is NULL, in which case we skip saving our ctx
        "test %rdi, %rdi\n\t"
        "jz 1f\n\t"

        // first we store our ctx to thisCtx with rip = ret so we are out
        // of this function when our ctx is loaded
        "pop %rax\n\t"             // rax = ret addr, remove ret addr from stack
        "mov %rax, 0x00(%rdi)\n\t" // thisCtx->rip = ret addr
        "mov %rsp, 0x08(%rdi)\n\t" // thisCtx->rsp = rsp
        "mov %rbp, 0x10(%rdi)\n\t" // thisCtx->rbp = rbp
        "mov %rbx, 0x18(%rdi)\n\t" // thisCtx->rbx = rbx
        "mov %r12, 0x20(%rdi)\n\t" // thisCtx->r12 = r12
        "mov %r13, 0x28(%rdi)\n\t" // thisCtx->r13 = r13
        "mov %r14, 0x30(%rdi)\n\t" // thisCtx->r14 = r14
        "mov %r15, 0x38(%rdi)\n\t" // thisCtx->r15 = r15

        // now we can load the newCtx to switch to a new fiber
        "1:\n\t"
        "mov 0x38(%rsi), %r15\n\t" // r15 = newCtx->r15
        "mov 0x30(%rsi), %r14\n\t" // r14 = newCtx->r14
        "mov 0x28(%rsi), %r13\n\t" // r13 = newCtx->r13
        "mov 0x20(%rsi), %r12\n\t" // r12 = newCtx->r12
        "mov 0x18(%rsi), %rbx\n\t" // rbx = newCtx->rbx
        "mov 0x10(%rsi), %rbp\n\t" // rbp = newCtx->rbp
        "mov 0x08(%rsi), %rsp\n\t" // rsp = newCtx->rsp
        "mov 0x00(%rsi), %rax\n\t" // rax = newCtx->rip
        "jmp *%rax\n\t"            // jmp newCtx->rip
    );
}

// allocates a fiber, sets up the stack, and sets up the entry point
Fiber* Fiber_Create(void (*entryPoint)(void*), size_t stackSize, void (*exitFunc)(void))
{
    if (stackSize % STACK_ALIGNMENT != 0 || stackSize < STACK_MINIMUM) {
        return NULL;
    }

    size_t requiredSpace   = sizeof(Fiber) + stackSize * sizeof(uint8_t);
    size_t alignAdjustment = STACK_ALIGNMENT - (requiredSpace % STACK_ALIGNMENT);
    Fiber* fiber           = aligned_alloc(STACK_ALIGNMENT, requiredSpace + alignAdjustment);
    if (fiber == NULL) {
        return NULL;
    }

    fiber->stack.size       = stackSize;
    fiber->ctx.stackPointer = &fiber->stack.data[stackSize - RED_ZONE - sizeof(uint64_t)];
    fiber->ctx.entryPoint   = entryPoint;
    fiber->fls              = NULL;

    // make Fiber_Exit the return address so returning from the fiber cleans itself up
    *(uint64_t*)fiber->ctx.stackPointer = (uint64_t)exitFunc;

    return fiber;
}

// TODO: I'm pretty sure we could implement this style of FLS with a more efficient style
// as the underlying mechanism. I.e. instead of binding, have an arena allocator tied to the
// fiber instance, and just return an index/handle that creates the storage in the fiber, therefore
// no copies required on context switch. You can implement binding style by allocating in the FLS heap
// and chaining together these (see FiberLocalStorage) FLS blocks in the FLS heap and wrapping Fiber_Yield
// with something that does the traversal and whatnot (might also need to wrap Fiber_Create to make the first
// alloc the head of the FLS blocks LL, or use a vector or something)

// I think the most minimal thing required would be an opaque pointer attached to the fiber
// at creation time, just so we don't need to implement all the arena logic in here
void* Fiber_Storage_Bind(Fiber* fiber, void* var, size_t size)
{
    FiberLocalStorage* newFLS = malloc(sizeof(FiberLocalStorage) + size);
    if (newFLS == NULL) {
        return NULL;
    }

    newFLS->variable = var;
    newFLS->size     = size;

    // insert into list
    if (fiber->fls == NULL) {
        newFLS->next = NULL;
        fiber->fls   = newFLS;
    } else {
        newFLS->next = fiber->fls;
        fiber->fls   = newFLS;
    }

    return var;
}

void Fiber_Storage_Release(Fiber* fiber, void* var)
{
    // find the list entry matching var
    FiberLocalStorage *flsPrev = NULL, *flsSel = NULL;
    for (FiberLocalStorage* fls = fiber->fls; fls != NULL; fls = fls->next) {
        if (fls->variable == var) {
            flsSel = fls;
            break;
        }
        flsPrev = fls;
    }

    if (flsSel == NULL) {
        // couldn't find an appropriate FLS entry
        return;
    } else if (flsPrev == NULL) {
        // the one found is the head of the FLS list
        fiber->fls = NULL;
        free(flsSel);
        return;
    } else {
        // remove the entry and join the list halves
        flsPrev->next = flsSel->next;
        free(flsSel);
        return;
    }
}

static void LoadFLS(Fiber* fiber)
{
    for (FiberLocalStorage* fls = fiber->fls; fls != NULL; fls = fls->next) {
        memcpy(fls->variable, fls->value, fls->size);
    }
}

static void StoreFLS(Fiber* fiber)
{
    for (FiberLocalStorage* fls = fiber->fls; fls != NULL; fls = fls->next) {
        memcpy(fls->value, fls->variable, fls->size);
    }
}

void Fiber_Yield(Fiber* fromFiber, Fiber* toFiber)
{
    if (fromFiber != NULL) {
        StoreFLS(fromFiber);
    }

    LoadFLS(toFiber);

    FiberContext* fromCtx = NULL;
    if (fromFiber != NULL) {
        fromCtx = &fromFiber->ctx;
    }

    ContextSwitch(fromCtx, &toFiber->ctx);

    MemoryClobber();
    return;
}

void Fiber_Delete(Fiber* fiber)
{
    while (fiber->fls != NULL) {
        Fiber_Storage_Release(fiber, fiber->fls->variable);
    }

    free(fiber);
}
