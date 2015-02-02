/*
 * Copyright (c) 2011, Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 *
 */

#include "contiki.h"
#include "lib/random.h"
#include "sys/ctimer.h"
#include "sys/etimer.h"
#include "net/uip.h"
#include "net/uip-ds6.h"
#include "net/uip-debug.h"

#include "sys/node-id.h"

#include "simple-udp.h"
#include "servreg-hack.h"

#include <stdio.h>
#include <string.h>

#include "lib/list.h"
#include "lib/memb.h"

#define UDP_PORT 1234
#define SERVICE_ID 190

#define UIP_REXMIT    0

//#define SEND_INTERVAL		(60 * CLOCK_SECOND)
//#define SEND_INTERVAL		(310 * CLOCK_SECOND)
#define SEND_INTERVAL		(450 * CLOCK_SECOND)
#define SEND_TIME		(random_rand() % (SEND_INTERVAL))

struct probeResult {
  struct probeResult *next;
  uip_ipaddr_t pAddr;
  uint8_t chNum;
  uint8_t rxValue;

  uint8_t checkAck;
};

LIST(probeResult_table);
MEMB(probeResult_mem, struct probeResult, 5);

uint8_t currentVal;
uint8_t changeTo;
static uip_ipaddr_t holdAddr;
uint8_t x;
uint8_t y;

uint8_t noOfEntry = 0;

  uip_ipaddr_t nextHopAddr;

uint8_t keepType;
extern uint8_t keepListNo = 0;

uint8_t sum;
uint8_t divide;

uip_ipaddr_t toParent;

static struct simple_udp_connection unicast_connection;

//the application specific event value
static process_event_t event_data_ready;

enum {
	CH_CHANGE,
	NBR_CH_CHANGE,
	STARTPROBE,
	NBRPROBE,
	PROBERESULT,
	CONFIRM_CH,
	GET_ACK
};

struct unicast_message {
	uint8_t type;
	uint8_t value;
	//uint8_t holdV;
	uint8_t value2;

	uip_ipaddr_t address;
	uip_ipaddr_t *addrPtr; 

	char paddingBuf[30];
};

/*---------------------------------------------------------------------------*/
PROCESS(unicast_sender_process, "Unicast sender example process");
PROCESS(test1, "test");
//PROCESS(test2, "test2");
AUTOSTART_PROCESSES(&unicast_sender_process, &test1);
//AUTOSTART_PROCESSES(&unicast_sender_process, &test1, &test2);
/*---------------------------------------------------------------------------*/
static void updateRoutingTable(uip_ipaddr_t *addr, uint8_t msgValue) {
  static uip_ds6_route_t *r;

  for(r = uip_ds6_route_head(); r != NULL; 
    r = uip_ds6_route_next(r)) {

    if(uip_ipaddr_cmp(uip_ds6_route_nexthop(r), addr)) {
      r->nbrCh = msgValue;

      /*printf("%d UPDATE OWN ROUTING TABLE ", msgValue);
      uip_debug_ipaddr_print(&r->ipaddr);
      printf(" via ");
      uip_debug_ipaddr_print(uip_ds6_route_nexthop(r));
      printf(" nbrCh %d\n", r->nbrCh);*/
    }
  }

  if(uip_ipaddr_cmp(addr, uip_ds6_defrt_choose())) {
    uip_ds6_defrt_setCh(msgValue);

    /*printf("%d PARENT UPDATE OWN ROUTING TABLE ", msgValue);
    uip_debug_ipaddr_print(uip_ds6_defrt_choose());
    printf(" nbrCh %d\n", uip_ds6_defrt_ch());*/
  }
}
/*---------------------------------------------------------------------------*/
static void removeProbe() {
  struct probeResult *pr;

  for(pr = list_head(probeResult_table); pr != NULL; pr = pr->next) {
    list_remove(probeResult_table, pr);
    memb_free(&probeResult_mem, pr);
  }
}
/*---------------------------------------------------------------------------*/
static void checkAckProbeResultTable() {
  struct probeResult *pr;
  struct unicast_message msg2;

  for(pr = list_head(probeResult_table); pr != NULL; pr = pr->next) {
    /*printf("%d: CHECKACK PROBE RESULT: ", cc2420_get_channel());
    uip_debug_ipaddr_print(&pr->pAddr);
    printf(" ");
    printf("CHANNEL %d PROBED %d ACK %d\n", pr->chNum, pr->rxValue, pr->checkAck);*/

    //! if checkAck == 0, do retransmission from CONFIRM_CH
    if((pr->checkAck) == 0) {
      if((sum/divide) >= ((sum/divide)/2)) {
        msg2.value = uip_ds6_if.addr_list[1].currentCh;
      }
      else {
	msg2.value = uip_ds6_if.addr_list[1].prevCh;
      }
      msg2.type = CONFIRM_CH;
      msg2.value2 = 0; //y==0, for() run only once in test1

      process_post(&test1, event_data_ready, &msg2);
      break;
    }
  }
}
/*---------------------------------------------------------------------------*/
static void readProbeResult() {
  struct probeResult *pr;
  struct unicast_message msg2;
  sum = 0;
  divide = 0;

  uip_ipaddr_t sendTo1;
  uip_ip6addr(&sendTo1, 0xaaaa, 0, 0, 0, 0x212, 0x7401, 0x0001, 0x0101);

  for(pr = list_head(probeResult_table); pr != NULL; pr = pr->next) {
    /*printf("%d: PROBE RESULT: ", cc2420_get_channel());
    uip_debug_ipaddr_print(&pr->pAddr);
    printf(" ");
    printf("CHANNEL %d PROBED %d ACK %d\n", pr->chNum, pr->rxValue, pr->checkAck);*/

    printf("Sending PROBERESULT to ");
    uip_debug_ipaddr_print(&sendTo1);
    printf("\n");

    msg2.type = PROBERESULT;
    msg2.address = pr->pAddr;
    msg2.value = pr->chNum;
    msg2.value2 = pr->rxValue; 

    //printf("READPROBERESULT SIZE %d\n\n", sizeof(msg2));
    simple_udp_sendto(&unicast_connection, &msg2, sizeof(msg2) + 1, &sendTo1);

//? SEND TO LPBR

    sum = sum + pr->rxValue;
    divide++;
  }
  keepListNo = divide;

  msg2.type = CONFIRM_CH;
  msg2.value2 = 0;
  if((sum/divide) >= ((sum/divide)/2)) {
    msg2.value = uip_ds6_if.addr_list[1].currentCh;
  }
  else {
    msg2.value = uip_ds6_if.addr_list[1].prevCh;
  }
  process_post(&test1, event_data_ready, &msg2);
}
/*---------------------------------------------------------------------------*/
static void keepProbeResult(const uip_ipaddr_t *prAddr, uint8_t chN, uint8_t getAck) {
  struct probeResult *pr;

  for(pr = list_head(probeResult_table); pr != NULL; pr = pr->next) {
    if(uip_ipaddr_cmp(prAddr, &pr->pAddr)) {
      if(chN == pr->chNum) {
	pr->rxValue = (pr->rxValue) + 1;
	return;
      }
      if(chN == 0) {
	pr->checkAck = getAck;
	return;
      }
      else {
	pr->checkAck = 0;
      }
    }
  }

  pr = memb_alloc(&probeResult_mem);
  if(pr != NULL) {
    pr->rxValue = 1;
    pr->chNum = chN;
    uip_ipaddr_copy(&pr->pAddr, prAddr);
    list_add(probeResult_table, pr);
  }
}
/*---------------------------------------------------------------------------*/

static void loopFunction(struct unicast_message *msg, uint8_t y, uint8_t x, uint8_t rNbrCh) {

  struct unicast_message msg2;
  struct probeResult *pr;
  //struct simple_udp_connection *c;

  msg2.value = msg->value;
  msg2.paddingBuf[30] = " ";
  msg2.addrPtr = msg->addrPtr;
  if(y == 1 && x == 0) {
    msg2.type = NBR_CH_CHANGE;
    printf("%d Sending channel change %d to tree neighbour ", rNbrCh, msg2.value);
  }
  else if(y == 1 && x == 1) {
    msg2.type = STARTPROBE;
    printf("%d Sending STARTPROBE %d to tree neighbour ", rNbrCh, msg2.value);
  }

  else if(y == 0 && x == 0) {
    for(pr = list_head(probeResult_table); pr != NULL; pr = pr->next) {
      //printf("%d: TO SEND CONFIRM CH: ", cc2420_get_channel());
      /*uip_debug_ipaddr_print(&pr->pAddr);
      printf(" ");
      printf("CHANNEL %d PROBED %d ACK %d\n", pr->chNum, pr->rxValue, pr->checkAck);*/

      //if(uip_ipaddr_cmp(&pr->pAddr, routeNextHop)) {
      if(uip_ipaddr_cmp(&pr->pAddr, msg2.addrPtr)) {
        //! if checkAck == 0, do retransmission from CONFIRM_CH
        if((pr->checkAck) == 0) {
          msg2.type = CONFIRM_CH;
          printf("CONFIRM CH SENDING!!");
        }
      }
    }
  }

  keepType = msg2.type;
//!	    cc2420_set_channel(r->nbrCh);
  uip_debug_ipaddr_print(msg2.addrPtr);
  //uip_debug_ipaddr_print(routeNextHop);
  printf("\n");	

  //msg2.addrPtr = routeNextHop;

  //? change to the neighbour channel cc2420_set_channel(r->nbrCh)
  simple_udp_sendto(&unicast_connection, &msg2, sizeof(msg2), msg2.addrPtr);
}
/*---------------------------------------------------------------------------*/
static void loopFunction2(struct unicast_message *msg, uint8_t y, uint8_t x, uint8_t rNbrCh) {

printf("LOOPFUNCTION2\n\n");
  struct unicast_message msg2;
  struct probeResult *pr;
  //struct simple_udp_connection *c;

  msg2.value = msg->value;
  msg2.paddingBuf[30] = " ";
  msg2.addrPtr = msg->addrPtr;
  if(y == 1 && x == 0) {
    msg2.type = NBR_CH_CHANGE;
    printf("%d Sending channel change %d to tree neighbour2 ", rNbrCh, msg2.value);
  }
  else if(y == 1 && x == 1) {
    msg2.type = STARTPROBE;
    printf("%d Sending STARTPROBE %d to tree neighbour2 ", rNbrCh, msg2.value);
  }

  else if(y == 0 && x == 0) {
    for(pr = list_head(probeResult_table); pr != NULL; pr = pr->next) {
      //printf("%d: TO SEND CONFIRM CH: ", cc2420_get_channel());
      /*uip_debug_ipaddr_print(&pr->pAddr);
      printf(" ");
      printf("CHANNEL %d PROBED %d ACK %d\n", pr->chNum, pr->rxValue, pr->checkAck);*/

      //if(uip_ipaddr_cmp(&pr->pAddr, routeNextHop)) {
      if(uip_ipaddr_cmp(&pr->pAddr, msg2.addrPtr)) {
        //! if checkAck == 0, do retransmission from CONFIRM_CH
        if((pr->checkAck) == 0) {
          msg2.type = CONFIRM_CH;
          printf("CONFIRM CH SENDING!!");
        }
      }
    }
  }

  keepType = msg2.type;
//!	    cc2420_set_channel(r->nbrCh);
  uip_debug_ipaddr_print(msg2.addrPtr);
  //uip_debug_ipaddr_print(routeNextHop);
  printf("\n");	

  //msg2.addrPtr = routeNextHop;

  //? change to the neighbour channel cc2420_set_channel(r->nbrCh)
  simple_udp_sendto(&unicast_connection, &msg2, sizeof(msg2), msg2.addrPtr);
}
/*---------------------------------------------------------------------------*/
static void
receiver(struct simple_udp_connection *c,
         const uip_ipaddr_t *sender_addr,
         uint16_t sender_port,
         const uip_ipaddr_t *receiver_addr,
         uint16_t receiver_port,
         const uint8_t *data,
         uint16_t datalen)
{
  struct unicast_message *msg;
  struct unicast_message msg2;
  msg = data;

  struct probeResult *pr;

  //static uip_ds6_route_t *r;

  //uip_ipaddr_t sendTo1;
  //uip_ip6addr(&sendTo1, 0xaaaa, 0, 0, 0, 0x212, 0x7401, 0x0001, 0x0101);

  if(msg->type == CH_CHANGE) {
    printf("%d: %d received CH_CHANGE from ", cc2420_get_channel(), msg->value);
    uip_debug_ipaddr_print(sender_addr);
    printf("\n");  

    msg2.type = NBR_CH_CHANGE;
    msg2.value = msg->value;

    msg2.value2 = 1;

    process_post_synch(&test1, event_data_ready, &msg2);
  }//end if(msg->type == CH_CHANGE)

  else if(msg->type == NBR_CH_CHANGE) {
    printf("%d: %d received NBR_CH_CHANGE from ", cc2420_get_channel(), msg->value);
    uip_debug_ipaddr_print(sender_addr);
    printf("\n");

    msg2.value = msg->value;
    msg2.addrPtr = sender_addr;

    //? updates the routing table r->nbrCh = msg->value;
    updateRoutingTable(msg2.addrPtr, msg2.value);
  }//end if(msg->type == NBR_CH_CHANGE)

  else if(msg->type == STARTPROBE) {
    //printf("RECEIVED STARTPROBE\n\n");

    msg2.type = NBRPROBE;
    //msg2.value = msg->value;
    msg2.addrPtr = sender_addr;

    process_post_synch(&test1, event_data_ready, &msg2);
    //process_post_synch(&unicast_sender_process, event_data_ready, &msg2);
  }

  else if(msg->type == NBRPROBE) {
    printf("%d: %d received %d NBRPROBE from ", cc2420_get_channel(), msg->value, msg->value2);
    uip_debug_ipaddr_print(sender_addr);
    printf("\n");

    keepProbeResult(sender_addr, msg->value, 0);
  }

  else if(msg->type == CONFIRM_CH) {
    printf("%d: Received CONFIRM_CH from ", cc2420_get_channel());
    uip_debug_ipaddr_print(sender_addr);
    printf("\n");
 
    msg2.type = GET_ACK;
    msg2.addrPtr = sender_addr;
    msg2.value = msg->value;

    process_post_synch(&test1, event_data_ready, &msg2);
  }

  else if(msg->type == GET_ACK) {
    msg2.value = msg->value;
    printf("%d: GET ACK BACK %d\n", cc2420_get_channel(), keepListNo);

    keepProbeResult(sender_addr, 0, 1);
  }

  else {
  printf("Data received on port %d from port %d with length %d\n",
         receiver_port, sender_port, datalen);
  }
}
/*---------------------------------------------------------------------------*/
static void
set_global_address(void)
{
  uip_ipaddr_t ipaddr;
  int i;
  uint8_t state;

  uip_ip6addr(&ipaddr, 0xaaaa, 0, 0, 0, 0, 0, 0, 0);
  uip_ds6_set_addr_iid(&ipaddr, &uip_lladdr);
  uip_ds6_addr_add(&ipaddr, 0, ADDR_AUTOCONF);

  printf("IPv6 addresses: ");
  for(i = 0; i < UIP_DS6_ADDR_NB; i++) {
    state = uip_ds6_if.addr_list[i].state;
    if(uip_ds6_if.addr_list[i].isused &&
       (state == ADDR_TENTATIVE || state == ADDR_PREFERRED)) {
      uip_debug_ipaddr_print(&uip_ds6_if.addr_list[i].ipaddr);
      printf("\n");
    }
  }
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(unicast_sender_process, ev, data)
{
  static struct etimer periodic_timer;
  static struct etimer send_timer;
  uip_ipaddr_t *addr;

  uip_ipaddr_t sendTo1;
  uip_ip6addr(&sendTo1, 0xaaaa, 0, 0, 0, 0x212, 0x7401, 0x0001, 0x0101);

  static uip_ds6_route_t *r;

  struct unicast_message *msg;
  struct unicast_message msg2;
  msg = data;

  static struct etimer time;

  PROCESS_BEGIN();

  servreg_hack_init();

  set_global_address();

  simple_udp_register(&unicast_connection, UDP_PORT,
                      NULL, UDP_PORT, receiver);

  etimer_set(&periodic_timer, SEND_INTERVAL);
  while(1) {
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));
    etimer_reset(&periodic_timer);
    etimer_set(&send_timer, SEND_TIME);

    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&send_timer));
    addr = servreg_hack_lookup(SERVICE_ID);

      static unsigned int message_number;
      char buf[20];

      //readProbe();

  /*struct probeResult *pr;

  for(pr = list_head(probeResult_table); pr != NULL; pr = pr->next) {
    printf("BEFORE SENDING %d READPROBE pktRx %d, chNum %d, ip ", pr->checkAck, pr->rxValue, pr->chNum);
    uip_debug_ipaddr_print(&pr->pAddr);
    printf("\n");
  }*/

      //uip_debug_ipaddr_print(&((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])->destipaddr);
      //uip_debug_ipaddr_print(&((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])->srcipaddr);

      printf("Sending unicast to ");
      uip_debug_ipaddr_print(&sendTo1);
      printf("\n");
      sprintf(buf, "Message %d", message_number);
      message_number++;
      simple_udp_sendto(&unicast_connection, buf, strlen(buf) + 1, &sendTo1);

      //? to be called after sending the PROBE INFO to LPBR - empty the list
      //? QUICK HACK
      //for(q = 1; q <= 3; q++) {
        //removeProbe();
      //}
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(test1, ev, data)
{
  static struct etimer time;
  struct unicast_message *msg;
  struct unicast_message msg2;
  msg = data;

  static uip_ds6_route_t *r;

  uip_ipaddr_t sendTo1;
  uip_ip6addr(&sendTo1, 0xfe80, 0, 0, 0, 0x212, 0x7401, 0x0001, 0x0101);

  uip_ipaddr_t sendTo1G;
  uip_ip6addr(&sendTo1G, 0xaaaa, 0, 0, 0, 0x212, 0x7401, 0x0001, 0x0101);

  static struct ctimer timer;
  struct probeResult *pr;

uint8_t newVal;
uip_ipaddr_t toParent;

  PROCESS_BEGIN();

  while(1) {
    PROCESS_WAIT_EVENT_UNTIL(ev == event_data_ready);

    if(msg->type == NBR_CH_CHANGE || msg->type == STARTPROBE || msg->type == CONFIRM_CH) {

      msg2.type = msg->type;
      msg2.value = msg->value;
      changeTo = msg2.value;
      keepType = msg2.type;

      msg2.value2 = msg->value2;
      y = msg2.value2;

      //! for padding as shortest packet size is 43 bytes (defined in contikimac.c)
      msg2.paddingBuf[30] = " ";

      printf("VALUE2 IS %d\n\n", y);

      for(x = 0; x <=y; x++) {
        //! sending to ALL TREE NBR and PARENT should be done within 1 seconds maximum
        for(r = uip_ds6_route_head(); r != NULL; 
	  r = uip_ds6_route_next(r)) {

	  //! check to ensure it doesn't repeat the same nexthop neighbour
	  if(!uip_ipaddr_cmp(&nextHopAddr, uip_ds6_route_nexthop(r))) {

            msg2.value = changeTo;
            msg2.paddingBuf[30] = " ";
	    msg2.addrPtr = uip_ds6_route_nexthop(r);
            uip_ipaddr_copy(&nextHopAddr, uip_ds6_route_nexthop(r));
            loopFunction2(&msg2, y, x, r->nbrCh);

	    if((y == 1 && x == 0)) {
	      etimer_set(&time, 0.15 * CLOCK_SECOND);
	    }
	    else if(y == 1 && x == 1) {
	      etimer_set(&time, 1 * CLOCK_SECOND);
	    }
	    else if(y == 0 && x == 0) {
	      etimer_set(&time, 0.5 * CLOCK_SECOND);
	    }
	    PROCESS_YIELD_UNTIL(etimer_expired(&time));

            printf("AFTER 0.15 OR 1\n\n");

    	    //uip_ds6_if.addr_list[1].prevCh = cc2420_get_channel();	
    	    uip_ds6_if.addr_list[1].currentCh = changeTo;
	    //cc2420_set_channel(uip_ds6_if.addr_list[1].currentCh);
	  }//END IF

        }//END RT
      }//END FOR X==1

printf("xxVALUE2 IS %d\n\n", y);
      for(x = 0; x <=y; x++) {
        if(!uip_ipaddr_cmp(uip_ds6_defrt_choose(), &sendTo1)) {
	  uip_ipaddr_copy(&toParent, uip_ds6_defrt_choose());
	  msg2.addrPtr = &toParent;
            msg2.paddingBuf[30] = " ";
	  /*printf("PARENT ");
	  uip_debug_ipaddr_print(uip_ds6_defrt_choose());
	  printf(" copied ");
	  uip_debug_ipaddr_print(msg2.addrPtr);
	  uip_debug_ipaddr_print(&toParent);
	  printf("\n\n");*/

	  newVal = uip_ds6_defrt_ch();
	  msg2.value = changeTo;
          loopFunction(&msg2, y, x, newVal);

	  if((y == 1 && x == 0)) {
	  //if(x == 0) {
printf("\n\nSTART ETIMER\n\n");
	    etimer_set(&time, 0.15 * CLOCK_SECOND);
	  }
	  else if(y == 1 && x == 1) {
printf("\n\nSTART ETIMER\n\n");
	      etimer_set(&time, 1 * CLOCK_SECOND);
	  }
	  else if(y == 0 && x == 0) {
	    //printf("START 1\n\n");
printf("\n\nSTART ETIMER\n\n");
	    etimer_set(&time, 0.5 * CLOCK_SECOND);
	  }
	  PROCESS_YIELD_UNTIL(etimer_expired(&time));

          printf("AFTER 0.15 OR 1\n\n");
    	  uip_ds6_if.addr_list[1].currentCh = changeTo;
	  //cc2420_set_channel(uip_ds6_if.addr_list[1].currentCh);

        }//END IF

      }//END FOR X==1

      if(keepType == NBR_CH_CHANGE || keepType == STARTPROBE) {
        readProbeResult();
      }

      if(keepType == CONFIRM_CH) {
	printf("\n\nFINISH CONFIRM_CH\n\n");
	checkAckProbeResultTable();
      }
    }

    if(msg->type == NBRPROBE) {

      msg2.type = NBRPROBE;
      msg2.addrPtr = msg->addrPtr;
      msg2.value = msg->value;

      changeTo = msg2.value;
      uip_ipaddr_copy(&holdAddr, msg2.addrPtr);

      printf("IN NBRPROBE\n\n");
      etimer_set(&time, 0.125 * CLOCK_SECOND);
      PROCESS_YIELD_UNTIL(etimer_expired(&time));

      //printf("AFTER 0.125s\n\n");
      y = 1;
      //! for padding as shortest packet size is 43 bytes (defined in contikimac.c)
      msg2.paddingBuf[30] = " ";
      for(x = 1; x <= 8; x++) {
      msg2.type = NBRPROBE;
	msg2.value = changeTo;
	msg2.value2 = y;
	msg2.addrPtr = &holdAddr;
        msg2.paddingBuf[30] = " ";

        //!cc2420_set_channel(msg2.value);
 	printf("%d %d Sending %d NBRPROBE %d to sender ", cc2420_get_channel(), sizeof(msg2), msg2.value2, msg2.value);
	uip_debug_ipaddr_print(msg2.addrPtr);
	printf("\n");

	y++;
	simple_udp_sendto(&unicast_connection, &msg2, sizeof(msg2), msg2.addrPtr);
      }
      //! the last few packets might be still be sending
      //!cc2420_set_channel(changeTo);
      etimer_set(&time, 0.15 * CLOCK_SECOND);
      //etimer_set(&time, 1 * CLOCK_SECOND);
      PROCESS_YIELD_UNTIL(etimer_expired(&time));
    }//end if(msg->type == NBRPROBE)

    if(msg->type == GET_ACK) {
      
      msg2.addrPtr = msg->addrPtr;
      uip_ipaddr_copy(&holdAddr, msg2.addrPtr);
      msg2.value = msg->value;

      changeTo = msg2.value;

      updateRoutingTable(msg2.addrPtr, msg2.value);

      //printf("SET 0.15S TIMER\n");
      etimer_set(&time, 0.15 * CLOCK_SECOND);
      PROCESS_YIELD_UNTIL(etimer_expired(&time));

      msg2.type = GET_ACK;
      msg2.addrPtr = &holdAddr;

      msg2.value = changeTo;

      //!cc2420_set_channel(msg2.value);
      printf("Sending GET_ACK back ");
      //cc2420_set_channel(uip_ds6_defrt_ch());
      uip_debug_ipaddr_print(msg2.addrPtr);
      printf("\n");

      simple_udp_sendto(&unicast_connection, &msg2, sizeof(msg2), msg2.addrPtr);
    }
  }//end while(1)

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
