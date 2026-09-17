#include "ch32fun.h"
#include <stdio.h>

// use defines to make more meaningful names for our GPIO pins
#define PIN_1 PC1
#define PIN_K PC2

int main()
{
	SystemInit();

	funGpioInitAll(); // Enable GPIOs

	funPinMode( PIN_1,     GPIO_Speed_10MHz | GPIO_CNF_OUT_PP ); // Set PIN_1 to output
	funPinMode( PIN_K,     GPIO_Speed_10MHz | GPIO_CNF_OUT_PP ); // Set PIN_K to output

	while(1)
	{
		funDigitalWrite( PIN_1,     FUN_HIGH ); // Turn on PIN_1
		funDigitalWrite( PIN_K,     FUN_HIGH ); // Turn on PIN_K
		Delay_Ms( 250 );
		funDigitalWrite( PIN_1,     FUN_LOW );  // Turn off PIN_1
		funDigitalWrite( PIN_K,     FUN_LOW );  // Turn off PIN_K
		Delay_Ms( 250 );
	}
}
