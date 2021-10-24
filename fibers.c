/* --- INCLUDES --- */

#include <stddef.h>
#include <stdlib.h>
#include <stdalign.h>
#include <assert.h>

#include "fibers.h"

/* --- DEFINES --- */

#define naked           __attribute__((naked))
#define nopadding       __attribute__((packed, aligned(1)))
#define STACK_ALIGNMENT 16u
#define RED_ZONE        128u
#define STACK_MINIMUM   (RED_ZONE)

/* --- TYPES --- */

typedef struct FiberLocalStorage {
    struct FiberLocalStorage* next;
    void** variable;
    void* value;
} FiberLocalStorage_t;

typedef struct FiberContext {
    union {
        uint64_t rip;
        void (*entryPoint)(void*);
    };
    union {
        uint64_t rsp;
        void* stackPointer;
    };
    uint64_t rbp;
    uint64_t rbx;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
} nopadding FiberContext_t;

typedef struct FiberStack {
                             size_t     size;
    alignas(STACK_ALIGNMENT) uint8_t    data[];
} FiberStack_t;

typedef struct FiberInstance {
                             FiberLocalStorage_t*   fls;
                             FiberContext_t         ctx;
    alignas(STACK_ALIGNMENT) FiberStack_t           stack;
} FiberInstance_t;

/* --- GLOBALS --- */

static FiberInstance_t* CurrentFiber = NULL;

/* --- FUNCTIONS --- */

// performs the context switch, if curCtx is NULL skips the store of the current context
static naked void FiberContextSwitch(FiberContext_t* curCtx, FiberContext_t* newCtx) {
    asm (
        // check if thisCtx is NULL, in which case we skip saving our ctx
        "test %rdi, %rdi\n\t"
        "jz 1f\n\t"

        // first we store our ctx to thisCtx with rip = ret so we are out
        // of this function when our ctx is loaded
        "pop %rax\n\t"              // rax = ret addr, remove ret addr from stack
        "mov %rax, 0x00(%rdi)\n\t"  // thisCtx->rip = ret addr
        "mov %rsp, 0x08(%rdi)\n\t"  // thisCtx->rsp = rsp
        "mov %rbp, 0x10(%rdi)\n\t"  // thisCtx->rbp = rbp
        "mov %rbx, 0x18(%rdi)\n\t"  // thisCtx->rbx = rbx
        "mov %r12, 0x20(%rdi)\n\t"  // thisCtx->r12 = r12
        "mov %r13, 0x28(%rdi)\n\t"  // thisCtx->r13 = r13
        "mov %r14, 0x30(%rdi)\n\t"  // thisCtx->r14 = r14
        "mov %r15, 0x38(%rdi)\n\t"  // thisCtx->r15 = r15

        // now we can load the newCtx to switch to a new fiber
        "1:\n\t"
        "mov 0x38(%rsi), %r15\n\t"  // r15 = newCtx->r15
        "mov 0x30(%rsi), %r14\n\t"  // r14 = newCtx->r14
        "mov 0x28(%rsi), %r13\n\t"  // r13 = newCtx->r13
        "mov 0x20(%rsi), %r12\n\t"  // r12 = newCtx->r12
        "mov 0x18(%rsi), %rbx\n\t"  // rbx = newCtx->rbx
        "mov 0x10(%rsi), %rbp\n\t"  // rbp = newCtx->rbp
        "mov 0x08(%rsi), %rsp\n\t"  // rsp = newCtx->rsp
        "mov 0x00(%rsi), %rax\n\t"  // rax = newCtx->rip
        "jmp *%rax\n\t"             // jmp newCtx->rip
    );
}

// allocates a fiber, sets up the stack, and sets up the entry point
Fiber_t FiberCreate(void (*entryPoint)(void*), size_t stackSize) {
    if (stackSize % STACK_ALIGNMENT != 0 || stackSize < STACK_MINIMUM) {
        return NULL;
    }

    // allocate an aligned instance for the fiber
    size_t requiredSpace    = sizeof(FiberInstance_t) + stackSize * sizeof(uint8_t);
    size_t alignAdjustment  = STACK_ALIGNMENT - (requiredSpace % STACK_ALIGNMENT);
    FiberInstance_t* fiber  = aligned_alloc(STACK_ALIGNMENT, requiredSpace + alignAdjustment);
    if (fiber == NULL) {
        return NULL;
    }

    fiber->stack.size       = stackSize;
    fiber->ctx.stackPointer = &fiber->stack.data[stackSize - RED_ZONE];
    fiber->ctx.entryPoint   = entryPoint;
    fiber->fls              = NULL;

    return (Fiber_t)fiber;
}

// binds a pointer to a fiber
void* FiberStorageBind(void** var) {
    // alloc list entry
    FiberLocalStorage_t* newFLS = malloc(sizeof(FiberLocalStorage_t));
    if (newFLS == NULL) {
        return NULL;
    }

    // associate pointer
    newFLS->variable = var;

    // insert into list
    if (CurrentFiber->fls == NULL) {
        newFLS->next = NULL;
        CurrentFiber->fls = newFLS;
    } else {
        newFLS->next = CurrentFiber->fls;
        CurrentFiber->fls = newFLS;
    }

    return *var;
}

// releases a pointer binding
void FiberStorageRelease(void** var) {
    // find the list entry matching var
    FiberLocalStorage_t *flsPrev = NULL, *flsSel = NULL;
    for (FiberLocalStorage_t* fls = CurrentFiber->fls; fls != NULL; fls = fls->next) {
        if (fls->variable == var) {
            flsSel = fls;
            break;
        }
        flsPrev = fls;
    }

    // delete it
    if (flsSel == NULL) {
        // couldn't find an appropriate FLS entry
        return;
    } else if (flsPrev == NULL) {
        // the one found is the head of the FLS list
        CurrentFiber->fls = NULL;
        free(flsSel);
        return;
    } else {
        // remove the entry and join the list halves
        flsPrev->next = flsSel->next;
        free(flsSel);
        return;
    }
}

// loads bound pointers to their fiber local storage
static void FiberLoadFLS(FiberInstance_t* fiber) {
    for (FiberLocalStorage_t* fls = fiber->fls; fls != NULL; fls = fls->next) {
        *(fls->variable) = fls->value;
    }
}

// store bound pointers to their fiber local storage
static void FiberStoreFLS(FiberInstance_t* fiber) {
    for (FiberLocalStorage_t* fls = fiber->fls; fls != NULL; fls = fls->next) {
        fls->value = *(fls->variable);
    }
}

// yield to a new fiber
void FiberYield(Fiber_t fiber) {
    // store current FLS values
    if (CurrentFiber != NULL) {
        FiberStoreFLS(CurrentFiber);
    }

    // change the current fiber and load FLS values
    FiberLoadFLS(fiber);

    // switch to fiber
    FiberContext_t* ctxCurrent = NULL;
    if (CurrentFiber != NULL) {
        ctxCurrent = &CurrentFiber->ctx;
    }

    CurrentFiber = fiber;
    FiberContextSwitch(ctxCurrent, &((FiberInstance_t*)fiber)->ctx);
    return;
}

// deletes a fiber and its associated local storage
void FiberDelete(Fiber_t fiber) {
    FiberInstance_t* fiberInstance = (FiberInstance_t*)fiber;
    while (fiberInstance->fls != NULL) {
        FiberStorageRelease(fiberInstance->fls->variable);
    }

    free(fiber);
}