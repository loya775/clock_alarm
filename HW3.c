
/*
 * Copyright (c) 2017, NXP Semiconductor, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * o Redistributions of source code must retain the above copyright notice, this list
 *   of conditions and the following disclaimer.
 *
 * o Redistributions in binary form must reproduce the above copyright notice, this
 *   list of conditions and the following disclaimer in the documentation and/or
 *   other materials provided with the distribution.
 *
 * o Neither the name of NXP Semiconductor, Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file    hw3.c
 * @brief   Application entry point.
 */
#include <stdio.h>
#include "board.h"
#include "peripherals.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "MK64F12.h"
#include "fsl_debug_console.h"
#include <stdlib.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "event_groups.h"
#include "queue.h"

//#define TYPE_A

#ifdef TYPE_A
#define PRODUCER_PRIORITY 		(configMAX_PRIORITIES-4)
#define CONSUMER_PRIORITY 		(configMAX_PRIORITIES-3)
#define SUPERVISOR_PRIORITY 	(configMAX_PRIORITIES-1)
#define PRINTER_PRIORITY 		(configMAX_PRIORITIES-2)
#define TIMER_PRIORITY 			(configMAX_PRIORITIES)
#else
#define PRODUCER_PRIORITY 		(configMAX_PRIORITIES-4)
#define CONSUMER_PRIORITY 		(configMAX_PRIORITIES-3)
#define SUPERVISOR_PRIORITY 	(configMAX_PRIORITIES-2)
#define PRINTER_PRIORITY 		(configMAX_PRIORITIES-5)
#define TIMER_PRIORITY 			(configMAX_PRIORITIES)
#endif

#define PRINTF_MIN_STACK (110)
#define GET_ARGS(args,type) *((type*)args)
#define EVENT_CONSUMER (1<<0)
#define EVENT_PRODUCER (1<<1)

typedef struct
{
	int8_t shared_memory;
	SemaphoreHandle_t seconds_signal;
	EventGroupHandle_t supervisor_signals;
	SemaphoreHandle_t serial_port_mutex;
	SemaphoreHandle_t shared_memory_mutex;
	SemaphoreHandle_t minutes_semaphore;
	SemaphoreHandle_t hours_semaphore;
	SemaphoreHandle_t seconds_semaphore;
	QueueHandle_t mailbox;
}task_args_t;

typedef enum {consumer_id,producer_id,supervisor_id} id_t;

typedef enum{seconds_type, minutes_type, hours_type} time_types_t;
typedef struct
{
	time_types_t time_type;
	uint8_t value;
}time_msg_t;

typedef struct
{
	time_types_t hour;
	time_types_t minute;
	time_types_t second;
}alarm_t;

typedef struct
{
	id_t id;
	uint32_t data;
	const char * msg;
}msg_t;

void task_producer(void*args);
void task_consumer(void*args);
void task_supervisor(void*args);
void task_printer(void*args);
void task_timer(void*args);

void task_producer(void*args)
{
	const char consumer_msg[] = "Horas";
	msg_t msg;
	msg.id = supervisor_id;
	msg.msg = consumer_msg;
	task_args_t task_args = GET_ARGS(args,task_args_t);
	alarm_t alarm;
	alarm.hour = 1;
	uint8_t hours = 0;
	for(;;)
	{
		xSemaphoreTake(task_args.hours_semaphore,portMAX_DELAY);
		hours++;
		if(hours == 24)
		{
			hours = 0;
		}
		if(hours == alarm.hour)
		{
			xEventGroupSetBits(task_args.supervisor_signals, EVENT_PRODUCER);
		}
		xQueueSend(task_args.mailbox,&msg,portMAX_DELAY);
		xSemaphoreGive(task_args.hours_semaphore);
		vTaskDelay(pdMS_TO_TICKS(10000));
	}
}

void task_consumer(void*args)
{
	const char producer_msg[] = "minutes";
	msg_t msg;
	msg.id = producer_id;
	msg.msg = producer_msg;
	TickType_t last_wake_time = xTaskGetTickCount();
	task_args_t task_args = GET_ARGS(args,task_args_t);
	alarm_t alarm;
	alarm.minute = 1;
	uint8_t minute = 0;
	for(;;)
	{
		if(minute == 0)
		{
			xSemaphoreTake(task_args.hours_semaphore,portMAX_DELAY);
		}
		xSemaphoreTake(task_args.minutes_semaphore,portMAX_DELAY);
		minute++;
		if(minute == 2)
		{
			minute = 0;
			xSemaphoreGive(task_args.hours_semaphore);
		}
		if(minute == alarm.minute)
		{
			xEventGroupSetBits(task_args.supervisor_signals, EVENT_PRODUCER);
		}
		xQueueSend(task_args.mailbox,&msg,portMAX_DELAY);
		xSemaphoreGive(task_args.minutes_semaphore);
		vTaskDelay(pdMS_TO_TICKS(5000));
	}
}


void task_printer(void*args)
{
	task_args_t task_args = GET_ARGS(args,task_args_t);
	msg_t received_msg;
	uint8_t seconds = 0;
	uint8_t minutes = 0;
	uint8_t hours = 0;
	for(;;)
	{
		xQueueReceive(task_args.mailbox,&received_msg,portMAX_DELAY);

		xSemaphoreTake(task_args.serial_port_mutex,portMAX_DELAY);
		switch(received_msg.id)
		{
		case producer_id:
			minutes += 1;
			if (minutes == 60)
			minutes=0;
			PRINTF(" %i:%i:%i\n",hours,minutes,seconds);
			break;
		case consumer_id:
			seconds += 1;
			if (seconds == 60)
			seconds = 0;
			PRINTF(" %i:%i:%i\n",hours,minutes,seconds);
			break;
		case supervisor_id:
			hours += 1;
			if (hours == 24)
			hours = 0;
			PRINTF(" %i:%i:%i\n",hours,minutes,seconds);
			break;
		default:
			PRINTF("\rError\n");
			break;
		}
		xSemaphoreGive(task_args.serial_port_mutex);

	}
}

void task_timer(void*args)
{
	uint32_t seconds  = 0;
	const char consumer_msg[] = "Seconds";
	msg_t msg;
	msg.id = consumer_id;
	msg.msg = consumer_msg;
	TickType_t last_wake_time = xTaskGetTickCount();
	task_args_t task_args = GET_ARGS(args,task_args_t);
	alarm_t alarm;
	alarm.second = 5;
	uint8_t min;
	min = alarm.minute;
	for(;;)
	{
		if(seconds ==0)
		{
			xSemaphoreTake(task_args.minutes_semaphore,portMAX_DELAY);
		}
		seconds++;
		if(seconds == 5)
		{
			seconds=0;
			xSemaphoreGive(task_args.minutes_semaphore);
		}
		if(seconds == alarm.second)
		{
			xEventGroupSetBits(task_args.supervisor_signals, EVENT_PRODUCER);
		}
		xQueueSend(task_args.mailbox,&msg,portMAX_DELAY);
		vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(1000));
	}
}

int main(void)
{
	static task_args_t args;
	args.supervisor_signals = xEventGroupCreate();
	args.seconds_signal = xSemaphoreCreateBinary();
	args.mailbox = xQueueCreate(5,sizeof(msg_t));
	args.serial_port_mutex = xSemaphoreCreateMutex();
	args.shared_memory_mutex = xSemaphoreCreateMutex();
	args.seconds_semaphore = xSemaphoreCreateMutex();
	args.minutes_semaphore = xSemaphoreCreateMutex();
	args.hours_semaphore = xSemaphoreCreateMutex();
	srand(0x15458523);

	BOARD_InitBootPins();
	BOARD_InitBootClocks();
	BOARD_InitBootPeripherals();
	BOARD_InitDebugConsole();

	xTaskCreate(task_producer, "producer", PRINTF_MIN_STACK, (void*)&args, PRODUCER_PRIORITY, NULL);
	xTaskCreate(task_consumer, "consumer", PRINTF_MIN_STACK, (void*)&args, CONSUMER_PRIORITY, NULL);
	//xTaskCreate(task_supervisor, "supervisor", PRINTF_MIN_STACK, (void*)&args, SUPERVISOR_PRIORITY, NULL);
	xTaskCreate(task_printer, "printer", PRINTF_MIN_STACK, (void*)&args, PRINTER_PRIORITY, NULL);
	xTaskCreate(task_timer, "timer", PRINTF_MIN_STACK, (void*)&args, TIMER_PRIORITY, NULL);
	vTaskStartScheduler();

	while(1) {}

	return 0 ;
}
