/* channel.cpp - Implements an oscilloscope channel resource
 * Copyright (C) 2011 Antoine Mercier-Linteau
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "channel.h"
#include <utils/template.h>
#include <utils/memfile.h>
#include <utils/pgmspace_file.h>
#include <string.h>
#include <avr/io.h>
#include <stdlib.h>
#include <avr_pal.h>

/// Oscilloscope channels are stored in this array to be accessed by the ISR.
static Channel* instances[NUMBER_OF_CHANNELS];

Channel::Channel(uint8_t number):
	Resource(),
	// Initialize as if we already have samples stored.
	sample_ptr(extra_space + 1),
	trigger_flags(0),
	trigger_level(128)
{
	// Store of reference to this channel for acces by the ISR.
	instances[number - 1] = this;

	// If this is the last channel being created.
	if(number ==  NUMBER_OF_CHANNELS)
	{
		/*The ADC configuration is only done once and by the last channel that gets
		 * instantiated. This ensure that no inconsistent state occur due to
		 * a read-modify-write while a conversion is running.*/

		ADMUX = _BV(REFS0) +  // Using AVcc with external capacitor at AREF pin.
				_BV(ADLAR) + // Left adjustment for 8 bit precision necessary.
				NUMBER_OF_CHANNELS - 1; // Conversion starts with the last channel.

		ADCSRA = _BV(ADEN) +  // Enables the ADC.
				_BV(ADSC) +  // Start a conversion.
				_BV(ADATE) +  // Auto-trigger enabled.
				_BV(ADIE) +  // Interrupt enable.
				// Prescaler at 128 (See sampling_rate below).
				_BV(ADPS2) + _BV(ADPS1) + _BV(ADPS0);
	}
	 // Value derived from instructions below.
	sampling_rate = 9616 / NUMBER_OF_CHANNELS;

	/*An ADC conversion takes 13 cycles by default, here is a list giving the conversion rates
	 * with F_CPU = 16 MHz
	 * Division Factor = 2, 26 cycles per conversion, 615384 conversion per second
	 * Division Factor = 4, 52 cycles per conversion, 307692 conversion per second
	 * Division Factor = 8, 104 cycles per conversion, 153846 conversion per second
	 * Division Factor = 16, 208 cycles per conversion, 76923 conversion per second
	 * Division Factor = 32, 416 cycles per conversion, 38461 conversion per second
	 * Division Factor = 64, 832 cycles per conversion, 19230 conversion per second
	 * Division Factor = 128, 1664 cycles per conversion, 9615 conversion per second
	 *
	 * Since we are alternating between channels, the sampling rate is also affected
	 * by the number of active channels at any given time.
	 *
	 * This is in free running mode. To get a precise conversion rate, we should be
	 * starting the conversion on a timer. It is also important to note that prescalers
	 * 2, 4 are too fast for the firmware to process and that prescaler 8 would be a
	 * severe hit on performance. This could be mitigated by doing sample storing
	 * in assembly or using very optimized static code.
	 * */

	// Nothing to set in ADCSRB.

	// Disable the digital input on the ADC pin.
	/// TODO the pin should change with the channel.
	DIDR0 = _BV(ADC0D);

	VERBOSE_PRINTLN_P("Channel ready...");

}

/// The template that contains the parameters.
#define CONTENT "{\"sr\":~,\"tf\":~,\"tl\":~}"

/// The length of the template string.
#define CONTENT_SIZE sizeof(CONTENT) - 1

/// The parameter template stored in program memory.
static const char content_P[] PROGMEM = CONTENT;

File* Channel::get_params(void)
{
	// Create a file to hold the parameter program memory string..
	File* f = new PGMSpaceFile(content_P, CONTENT_SIZE);

	if(!f) // If there was no memory left for the file.
	{
		return NULL; // Cannot proceed.
	}

	Template* t = new Template(f); // Create a template.

	if(!t) // If there was no memory left for the template.
	{
		delete f; // Delete the file.
		return NULL; // Cannot proceed.
	}

	// Add the template arguments.
	t->add_narg(sampling_rate);
	t->add_narg(trigger_flags);
	t->add_narg(trigger_level);

	return t;
}

Response::status_code Channel::process(Request* request, Response* response)
{
	if(!request->to_destination()) // If the request is at destination.
	{
			if(request->is_method(Request::GET)) // If this is a GET request.
			{
				if(sample_ptr > sample_size) // If a sample is ready.
				{
					File* body = get_sample(); // Retrieve the sample.

					// If there was not enough memory to allocated the file.
					if(!body)
					{
						// Not enough resources to process the request.
						return SERVICE_UNAVAILABLE_503;
					}

					// Set the sample file as the response's body.
					response->set_body(body, MIME::APPLICATION_OCTET_STREAM);

					VERBOSE_PRINTLN_P("Sample ready");

					return OK_200;
				}
				else // If there is no sample ready yet.
				{
					// Queue the request to process it at a later time.
					if(queue.queue(request))
					{
						return SERVICE_UNAVAILABLE_503; // Queue is full.
					}

					VERBOSE_PRINTLN_P("Sample not ready ");

					schedule(1); // Run the resource in 1 ms.

					return RESPONSE_DELAYED_102;
				}
			}

			return NOT_IMPLEMENTED_501; // Request method not implemented.

	}
	/* If the request is one resource before destionation, it might refer to
	 * sub resources of the channel. */
	else if(request->to_destination() == 1)
	{
		request->next(); // Go to the next resource.

		// If the request is for the parameters resource.
		if(!strcmp(request->current(), "pr"))
		{
			if(request->is_method(Request::POST)) // If it is a POST request.
			{
				char buffer[8]; // A buffer to store form data.

				// Find an argument named sr (sampling rate).
				uint8_t len = request->find_arg("sr", buffer, 7);

				if(len) // If there is an argument for the sampling rate.
				{
					buffer[len] = '\0'; // Terminate the string.
					// Convert it to an integer and set it.
					sampling_rate = atoi(buffer);
				}

				// Find an argument named tl (trigger level).
				len = request->find_arg("tl", buffer, 7);

				if(len) // If there is an argument for the trigger level.
				{
					buffer[len] = '\0'; // Terminate the string.
					// Convert it to an integer and set it.
					ATOMIC { trigger_level = atoi(buffer); }
				}

				// Find an argument named tf (trigger flags).
				len = request->find_arg("tf", buffer, 7);

				if(len) // If there is an argument for the trigger flags.
				{
					buffer[len] = '\0'; // Terminate the string.
					// Convert it to an integer and set it.
					ATOMIC { trigger_flags = atoi(buffer); }
				}

				goto get; // Proceed the rest of the request like a GET.
			}
			// If this ia GET request.
			else if(request->is_method(Request::GET))
			{
				get:

				File* body = get_params(); // Get the parameters for this channel.

				/* If there was not enough memory to allocate a file to hold
				 * the parameters JSON array. */
				if(!body)
				{
					// Not enough resources to process the request.
					return SERVICE_UNAVAILABLE_503;
				}

				// Set the parameters file as the response body.
				response->set_body(body, MIME::APPLICATION_JSON);

				return OK_200;
			}

			return NOT_IMPLEMENTED_501; // Request method not implemented.
		}

		/* This url cannot be processed, pass it to child resources. Because
		 * we jumped to the next resource earlier, the request url needs to be
		 * rewinded.*/
		request->previous();
	}

	return PASS_308; // Cannot process this request.
}

File* Channel::get_sample(void)
{
	// Allocate a buffer to hold the current sample.
	char* sample = (char*)ts_malloc(sample_size);

	if(!ssample) // If the buffer could not be allocated.
	{
		return NULL; // Not enough memory to proceed.
	}

	ATOMIC
	{
		// Copy the statically allocated buffer into the newly allocated one.
		memcpy(sample, sample_buffer, sample_size);
	}

	// Wrap the sample into a file.
	MemFile* f = new MemFile(sample, sample_size, false);

	if(!f) // If the file could not be allocated.
	{
		ts_free(s); // The sample is lost.
		// return NULL; // f will be null.
		// Not enough memory to proceed.
	}

	return f;
}


void Channel::run(void)
{
	Request* request = NULL;

	VERBOSE_PRINTLN_P("Channel run ");

	/* Remove the requests that have time out from the queue an get to the
	 * request to which the sample needs to be sent.*/
	while(true)
	{
		///TODO move the next line into the loop condition.

		request = queue.peek(); // Is there a request waiting for a sample?

		if(!request) // If there is no waiting request.
		{
			break; // Nothing else to do.
		}

		// If the request has timed out.
		if(request->age + max_request_age < get_uptime())
		{
			VERBOSE_PRINTLN_P("Request too old");

			request = queue.dequeue(); // Remove the request from the queue.

			// Craft a response to inform the client his request has timed out.
			Response* response = new Response(REQUEST_TIMEOUT_408, request);
			if(!response) // If the response could no be allocated.
			{
				delete request; // Get rid of the request.

				// Nothing more we can do, let the client time out at its end.
				return;
			}

			dispatch(response); // Dispatch the response to the client.
		}
		else // Process that request.
		{
			VERBOSE_PRINTLN_P("No Request too old");
			break; // Keep the current requests in the queue.
		}
	}
	// If there is a request waiting for a sample and one is ready.
	if(request && trigger_flags & DONE_SAMPLE)
	{
		VERBOSE_PRINTLN_P("Got data!");

		while(true) // Process all requests in the queue.
		{
			request = queue.dequeue(); // Dequeue the oldest request.

			if(!request) // If there are no more requests in the queue.
			{
				break;
			}
			// Craft a response that will contain the sample.
			Response* response = new Response(OK_200, request);

			if(!response) // If the response could not be allocated.
			{
				delete request; // Drop the request and let the client time out.
				break; // Cannot proceed.

				/* Do not empty the queue, maybe there will be resources available
				 * during the next run to process the next request. */
			}

			File* sample = get_sample(); // Get the current sample.
			if(!sample) // If the sample file could no be allocated.
			{
				// Not enough memory to get the sample.
				response->response_code_int = SERVICE_UNAVAILABLE_503;
			}
			else
			{
				// Set the sample as the body.
				response->set_body(sample, MIME::APPLICATION_OCTET_STREAM);
			}

			dispatch(response); // Dispatch the response to the client.
		}
	}
	else if(request) // If a request is waiting for a sample but none is ready.
	{
		schedule(1) // Process this resource again in 1 ms.

		return; // Done.
	}

	schedule(NEVER); // No requests waiting for a sample.
}

void Channel::store_sample(uint8_t sample)
{
	// Have to save the sample here to compare it with trigger.
	sample_buffer[sample_ptr] = sample; // Save the sample.

	/* When sampling has been restarted, sample_ptr is set to
	 * extra_space + 1 so there is no danger of buffer overrun.*/

	// If the trigger is on but has not been triggered yet.
	if((trigger_flags & TRIGGER_ON) && !(trigger_flags & TRIGGERED) )
	{
		// If the trigger is on the up slope.
		if(trigger_flags & TRIGGER_SLOPE_UP)
		{
			// If the slope is not going up.
			if(sample_buffer[sample_ptr] < sample_buffer[sample_ptr - 1] ||
				sample_buffer[sample_ptr -1 ] < sample_buffer[sample_ptr - 2])
			{
				// Only keep the last two samples.
				sample_buffer[sample_ptr - 2] = sample_buffer[sample_ptr - 1];
				sample_buffer[sample_ptr - 1] = sample_buffer[sample_ptr];

				return; // No triggering.
			}

			// If the sample is under the trigger level.
			if(sample < trigger_level)
			{
				/// TODO same as above.
				// Only keep the last two samples.
				sample_buffer[sample_ptr - 2] = sample_buffer[sample_ptr - 1];
				sample_buffer[sample_ptr - 1] = sample_buffer[sample_ptr];

				return; // No triggering.
			}
		}
		else // Else it is on the down slope.
		{
			// If the slope is not going down.
			if(sample_buffer[sample_ptr] > sample_buffer[sample_ptr - 1] ||
				sample_buffer[sample_ptr -1 ] > sample_buffer[sample_ptr - 2])
			{
				// Only keep the last two samples.
				sample_buffer[sample_ptr - 2] = sample_buffer[sample_ptr - 1];
				sample_buffer[sample_ptr - 1] = sample_buffer[sample_ptr];

				return;  // No triggering.
			}

			// If the sample is over the trigger level.
			if(sample > trigger_level)
			{
				/// TODO same as above.
				// Only keep the last two samples.
				sample_buffer[sample_ptr - 2] = sample_buffer[sample_ptr - 1];
				sample_buffer[sample_ptr - 1] = sample_buffer[sample_ptr];

				return;  // No triggering.
			}
		}

		trigger_flags |= TRIGGERED; // Triggering has occurred.
	}

	sample_ptr++; // Start saving the sample.

	if(sample_ptr == sample_size + extra_space)
	{
		sample_ptr = extra_space - 1;

		/* Copy the last "extra_space" bytes to the beginning of the sample
		 * buffer so trigerring can be verified from the end of the last sample. */
		memcpy(sample_buffer, sample_buffer + sample_size, extra_space);
		trigger_flags &= ~DONE_SAMPLE; // Waiting for a new sample.
	}

	if(sample_ptr == sample_size) // If a full sample has been acquired.
	{
		trigger_flags &= ~TRIGGERED; // No longer triggered.
		trigger_flags |= DONE_SAMPLE; // Done acquiring a sample.

		/* Let acquisition go for another "extra_sample" before starting a
		 * new one, this will give enough time for clients to get the last
		 * sample. Here, a queue to keep samples cannot be because as
		 * well as consuming too much memory, it could introduce a lag.*/

		/// TODO use a variable instead of extra space to pause acquisition.
	}
}

ISR(ADC_vect)
{
	/* Since we are in free-running mode, the interrupt retriggers the conversion
	 * automatically (on a rising edge) and then proceeds to the service routine.
	 * From that point on, we have only one ADC clock (varies according to the prescaler
	 * value) to switch channel before conversion starts again and locks ADMUX.
	 * ----------------------
	 * See the MCU's datasheet chapter "Analog-to-Digital Conversion" section
	 * "Changing Channel or Reference Selection".*/

	/*Selects only the first 4 bits of ADMUX in order to get an index of the channel
	 * we are at.*/
	uint8_t index = ADMUX & 0x0F;

	/*Switch channel, if the index is greater than the number of channels,
	 * reset ADMUX:MUXx to channel 0, otherwise, increment it.*/
	index >= NUMBER_OF_CHANNELS - 1 ? ADMUX &= 0xF0: ADMUX++;

	/*Let the last channel store the acquired sample.*/
	instances[index]->store_sample(ADCH);

}
