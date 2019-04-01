/*
 * Copyright (C) 2015 - 2018, IBEROXARXA SERVICIOS INTEGRALES, S.L.
 * Copyright (C) 2015 - 2018, Jaume Olivé Petrus (jolive@whitecatboard.org)
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the <organization> nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *     * The WHITECAT logotype cannot be changed, you can remove it, but you
 *       cannot change it in any way. The WHITECAT logotype is:
 *
 *          /\       /\
 *         /  \_____/  \
 *        /_____________\
 *        W H I T E C A T
 *
 *     * Redistributions in binary form must retain all copyright notices printed
 *       to any local or remote output device. This include any reference to
 *       Lua RTOS, whitecatboard.org, Lua, and other copyright notices that may
 *       appear in the future.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Lua RTOS, PING))) #28015 sensor (Distance Sensor)
 */

#include "luartos.h"

#if CONFIG_LUA_RTOS_LUA_USE_SENSOR
#if CONFIG_LUA_RTOS_USE_SENSOR_PING28015

#include "freertos/FreeRTOS.h"

#include <math.h>
#include <string.h>

#include <sys/delay.h>
#include <sys/driver.h>

#include <drivers/sensor.h>
#include <drivers/gpio.h>
#include <drivers/rmt.h>

driver_error_t *ping28015_setup(sensor_instance_t *unit);
driver_error_t *ping28015_acquire(sensor_instance_t *unit, sensor_value_t *values);
driver_error_t *ping28015_set(sensor_instance_t *unit, const char *id, sensor_value_t *setting);

// Sensor specification and registration
static const sensor_t __attribute__((used,unused,section(".sensors"))) ping28015_sensor = {
	.id = "PING28015",
	.interface = {
		{.type = GPIO_INTERFACE},
	},
	.data = {
		{.id = "distance", .type = SENSOR_DATA_DOUBLE},
	},
	.interface_name = {"SIG"},
	.properties = {
		{.id = "calibration", .type = SENSOR_DATA_DOUBLE},
		{.id = "temperature", .type = SENSOR_DATA_DOUBLE},
	},
	.setup = ping28015_setup,
	.acquire = ping28015_acquire,
	.set = ping28015_set
};

/*
 * Operation functions
 */
driver_error_t *ping28015_setup(sensor_instance_t *unit) {
	// Set default calibration value
	unit->properties[0].doubled.value = 0;

	// Set temperature to 20 ºC if no current temperature is provided
	unit->properties[1].doubled.value = 20;

    // By default, use bit bang implementation
    unit->args = (void *)0xffffffff;

	// The preferred implementation uses the RMT to avoid disabling interrupts during
	// the acquire process. If there are not RMT channels available, use the bit bang
	// implementation.
	driver_error_t *error;
#if 0
    int rmt_device;

    error = rmt_setup_tx(unit->setup[0].gpio.gpio, RMTPulseRangeUSEC, RMTIdleZ, NULL, &rmt_device);
    if (!error) {
        error = rmt_setup_rx(unit->setup[0].gpio.gpio, RMTPulseRangeUSEC, 10, 22000, &rmt_device);
        if (!error) {
			// Use RMT implementation
			unit->args = (void *)((uint32_t)rmt_device);
        } else {
        	free(error);
        }
    } else {
    	free(error);
    }
#endif

    // The initial status of the signal line must be low
    gpio_pin_output(unit->setup[0].gpio.gpio);
    gpio_pin_clr(unit->setup[0].gpio.gpio);

	// Ignore some measures
	sensor_value_t tmp[2];
	int i;

	for(i = 0;i < 2;i++) {
		error = ping28015_acquire(unit, tmp);
		if (error) {
			free(error);
		}

		udelay(200);
	}

	return NULL;
}

driver_error_t *ping28015_set(sensor_instance_t *unit, const char *id, sensor_value_t *setting) {
	if (strcmp(id,"calibration") == 0) {
		memcpy(&unit->properties[0], setting, sizeof(sensor_value_t));
	} else if (strcmp(id,"temperature") == 0) {
		memcpy(&unit->properties[1], setting, sizeof(sensor_value_t));
	}

	return NULL;
}

driver_error_t *ping28015_acquire(sensor_instance_t *unit, sensor_value_t *values) {
    int t = 0; // Echo pulse duration in usecs

    if ((uint32_t)unit->args != 0xffffffff) {
    	// Use RMT
    	driver_error_t *error;
    	rmt_item_t *item;

    	// First send, request pulse
    	rmt_item_t buffer[41];

    	buffer[0].level0 = 0;
    	buffer[0].duration0 = 5;
    	buffer[0].level1 = 1;
    	buffer[0].duration1 = 5;

    	error = rmt_tx_rx((int)unit->args, buffer, 1, buffer, 1, 22000);
    	if (!error) {
    		item = buffer;

			if (item->level0 == 0) {
				t = item->duration1;
			} else {
				t = item->duration0;
			}
    	} else {
    		free(error);

    		return driver_error(SENSOR_DRIVER, SENSOR_ERR_TIMEOUT, NULL);
    	}
    } else {
    	// Use bit bang

    	// Configure pin as output
    	gpio_pin_output(unit->setup[0].gpio.gpio);
        gpio_pin_clr(unit->setup[0].gpio.gpio);

    	// Trigger pulse
    	portDISABLE_INTERRUPTS();

    	gpio_pin_set(unit->setup[0].gpio.gpio);
    	udelay(5);
    	gpio_pin_clr(unit->setup[0].gpio.gpio);

    	// Configure pin as input
    	gpio_pin_input(unit->setup[0].gpio.gpio);

    	// Get echo pulse width in usecs
    	t = gpio_get_pulse_time(unit->setup[0].gpio.gpio, 1, 22000);
    	if (t < 0) {
    		portENABLE_INTERRUPTS();

    		return driver_error(SENSOR_DRIVER, SENSOR_ERR_TIMEOUT, NULL);
    	}

    	portENABLE_INTERRUPTS();
    }

	/*
	 * Calculate distance
	 *
	 * Sound speed depends on ambient temperature
	 * sound speed = 331.5 + (0.6 * temperature) m/sec
	 * sound speed = 331500 + (60 * temperature) mm/sec
	 *
	 */

	// First calculate mm per usec
	double mm_per_usecs = (331500.0 + (60.0 * (unit->properties[1].doubled.value))) / 1000000.0;

	// Calculate distance in centimeters.
	// Please, take note that the width of the echo is the time that the ultrasonic pulse takes to travel
	// from the sensor to the object, plus the time to back to the sensor, so we have to consider time / 2.
	// 1 decimal precision.
	if (t == 0) {
		values[0].doubled.value = 2;
	} else {
		values[0].doubled.value =
			  round(((((((double)t / 2.0)) * mm_per_usecs) / 10.0 +
			  unit->properties[0].doubled.value)) * 10.00) / 10.00;
	}

	// Next value can get in 200 useconds
	gettimeofday(&unit->next, NULL);
	unit->next.tv_usec += 200;

	return NULL;
}

#endif
#endif
