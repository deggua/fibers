#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>

#include "fibers.h"

Fiber_t fiberA, fiberB;
int *exP, *exQ;

void FiberRoutineA(void* arg) {
    exP = malloc(sizeof(int));
    exQ = malloc(sizeof(int));

    FiberStorageBind(&exP);
    FiberStorageBind(&exQ);

    *exP = 0;
    *exQ = 0;

    while (true) {
        printf("FiberRoutineA\n");
        printf("P = %d\n", *exP);
        printf("Q = %d\n\n", *exQ);

        *exP += 10;
        *exQ += 1;

        FiberYield(fiberB);
    }
}

void FiberRoutineB(void* arg) {
    exP = malloc(sizeof(int));
    exQ = malloc(sizeof(int));

    FiberStorageBind(&exP);
    FiberStorageBind(&exQ);

    *exP = 0;
    *exQ = 0;

    while (true) {
        printf("FiberRoutineB\n");
        printf("P = %d\n", *exP);
        printf("Q = %d\n\n", *exQ);
        
        *exP += 1000;
        *exQ += 100;

        FiberYield(fiberA);
    }
}

int main(int argc, char** argv) {
    fiberA = FiberCreate(FiberRoutineA, 16*1024);
    fiberB = FiberCreate(FiberRoutineB, 16*1024);

    FiberYield(fiberA);
    return 0;
}
