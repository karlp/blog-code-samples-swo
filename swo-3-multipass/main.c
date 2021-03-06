/*
 * Copyright (C) 2013-2014 Karl Palsson <karlp@tweak.net.au>
 *
 * Insert your choice of BSD 2 clause, Apache 2.0, MIT or ISC licenses here
 */

#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#include <libopencm3/cm3/scs.h>
#include <libopencm3/cm3/itm.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/adc.h>
#include <libopencm3/stm32/dma.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/exti.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/timer.h>

#include "discovery_board.h"

/* prototype to keep newlib happy */
int _write(int file, char *ptr, int len);

#define ARRAY_LENGTH(array) (sizeof((array))/sizeof((array)[0]))

struct state_t {
	bool falling;
	int tick_count;
	volatile int sample_counter;
	uint16_t sample_buffer[4];
};

static struct state_t state;

enum {
	STIMULUS_PRINTF, /* 0x01 */
	STIMULUS_DAC_OUT, /* 0x02 */
	STIMULUS_ADC_IN, /* 0x04 */
	STIMULUS_TIMING_IRQ_BUTTON, /* 0x08 */
	STIMULUS_TIMING_IRQ_DMA, /* 0x10 */
};

static void trace_send_blocking8(int stimulus, char c) {
	while (!(ITM_STIM8(stimulus) & ITM_STIM_FIFOREADY))
		;
	ITM_STIM8(stimulus) = c;
}

static void trace_send_blocking16(int stimulus, uint16_t c) {
	while (!(ITM_STIM16(stimulus) & ITM_STIM_FIFOREADY))
		;
	ITM_STIM16(stimulus) = c;
}

static void trace_send_blocking32(int stimulus, uint32_t c) {
	while (!(ITM_STIM32(stimulus) & ITM_STIM_FIFOREADY))
		;
	ITM_STIM32(stimulus) = c;
}

static void clock_setup(void) {
	rcc_clock_setup_pll(&clock_config[CLOCK_VRANGE1_HSI_PLL_24MHZ]);
}

static void setup_buttons_gpios(void) {
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);

	/* green led for ticking, blue for button feedback */
	gpio_mode_setup(LED_DISCO_GREEN_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED_DISCO_GREEN_PIN);
	gpio_mode_setup(LED_DISCO_BLUE_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED_DISCO_BLUE_PIN);
}

void BUTTON_DISCO_USER_isr(void) {
	uint32_t before = SCS_DWT_CYCCNT;
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
	trace_send_blocking32(STIMULUS_TIMING_IRQ_BUTTON, SCS_DWT_CYCCNT - before);
}

static void process_samples(uint16_t *samps) {
	trace_send_blocking16(STIMULUS_ADC_IN, samps[0]);
	/* TODO - demo oversampling here, or something like that... */
	/* let's make the processing not take a "fixed" amount of time. */
	if (samps[0] == samps[1]) {
		gpio_toggle(LED_DISCO_GREEN_PORT, LED_DISCO_GREEN_PIN);
	}
	/* Make it take longer based on buttons too */
	if (gpio_get(LED_DISCO_BLUE_PORT, LED_DISCO_BLUE_PIN)) {
		for (int i = 0; i < samps[0] / 4; i++) {
			__asm__("nop");
		}
	}
}

void dma1_channel1_isr(void) {
	uint32_t cyc_before = SCS_DWT_CYCCNT;
	if (dma_get_interrupt_flag(DMA1, DMA_CHANNEL1, DMA_TCIF)) {
		process_samples(state.sample_buffer);
		state.sample_counter++;
		dma_clear_interrupt_flags(DMA1, DMA_CHANNEL1, DMA_TCIF);
	}
	trace_send_blocking32(STIMULUS_TIMING_IRQ_DMA, SCS_DWT_CYCCNT - cyc_before);
}

/**
 * Set up an ADC sampling trigger at 5KHz
 */
static
void setup_adc_trigger(void) {
	rcc_periph_clock_enable(RCC_TIM6);
	// Timer to trigger ADC sampling
	uint32_t timer = TIM6;

	timer_disable_counter(timer);
	timer_set_prescaler(timer, 239); // 24MHz/100kHz - 1
	timer_set_period(timer, 19); // trigger at 5khz
	timer_set_master_mode(timer, TIM_CR2_MMS_UPDATE);

	TIM_CNT(timer) = 0;
	timer_enable_counter(timer);
}

/*
 * Free running ms timer, no advatange over systick.
 */
static void setup_button_timer(void) {
	rcc_periph_clock_enable(RCC_TIM7);

	timer_reset(TIM7);
	// 24Mhz/1000hz - 1
	timer_set_prescaler(TIM7, 23999);
	timer_set_period(TIM7, 0xffff);
	timer_enable_counter(TIM7);
}

static void setup_buttons(void) {
	/* Timer 7 will time the button presses */
	setup_button_timer();

	setup_buttons_gpios();

	/* Enable EXTI0 interrupt. */
	nvic_enable_irq(BUTTON_DISCO_USER_NVIC);

	gpio_mode_setup(BUTTON_DISCO_USER_PORT, GPIO_MODE_INPUT, GPIO_PUPD_NONE, BUTTON_DISCO_USER_PIN);

	/* Configure the EXTI subsystem. */
	exti_select_source(BUTTON_DISCO_USER_EXTI, BUTTON_DISCO_USER_PORT);
	state.falling = false;
	exti_set_trigger(BUTTON_DISCO_USER_EXTI, EXTI_TRIGGER_RISING);
	exti_enable_request(BUTTON_DISCO_USER_EXTI);
}

/**
 * Uses ITM stimulus port 0 as a console.
 * @param file
 * @param ptr
 * @param len
 * @return
 */
int _write(int file, char *ptr, int len) {
	int i;

	if (file == STDOUT_FILENO || file == STDERR_FILENO) {
		for (i = 0; i < len; i++) {
			if (ptr[i] == '\n') {
				trace_send_blocking8(STIMULUS_PRINTF, '\r');
			}
			trace_send_blocking8(STIMULUS_PRINTF, ptr[i]);
		}
		return i;
	}
	errno = EIO;
	return -1;
}

/*
 * Continuous circular transfer of 16bits from ADC to ram
 * FIXME - let it take a dest pointer instead?
 */
static
void setup_adc_dma(void) {
	// turn on DMA and start it up!
	dma_channel_reset(DMA1, DMA_CHANNEL1); // channel1 is adc
	dma_set_memory_address(DMA1, DMA_CHANNEL1, (uint32_t) state.sample_buffer);
	dma_set_memory_size(DMA1, DMA_CHANNEL1, DMA_CCR_MSIZE_16BIT);
	dma_enable_memory_increment_mode(DMA1, DMA_CHANNEL1);
	dma_set_peripheral_address(DMA1, DMA_CHANNEL1, (uint32_t) & ADC_DR(ADC1));
	dma_set_peripheral_size(DMA1, DMA_CHANNEL1, DMA_CCR_PSIZE_16BIT);
	dma_set_number_of_data(DMA1, DMA_CHANNEL1, ARRAY_LENGTH(state.sample_buffer));

	dma_enable_transfer_complete_interrupt(DMA1, DMA_CHANNEL1);
	dma_enable_transfer_error_interrupt(DMA1, DMA_CHANNEL1);
	dma_set_read_from_peripheral(DMA1, DMA_CHANNEL1);

	dma_enable_circular_mode(DMA1, DMA_CHANNEL1);

	dma_enable_channel(DMA1, DMA_CHANNEL1);
	nvic_enable_irq(NVIC_DMA1_CHANNEL1_IRQ);

	/* Keep on requesting DMA, as long as ADC is running */
	ADC_CR2(ADC1) |= ADC_CR2_DDS;
	adc_enable_dma(ADC1);
}

static
void setup_adc(void) {
	rcc_periph_clock_enable(RCC_ADC1);
	/* We're going to DMA adc samples continuously */
	rcc_periph_clock_enable(RCC_DMA1);

	/* Just one input for now */
	gpio_mode_setup(DEMO_ADC_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, DEMO_ADC_PIN);

	adc_off(ADC1);
	adc_enable_scan_mode(ADC1);
	/* Oversample four samples of the same channel */
	uint8_t sample_map[] = {DEMO_ADC_CHANNEL, DEMO_ADC_CHANNEL, DEMO_ADC_CHANNEL, DEMO_ADC_CHANNEL};
	adc_set_regular_sequence(ADC1, ARRAY_LENGTH(sample_map), sample_map);

	// Enable the external trigger source as timer6
	adc_enable_external_trigger_regular(ADC1, ADC_CR2_EXTSEL_TIM6_TRGO, ADC_CR2_EXTEN_RISING_EDGE);
	adc_set_sample_time_on_all_channels(ADC1, ADC_SMPR_SMP_48CYC);

	/* Note the datasheet sec 6.3.3, 10uSec for the viref/tempsense (worst case) */
	adc_set_sample_time(ADC1, ADC_CHANNEL_VREFINT, ADC_SMPR_SMP_192CYC);
	adc_set_sample_time(ADC1, ADC_CHANNEL_TEMP, ADC_SMPR_SMP_192CYC);

	/* This also enables the vref int channel */
	adc_enable_temperature_sensor();

	/* Enable the ADC, and wait for it to be ready */
	adc_power_on(ADC1);
	while ((ADC_SR(ADC1) & ADC_SR_ADONS) == 0) {
		;
	}

	setup_adc_dma();

	/* trigger the adc at a known rate */
	setup_adc_trigger();
}

static
void setup_dac(void) {
	rcc_periph_clock_enable(RCC_DAC);
	// FIXME - incomplete
	/* Eventually, would be nice to generate here, and dump out to the adc... */

}

int main(void) {
	clock_setup();
	printf("hi guys!\n");
	setup_buttons();
	setup_adc();
	setup_dac();
	while (1) {
		if (state.sample_counter % 5000 == 0) {
			state.tick_count++;
			printf("Tick: %d\n", state.tick_count);
		}
	}

	return 0;
}
