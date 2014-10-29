/* 
 * File:   discovery_board.h
 * Author: karlp
 *
 * Created on December 19, 2013, 12:16 AM
 */

#ifndef DISCOVERY_BOARD_H
#define	DISCOVERY_BOARD_H

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef STM32L1
#define LED_DISCO_GREEN_PORT GPIOB
#define LED_DISCO_GREEN_PIN GPIO7
#define LED_DISCO_BLUE_PORT GPIOB
#define LED_DISCO_BLUE_PIN GPIO6

#define BUTTON_DISCO_USER_PORT GPIOA
#define BUTTON_DISCO_USER_PIN GPIO0
#define BUTTON_DISCO_USER_EXTI EXTI0
#define BUTTON_DISCO_USER_isr exti0_isr
#define BUTTON_DISCO_USER_NVIC NVIC_EXTI0_IRQ

/* This is what we want to hook up to the dac. */
#define DEMO_ADC_PORT GPIOA
#define DEMO_ADC_PIN GPIO1
#define DEMO_ADC_CHANNEL 1

#endif


#ifdef	__cplusplus
}
#endif

#endif	/* DISCOVERY_BOARD_H */

