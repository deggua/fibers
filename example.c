#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "fibers.h"

Fiber *fiberA, *fiberB;
int    var = 0;

void FiberRoutineA(void* arg)
{
    Fiber_Storage_Bind(fiberA, &var, sizeof(var));
    var = 0;

    while (var < 5) {
        printf("FiberRoutineA\n");
        printf("var = %d\n\n", var++);

        if (var == 2) {
            Fiber_Storage_Release(fiberA, &var);
        }

        Fiber_Yield(fiberA, fiberB);
    }
}

void FiberRoutineB(void* arg)
{
    Fiber_Storage_Bind(fiberB, &var, sizeof(var));
    var = -10;

    while (true) {
        printf("FiberRoutineB\n");
        printf("var = %d\n\n", var);
        var += 2;

        Fiber_Yield(fiberB, fiberA);
    }
}

void FiberExit(void)
{
    exit(0);
}

#define KiB 1024

int main(int argc, char** argv)
{
    fiberA = Fiber_Create(FiberRoutineA, 4 * KiB, FiberExit);
    fiberB = Fiber_Create(FiberRoutineB, 4 * KiB, FiberExit);

    Fiber_Yield(NULL, fiberA);
    return 0;
}
