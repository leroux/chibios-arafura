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

#include "ch.h"
#include "hal.h"

/*
 * Internal loopback mode, 500KBaud, automatic wakeup, automatic recover
 * from abort mode.
 * See section 22.7.7 on the STM32 reference manual.
 */
static const CANConfig cancfg = {
  CAN_MCR_ABOM | CAN_MCR_AWUM | CAN_MCR_TXFP,
  CAN_BTR_LBKM | CAN_BTR_SJW(0) | CAN_BTR_TS2(1) |
  CAN_BTR_TS1(8) | CAN_BTR_BRP(6),
  0,
  NULL
};

/*
 * Receiver thread.
 */
static WORKING_AREA(can_rx_wa, 512);
static msg_t can_rx(void *p) {
  EventListener el;
  CANRxFrame rxmsg;

  (void)p;
  chEvtRegister(&CAND1.cd_rxfull_event, &el, 0);
  while(!chThdShouldTerminate()) {
    if (chEvtWaitAnyTimeout(ALL_EVENTS, MS2ST(100)) == 0)
      continue;
    while (canReceive(&CAND1, &rxmsg, TIME_IMMEDIATE) == RDY_OK) {
      /* Process message.*/
      palTogglePad(IOPORT3, GPIOC_LED);
    }
  }
  chEvtUnregister(&CAND1.cd_rxfull_event, &el);
  return 0;
}

/*
 * Transmitter thread.
 */
static WORKING_AREA(can_tx_wa, 512);
static msg_t can_tx(void * p) {
  CANTxFrame txmsg;

  (void)p;
  txmsg.cf_IDE = CAN_IDE_EXT;
  txmsg.cf_EID = 0x01234567;
  txmsg.cf_RTR = CAN_RTR_DATA;
  txmsg.cf_DLC = 8;
  txmsg.cf_data32[0] = 0x55AA55AA;
  txmsg.cf_data32[1] = 0x00FF00FF;

  while (!chThdShouldTerminate()) {
    canTransmit(&CAND1, &txmsg, MS2ST(100));
    chThdSleepMilliseconds(500);
  }
  return 0;
}

/*
 * Entry point, note, the main() function is already a thread in the system
 * on entry.
 */
int main(int argc, char **argv) {

  (void)argc;
  (void)argv;

  /*
   * Activates the CAN driver 1.
   */
  canStart(&CAND1, &cancfg);

  /*
   * Starting the transmitter and receiver threads.
   */
  chThdCreateStatic(can_rx_wa, sizeof(can_rx_wa), NORMALPRIO + 7, can_rx, NULL);
  chThdCreateStatic(can_tx_wa, sizeof(can_tx_wa), NORMALPRIO + 7, can_tx, NULL);


  /*
   * Normal main() thread activity, in this demo it does nothing.
   */
  while (TRUE) {
    chThdSleepMilliseconds(500);
  }
  return 0;
}