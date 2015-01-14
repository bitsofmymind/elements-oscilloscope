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

/// The number of channels on the oscilloscope.
#define NUMBER_OF_CHANNELS 2

/**
 * This resource is a signal acquisition channel.
 * The following are the list of sub-resources defined inside the class:
 * - /: sample data octet stream
 * - /pr: parameters JSON array (GET)
 *    - sr: sampling rate (argument)
 *    - tl: trigger level (argument)
 *    - tf: trigger flags (argument)
 *
 * */
class Channel: public Resource
{
	public:

		// Cannot be accessed from within the ISR.
		// static Channel* instances[NUMBER_OF_CHANNELS];

	protected:
		///TODO the following should be converted to defines in order to save on RAM.

		/// The delay after which a request expires.
		static const uptime_t max_request_age = 1000;

		/// The size in bytes of a sample.
		static const uint8_t sample_size = 100;

		/** The amount of extra space after the end of the buffer. */
		static const uint8_t extra_space = 20;

		/** The sampling rate in samples / second. This value is configurable
		 * through the web interface. */
		uint16_t sampling_rate;

		/// The location where sampling is at in the sample buffer.
		volatile uint8_t sample_ptr;

		/// The buffer that holds the sample.
		uint8_t sample_buffer[sample_size + extra_space];
		/* This buffer is declared statically for the reason that there
		 * is always a minimum of two sample holding buffers in existence. This
		 * one, where the current sample is being saved, an another dynamically
		 * allocated that is being sent to a client that requested it. Since we
		 * always need a buffer to hold the current sample it saves memory
		 * an processing time to always have it in place. */

		/// Sample acquisition is done.
		#define DONE_SAMPLE _BV(3)

		/// Triggering has occurred.
		#define TRIGGERED _BV(2)

		/// If triggering should be on the up slope.
		#define TRIGGER_SLOPE_UP _BV(1)

		/// If there should be triggering for sample acquisition.
		#define TRIGGER_ON _BV(0)

		/** Configuration flags for triggering.
		 * 0 TRIGGER_ON
		 * 1 TRIGGER_SLOPE_UP
		 * 2 TRIGGERED
		 * 3 DONE SAMPLING
		 * 4 UNUSED
		 * 5 UNUSED
		 * 6 UNUSED
		 * 7 UNUSED
		 * */
		volatile uint8_t trigger_flags;

		/// The level in ADC units at which triggering should occur.
		volatile uint16_t trigger_level;

		/// The queue were requests are kept.
		Queue<Request*> queue;

	public:

		/**
		 * Class constructor.
		 * @param number the channel number starting from 1 and going up to
		 * NUMBER_OF_CHANNELS.
		 * */
		Channel(uint8_t number);

	protected:

        /// Process a request message.
        /**
         * @param request the request to process.
         * @param response the response to fill if a response should be returned (which
         * depends on the status code).
         * @return the status_code produced while processing the request.
         */
		virtual Response::status_code process(Request* request, Response* response);

		/// Does processing on the resource.
		virtual void run(void);

		/**
		 * Get the most recent sample.
		 * @return a file containing the most recent sample.
		 * */
		File* get_sample(void);

		/**
		 * Get the current parameters for the channel.
		 * @return a file containing the parameters for the channel.
		 * */
		File* get_params(void);

	public:

		/**
		 * Store a sample from the ADC. This method is meant to be called from
		 * the ADC's interrupt service routine.
		 * @param sample the sample just acquired by the ADC.
		 * */
		void store_sample(uint8_t sample);
};

#endif /* CHANNEL_H_ */
