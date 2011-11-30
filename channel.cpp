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

static Channel* instance;

Channel::Channel():
	Resource(),
	sample_ptr(extra_space + 1), //Init as if we already has samples stored
	trigger_flags(0),
	trigger_level(128)
{
	instance = this;

	ADMUX = _BV(REFS0) +  //Using AVcc with external capacitor at AREF pin.
			_BV(ADLAR); //Left Adjustement for 8 bit precision necessary

	ADCSRA = _BV(ADEN) +  //Enables the ADC.
			_BV(ADSC) +  //Start a conversion.
			_BV(ADATE) +  //Auto-trigger enabled.
			_BV(ADIE) +  //Interrup enable.
			_BV(ADPS2) + _BV(ADPS1) + _BV(ADPS0); //Prescaler at 128 (See sampling_rate below)
	sampling_rate = 9515; //Value taken from the comment below.

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
	 * This is in free running mode. To get a precise conversion rate, we should be
	 * starting the conversion on a timer. It is also important to note that prescalers
	 * 2, 4 are too fast for the firmware to process and that prescaler 8 would be a
	 * severe hit on performance.
	 * */

	//Nothing to set in ADCSRB
	DIDR0 = _BV(ADC0D);
	VERBOSE_PRINTLN_P("Channel ready...");

}

#define CONTENT "{\"sr\":~,\"tf\":~,\"tl\":~}"

#define CONTENT_SIZE sizeof(CONTENT) - 1

static const char content_P[] PROGMEM = CONTENT;

File* Channel::get_params(void)
{
	File* f = new PGMSpaceFile(content_P, CONTENT_SIZE);
	if(!f)
	{
		return NULL;
	}
	Template* t = new Template(f);
	if(!t)
	{
		delete f;
		return NULL;
	}

	t->add_narg(sampling_rate);
	t->add_narg(trigger_flags);
	t->add_narg(trigger_level);

	return t;
}

Response::status_code Channel::process(Request* request, Response* response)
{
	if( !request->to_destination() )
	{
			if(request->is_method(Request::GET))
			{
				if(sample_ptr > sample_size)
				{
					//A sample is ready
					File* body = get_sample();
					if(!body)
					{
						return INTERNAL_SERVER_ERROR_500;
					}
					response->set_body(body, MIME::APPLICATION_OCTET_STREAM);
					VERBOSE_PRINTLN_P("Sample ready");
					return OK_200;
				}
				else
				{
					if(queue.queue(request))
					{
						//Queue is full
						return SERVICE_UNAVAILABLE_503;
					}
					VERBOSE_PRINTLN_P("Sample not ready ");
					schedule(1);
					return RESPONSE_DELAYED_102;
				}
			}
			return NOT_IMPLEMENTED_501;

	}
	else if( request->to_destination() == 1)
	{
		request->next();
		if(!strcmp(request->current(), "pr"))
		{

			if(request->is_method(Request::POST))
			{
				char buffer[8];
				uint8_t len = request->find_arg("sr", buffer, 7);
				if(len)
				{
					buffer[len] = '\0';
					sampling_rate = atoi(buffer);
				}

				len = request->find_arg("tl", buffer, 7);
				if(len)
				{
					buffer[len] = '\0';
					ATOMIC { trigger_level = atoi(buffer); }
				}

				len = request->find_arg("tf", buffer, 7);
				if(len)
				{
					buffer[len] = '\0';
					ATOMIC { trigger_flags = atoi(buffer); }
				}

				goto get;
			}
			else if (request->is_method(Request::GET))
			{
				get:

				File* body = get_params();
				if(!body)
				{
					return INTERNAL_SERVER_ERROR_500;
				}
				response->set_body(body, MIME::APPLICATION_JSON);
				return OK_200;
			}
			return NOT_IMPLEMENTED_501;
		}
		request->previous();
	}

	return PASS_308;
}

File* Channel::get_sample(void)
{
	char* s = (char*)ts_malloc(sample_size);
	if(!s)
	{
		return NULL;
	}

	ATOMIC
	{
		memcpy(s, sample_buffer, sample_size);
	}

	MemFile* f = new MemFile(s, sample_size, false);
	if(!f)
	{
		ts_free(s);
	}

	return f;
}


void Channel::run(void)
{
	Request* request;
	VERBOSE_PRINTLN_P("Channel run ");
	while(true)
	{
		request = queue.peek();
		if(!request)
		{
			break;
		}
		if(request->age + max_request_age < get_uptime() )
		{
			VERBOSE_PRINTLN_P("Request too old");
			request = queue.dequeue();
			Response* response = new Response(REQUEST_TIMEOUT_408, request);
			if(!response)
			{
				delete request;
				return;
			}
			dispatch(response);
		}
		else
		{
			VERBOSE_PRINTLN_P("No Request too old");
			break;
		}
	}

	if( request && trigger_flags & DONE_SAMPLE )
	{
		VERBOSE_PRINTLN_P("Got data!");
		while(true)
		{
			request = queue.dequeue();
			if(!request)
			{
				break;
			}
			Response* response = new Response(OK_200, request);
			if(!response)
			{
				delete request;
				break;
			}
			File* sample = get_sample();
			if(!sample)
			{
				response->response_code_int = INTERNAL_SERVER_ERROR_500;
			}
			else
			{
				response->set_body(sample, MIME::APPLICATION_OCTET_STREAM);
			}
			dispatch(response);
		}
	}
	else if(request)
	{
		schedule(1);
		return;
	}

	schedule(NEVER);
}

void Channel::store_sample(uint8_t sample)
{
	//have to save the sample here to compare it with trigger

	sample_buffer[sample_ptr] = sample;

	if((trigger_flags & TRIGGER_ON) && !(trigger_flags & TRIGGERED) )
	{
		if(trigger_flags & TRIGGER_SLOPE_UP)
		{
			if(sample_buffer[sample_ptr] < sample_buffer[sample_ptr - 1] ||
					sample_buffer[sample_ptr -1 ] < sample_buffer[sample_ptr - 2])
			{
				sample_buffer[sample_ptr - 2] = sample_buffer[sample_ptr - 1];
				sample_buffer[sample_ptr - 1] = sample_buffer[sample_ptr];
				return;
			}
			if(sample < trigger_level)
			{
				sample_buffer[sample_ptr - 2] = sample_buffer[sample_ptr - 1];
				sample_buffer[sample_ptr - 1] = sample_buffer[sample_ptr];
				return;
			}
		}
		else
		{
			if(sample_buffer[sample_ptr] > sample_buffer[sample_ptr - 1] ||
					sample_buffer[sample_ptr -1 ] > sample_buffer[sample_ptr - 2])
			{
				sample_buffer[sample_ptr - 2] = sample_buffer[sample_ptr - 1];
				sample_buffer[sample_ptr - 1] = sample_buffer[sample_ptr];
				return;
			}
			if(sample > trigger_level)
			{
				sample_buffer[sample_ptr - 2] = sample_buffer[sample_ptr - 1];
				sample_buffer[sample_ptr - 1] = sample_buffer[sample_ptr];
				return;
			}
		}
		trigger_flags |= TRIGGERED;
	}

	sample_ptr++;

	if(sample_ptr == sample_size + extra_space)
	{
		sample_ptr = extra_space - 1;
		memcpy( sample_buffer, sample_buffer + sample_size, extra_space );
		trigger_flags &= ~DONE_SAMPLE;
	}

	if(sample_ptr == sample_size)
	{
		trigger_flags &= ~TRIGGERED;
		trigger_flags |= DONE_SAMPLE;
	}
}

ISR(ADC_vect)
{
	instance->store_sample(ADCH);
}
