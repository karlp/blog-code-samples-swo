/*
 * Copyright (C) 2013 Karl Palsson <karlp@tweak.net.au>
 *
 * Insert your choice of BSD 2 clause, Apache 2.0, MIT or ISC licenses here
 */

#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <libopencm3/cm3/itm.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/exti.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/timer.h>

#define LED_DISCO_GREEN_PORT GPIOB
#define LED_DISCO_GREEN_PIN GPIO7
#define LED_DISCO_BLUE_PORT GPIOB
#define LED_DISCO_BLUE_PIN GPIO6

#define BUTTON_DISCO_USER_PORT GPIOA
#define BUTTON_DISCO_USER_PIN GPIO0
#define BUTTON_DISCO_USER_EXTI EXTI0
#define BUTTON_DISCO_USER_isr exti0_isr
#define BUTTON_DISCO_USER_NVIC NVIC_EXTI0_IRQ

struct state_t {
	bool falling;
	int tickcount;
};

static struct state_t state;

enum {
	STIMULUS_PRINTF,
	// We'll have more later
};

static void clock_setup(void)
{
	rcc_clock_setup_pll(&clock_config[CLOCK_VRANGE1_HSI_PLL_24MHZ]);
	/* Lots of things on all ports... */
	rcc_peripheral_enable_clock(&RCC_AHBENR, RCC_AHBENR_GPIOAEN);
	rcc_peripheral_enable_clock(&RCC_AHBENR, RCC_AHBENR_GPIOBEN);

	/* And timers. */
	rcc_peripheral_enable_clock(&RCC_APB1ENR, RCC_APB1ENR_TIM6EN);
	rcc_peripheral_enable_clock(&RCC_APB1ENR, RCC_APB1ENR_TIM7EN);

}

static void gpio_setup(void)
{
	/* green led for ticking, blue for button feedback */
	gpio_mode_setup(LED_DISCO_GREEN_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED_DISCO_GREEN_PIN);
	gpio_mode_setup(LED_DISCO_BLUE_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED_DISCO_BLUE_PIN);
}

void BUTTON_DISCO_USER_isr(void)
{
	exti_reset_request(BUTTON_DISCO_USER_EXTI);
	if (state.falling) {
		gpio_clear(LED_DISCO_BLUE_PORT, LED_DISCO_BLUE_PIN);
		state.falling = false;
		exti_set_trigger(BUTTON_DISCO_USER_EXTI, EXTI_TRIGGER_RISING);
		unsigned int x = TIM_CNT(TIM7);
		printf("held: %u ms\n", x);
	} else {
		gpio_set(LED_DISCO_BLUE_PORT, LED_DISCO_BLUE_PIN);
		printf("Pushed down!\n");
		TIM_CNT(TIM7) = 0;
		state.falling = true;
		exti_set_trigger(BUTTON_DISCO_USER_EXTI, EXTI_TRIGGER_FALLING);
	}
}

static volatile int t6ovf = 0;

void tim6_isr(void)
{
	TIM_SR(TIM6) &= ~TIM_SR_UIF;
	if (t6ovf++ > 1000) {
		state.tickcount++;
		printf("TICK %d\n", state.tickcount);
		t6ovf = 0;
		gpio_toggle(LED_DISCO_GREEN_PORT, LED_DISCO_GREEN_PIN);
	}
}

/*
 * Another ms timer, this one used to generate an overflow interrupt at 1ms
 * It is used to toggle leds and write tick counts
 */
static void setup_tim6(void)
{
	timer_reset(TIM6);
	// 24Mhz / 10khz -1.
	timer_set_prescaler(TIM6, 2399); // 24Mhz/10000hz - 1
	// 10khz for 10 ticks = 1 khz overflow = 1ms overflow interrupts
	timer_set_period(TIM6, 10);

	nvic_enable_irq(NVIC_TIM6_IRQ);
	timer_enable_update_event(TIM6); // default at reset!
	timer_enable_irq(TIM6, TIM_DIER_UIE);
	timer_enable_counter(TIM6);
}

/*
 * Free running ms timer, no advatange over systick.
 */
static void setup_tim7(void)
{
	timer_reset(TIM7);
	// 24Mhz/1000hz - 1
	timer_set_prescaler(TIM7, 23999);
	timer_set_period(TIM7, 0xffff);
	timer_enable_counter(TIM7);
}

static void setup_buttons(void)
{
	/* Enable EXTI0 interrupt. */
	nvic_enable_irq(BUTTON_DISCO_USER_NVIC);

	gpio_mode_setup(BUTTON_DISCO_USER_PORT, GPIO_MODE_INPUT, GPIO_PUPD_NONE, BUTTON_DISCO_USER_PIN);

	/* Configure the EXTI subsystem. */
	exti_select_source(BUTTON_DISCO_USER_EXTI, BUTTON_DISCO_USER_PORT);
	state.falling = false;
	exti_set_trigger(BUTTON_DISCO_USER_EXTI, EXTI_TRIGGER_RISING);
	exti_enable_request(BUTTON_DISCO_USER_EXTI);
}

static void trace_send_blocking(int stimulus, char c)
{
	while (!(ITM_STIM8(stimulus) & ITM_STIM_FIFOREADY))
		;
	ITM_STIM8(stimulus) = c;
}

/**
 * Uses ITM stimulus port 0 as a console.
 * @param file
 * @param ptr
 * @param len
 * @return
 */
int _write(int file, char *ptr, int len)
{
	int i;

	if (file == STDOUT_FILENO || file == STDERR_FILENO) {
		for (i = 0; i < len; i++) {
			if (ptr[i] == '\n') {
				trace_send_blocking(STIMULUS_PRINTF, '\r');
			}
			trace_send_blocking(STIMULUS_PRINTF, ptr[i]);
		}
		return i;
	}
	errno = EIO;
	return -1;
}

int main(void)
{
	clock_setup();
	gpio_setup();
	printf("hi guys!\n");
	setup_buttons();
	setup_tim6();
	setup_tim7();
	while (1) {
		;
	}

	return 0;
}
