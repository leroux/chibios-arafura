/*
    ChibiOS/RT - Copyright (C) 2006,2007,2008,2009,2010 Giovanni Di Sirio.

    This file is part of ChibiOS/RT.

    ChibiOS/RT is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    ChibiOS/RT is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
 * @file    STM32/uart_lld.c
 * @brief   STM32 low level UART driver code.
 *
 * @addtogroup STM32_UART
 * @{
 */

#include "ch.h"
#include "hal.h"

#if CH_HAL_USE_UART || defined(__DOXYGEN__)

/*===========================================================================*/
/* Driver exported variables.                                                */
/*===========================================================================*/

/** @brief USART1 UART driver identifier.*/
#if STM32_UART_USE_USART1 || defined(__DOXYGEN__)
UARTDriver UARTD1;
#endif

/*===========================================================================*/
/* Driver local variables.                                                   */
/*===========================================================================*/

/*===========================================================================*/
/* Driver local functions.                                                   */
/*===========================================================================*/

/**
 * @brief   Status bits translation.
 *
 * @param[in] sr        USART SR register value
 *
 * @return  The error flags.
 */
static uartflags_t translate_errors(uint16_t sr) {
  uartflags_t sts = 0;

  if (sr & USART_SR_ORE)
    sts |= UART_OVERRUN_ERROR;
  if (sr & USART_SR_PE)
    sts |= UART_PARITY_ERROR;
  if (sr & USART_SR_FE)
    sts |= UART_FRAMING_ERROR;
  if (sr & USART_SR_NE)
    sts |= UART_NOISE_ERROR;
  if (sr & USART_SR_LBD)
    sts |= UART_BREAK_DETECTED;
  return sts;
}

/**
 * @brief   Puts the receiver in the UART_RX_IDLE state.
 *
 * @param[in] uartp     pointer to the @p UARTDriver object
 */
static void set_rx_idle_loop(UARTDriver *uartp) {
  uint32_t ccr;
  
  /* RX DMA channel preparation, if the char callback is defined then the
     TCIE interrupt is enabled too.*/
  if (uartp->ud_config->uc_rxchar == NULL)
    ccr = DMA_CCR1_CIRC | DMA_CCR1_TEIE;
  else
    ccr = DMA_CCR1_CIRC | DMA_CCR1_TEIE | DMA_CCR1_TCIE;
  dmaSetupChannel(uartp->ud_dmap, uartp->ud_dmarx, 1,
                  &uartp->ud_rxbuf, uartp->ud_dmaccr | ccr);
  dmaEnableChannel(uartp->ud_dmap, uartp->ud_dmarx);
}

/**
 * @brief   USART de-initialization.
 * @details This function must be invoked with interrupts disabled.
 *
 * @param[in] uartp     pointer to the @p UARTDriver object
 */
static void usart_stop(UARTDriver *uartp) {

  /* Stops RX and TX DMA channels.*/
  dmaDisableChannel(uartp->ud_dmap, uartp->ud_dmarx);
  dmaDisableChannel(uartp->ud_dmap, uartp->ud_dmatx);
  dmaClearChannel(uartp->ud_dmap, uartp->ud_dmarx);
  dmaClearChannel(uartp->ud_dmap, uartp->ud_dmatx);
  
  /* Stops USART operations.*/
  uartp->ud_usart->CR1 = 0;
  uartp->ud_usart->CR2 = 0;
  uartp->ud_usart->CR3 = 0;
}

/**
 * @brief   USART initialization.
 * @details This function must be invoked with interrupts disabled.
 *
 * @param[in] uartp     pointer to the @p UARTDriver object
 */
static void usart_start(UARTDriver *uartp) {
  USART_TypeDef *u = uartp->ud_usart;

  /* Defensive programming, starting from a clean state.*/
  usart_stop(uartp);

  /* Baud rate setting.*/
  if (uartp->ud_usart == USART1)
    u->BRR = STM32_PCLK2 / uartp->ud_config->uc_speed;
  else
    u->BRR = STM32_PCLK1 / uartp->ud_config->uc_speed;

  /* Note that some bits are enforced because required for correct driver
     operations.*/
  u->CR1 = uartp->ud_config->uc_cr1 | USART_CR1_UE | USART_CR1_PEIE |
                                      USART_CR1_TE | USART_CR1_RE;
  u->CR2 = uartp->ud_config->uc_cr2 | USART_CR2_LBDIE;
  u->CR3 = uartp->ud_config->uc_cr3 | USART_CR3_EIE;

  /* Resetting eventual pending status flags.*/
  (void)u->SR;  /* SR reset step 1.*/
  (void)u->DR;  /* SR reset step 2.*/

  /* Starting the receiver idle loop.*/
  set_rx_idle_loop(uartp);
}

/**
 * @brief   RX DMA common service routine.
 *
 * @param[in] uartp     pointer to the @p UARTDriver object
 */
static void serve_rx_end_irq(UARTDriver *uartp) {

  uartp->ud_rxstate = UART_RX_COMPLETE;
  if (uartp->ud_config->uc_rxend != NULL)
    uartp->ud_config->uc_rxend();
  /* If the callback didn't explicitely change state then the receiver
     automatically returns to the idle state.*/
  if (uartp->ud_rxstate == UART_RX_COMPLETE) {
    uartp->ud_rxstate = UART_RX_IDLE;
    set_rx_idle_loop(uartp);
  }
}

/**
 * @brief   TX DMA common service routine.
 *
 * @param[in] uartp     pointer to the @p UARTDriver object
 */
static void serve_tx_end_irq(UARTDriver *uartp) {

  /* A callback is generated, if enabled, after a completed transfer.*/
  uartp->ud_txstate = UART_TX_COMPLETE;
  if (uartp->ud_config->uc_txend1 != NULL)
    uartp->ud_config->uc_txend1();
  /* If the callback didn't explicitely change state then the transmitter
     automatically returns to the idle state.*/
  if (uartp->ud_txstate == UART_TX_COMPLETE)
    uartp->ud_txstate = UART_TX_IDLE;
}
/**
 * @brief   USART common service routine.
 *
 * @param[in] uartp     pointer to the @p UARTDriver object
 */
static void serve_usart_irq(UARTDriver *uartp) {
  uint16_t sr;
  USART_TypeDef *u = uartp->ud_usart;
  
  sr = u->SR;   /* SR reset step 1.*/
  (void)u->DR;  /* SR reset step 2.*/
/////////////////  u->SR = 0;    /* Clears the LBD bit in the SR.*/
  if (uartp->ud_rxstate == UART_RX_IDLE) {
    /* Receiver in idle state, a callback is generated, if enabled, for each
       receive error and then the driver stays in the same state.*/
    if (uartp->ud_config->uc_rxerr != NULL)
      uartp->ud_config->uc_rxerr(translate_errors(sr));
  }
  else {
    /* Receiver in active state, a callback is generated and the receive
       operation aborts.*/
    dmaDisableChannel(uartp->ud_dmap, uartp->ud_dmarx);
    dmaClearChannel(uartp->ud_dmap, uartp->ud_dmarx);
    uartp->ud_rxstate = UART_RX_ERROR;
    if (uartp->ud_config->uc_rxerr != NULL)
      uartp->ud_config->uc_rxerr(translate_errors(sr));
    /* If the callback didn't explicitely change state then the receiver
       automatically returns to the idle state.*/
    if (uartp->ud_rxstate == UART_RX_ERROR) {
      uartp->ud_rxstate = UART_RX_IDLE;
      set_rx_idle_loop(uartp);
    }
  }
}

/*===========================================================================*/
/* Driver interrupt handlers.                                                */
/*===========================================================================*/

#if STM32_UART_USE_USART1 || defined(__DOXYGEN__)
/**
 * @brief   USART1 RX DMA interrupt handler (channel 4).
 */
CH_IRQ_HANDLER(DMA1_Ch4_IRQHandler) {
  UARTDriver *uartp;

  CH_IRQ_PROLOGUE();

  dmaClearChannel(STM32_DMA1, STM32_DMA_CHANNEL_4);
  uartp = &UARTD1;
  if (uartp->ud_rxstate == UART_RX_IDLE) {
    /* Fast IRQ path, this is why it is not centralized in serve_rx_end_irq().*/
    /* Receiver in idle state, a callback is generated, if enabled, for each
       received character and then the driver stays in the same state.*/
    if (uartp->ud_config->uc_rxchar != NULL)
      uartp->ud_config->uc_rxchar(uartp->ud_rxbuf);
  }
  else {
    /* Receiver in active state, a callback is generated, if enabled, after
       a completed transfer.*/
    dmaDisableChannel(STM32_DMA1, STM32_DMA_CHANNEL_4);
    serve_rx_end_irq(uartp);
  }

  CH_IRQ_EPILOGUE();
}

/**
 * @brief   USART1 TX DMA interrupt handler (channel 5).
 */
CH_IRQ_HANDLER(DMA1_Ch5_IRQHandler) {

  CH_IRQ_PROLOGUE();

  dmaClearChannel(STM32_DMA1, STM32_DMA_CHANNEL_5);
  dmaDisableChannel(STM32_DMA1, STM32_DMA_CHANNEL_5);
  serve_tx_end_irq(&UARTD1);

  CH_IRQ_EPILOGUE();
}

CH_IRQ_HANDLER(USART1_IRQHandler) {

  CH_IRQ_PROLOGUE();

  serve_usart_irq(&UARTD1);

  CH_IRQ_EPILOGUE();
}
#endif

/*===========================================================================*/
/* Driver exported functions.                                                */
/*===========================================================================*/

/**
 * @brief   Low level UART driver initialization.
 */
void uart_lld_init(void) {

#if STM32_UART_USE_USART1
  RCC->APB2RSTR     = RCC_APB2RSTR_USART1RST;
  RCC->APB2RSTR     = 0;
  uartObjectInit(&UARTD1);
  UARTD1.ud_usart   = USART1;
  UARTD1.ud_dmarx   = STM32_DMA_CHANNEL_4;
  UARTD1.ud_dmatx   = STM32_DMA_CHANNEL_5;
  UARTD1.ud_dmaccr  = 0;
#endif
}

/**
 * @brief   Configures and activates the UART peripheral.
 *
 * @param[in] uartp     pointer to the @p UARTDriver object
 */
void uart_lld_start(UARTDriver *uartp) {

  if (uartp->ud_state == UART_STOP) {
#if STM32_UART_USE_USART1
    if (&UARTD1 == uartp) {
      dmaEnable(DMA1_ID);   /* NOTE: Must be enabled before the IRQs.*/
      NVICEnableVector(USART1_IRQn,
                       CORTEX_PRIORITY_MASK(STM32_UART_USART1_IRQ_PRIORITY));
      NVICEnableVector(DMA1_Channel4_IRQn,
                       CORTEX_PRIORITY_MASK(STM32_UART_USART1_IRQ_PRIORITY));
      NVICEnableVector(DMA1_Channel5_IRQn,
                       CORTEX_PRIORITY_MASK(STM32_UART_USART1_IRQ_PRIORITY));
      RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    }
#endif

    /* Static DMA setup, the transfer size depends on the USART settings,
       it is 16 bits if M=1 and PCE=0 else it is 8 bits.*/
    uartp->ud_dmaccr      = STM32_UART_USART1_DMA_PRIORITY << 12;
    if ((uartp->ud_config->uc_cr1 & (USART_CR1_M | USART_CR1_PCE)) == USART_CR1_M)
      uartp->ud_dmaccr |= DMA_CCR1_MSIZE_0 | DMA_CCR1_PSIZE_0;
    uartp->ud_dmap->channels[uartp->ud_dmarx].CPAR = (uint32_t)&uartp->ud_usart->DR;
    uartp->ud_dmap->channels[uartp->ud_dmatx].CPAR = (uint32_t)&uartp->ud_usart->DR;
  }

  uartp->ud_rxstate = UART_RX_IDLE;
  uartp->ud_txstate = UART_TX_IDLE;
  usart_start(uartp);
}

/**
 * @brief   Deactivates the UART peripheral.
 *
 * @param[in] uartp     pointer to the @p UARTDriver object
 */
void uart_lld_stop(UARTDriver *uartp) {

  if (uartp->ud_state == UART_READY) {
    usart_stop(uartp);

#if STM32_UART_USE_USART1
    if (&UARTD1 == uartp) {
      NVICDisableVector(USART1_IRQn);
      NVICDisableVector(DMA1_Channel4_IRQn);
      NVICDisableVector(DMA1_Channel5_IRQn);
      dmaDisable(DMA1_ID);
      RCC->APB2ENR &= ~RCC_APB2ENR_USART1EN;
      return;
    }
#endif
  }
}

/**
 * @brief   Starts a transmission on the UART peripheral.
 * @note    The buffers are organized as uint8_t arrays for data sizes below
 *          or equal to 8 bits else it is organized as uint16_t arrays.
 *
 * @param[in] uartp     pointer to the @p UARTDriver object
 * @param[in] n         number of data frames to send
 * @param[in] txbuf     the pointer to the transmit buffer
 */
void uart_lld_start_send(UARTDriver *uartp, size_t n, const void *txbuf) {

}

/**
 * @brief   Stops any ongoing transmission.
 * @note    Stopping a transmission also suppresses the transmission callbacks.
 *
 * @param[in] uartp     pointer to the @p UARTDriver object
 */
void uart_lld_stop_send(UARTDriver *uartp) {

}

/**
 * @brief   Starts a receive operation on the UART peripheral.
 * @note    The buffers are organized as uint8_t arrays for data sizes below
 *          or equal to 8 bits else it is organized as uint16_t arrays.
 *
 * @param[in] uartp     pointer to the @p UARTDriver object
 * @param[in] n         number of data frames to send
 * @param[in] rxbuf     the pointer to the receive buffer
 */
void uart_lld_start_receive(UARTDriver *uartp, size_t n, void *rxbuf) {

}

/**
 * @brief   Stops any ongoing receive operation.
 * @note    Stopping a receive operation also suppresses the receive callbacks.
 *
 * @param[in] uartp     pointer to the @p UARTDriver object
 */
void uart_lld_stop_receive(UARTDriver *uartp) {

}

#endif /* CH_HAL_USE_UART */

/** @} */