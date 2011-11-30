/* channel.h - Implements an oscilloscope channel resource
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

#ifndef CHANNEL_H_
#define CHANNEL_H_

#include <core/resource.h>

#define NUMBER_OF_CHANNELS 2

class Channel: public Resource
{

	public:
		//Cannot be accessed from within the ISR.
		//static Channel* instances[NUMBER_OF_CHANNELS];

	protected:
		static const uptime_t max_request_age = 1000;
		static const uint8_t sample_size = 100;
		static const uint8_t extra_space = 20;

		uint16_t sampling_rate;
		volatile uint8_t sample_ptr;
		uint8_t sample_buffer[sample_size + extra_space];

		#define DONE_SAMPLE _BV(3)
		#define TRIGGERED _BV(2)
		#define TRIGGER_SLOPE_UP _BV(1)
		#define TRIGGER_ON _BV(0)
		volatile uint8_t trigger_flags;
		volatile uint16_t trigger_level;

		Queue<Request*> queue;

	public:
		Channel(uint8_t number);

	protected:
		File* get_params(void);
		virtual Response::status_code process(Request* request, Response* response);
		virtual void run(void);
		File* get_sample(void);
	public:
		void store_sample(uint8_t sample);

};

#endif /* CHANNEL_H_ */
