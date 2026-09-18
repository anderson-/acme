#include "ch32fun.h"
#include <stdio.h>

int main(void)
{
    SystemInit();
    int count = 0;
    while (1) {
        printf("ACME CH32 debug message %d\n", count++);
        Delay_Ms(1000);
    }
}
