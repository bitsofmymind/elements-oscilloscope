/* main.cpp - startup function for the oscilloscope
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
#include <tcpip/tcpip.h>
//#include <eeprom/eeprom_24LCxx.h>
#include <core/processing.h>
#include <led/blink.h>
#include <pal/pal.h>
#include <serial/eserial.h>

/**
 * This program is a web based oscilloscope built using the elements framework.
 * It is meant to run on an arduino duemilanove with a 24LC256 EEPROM for storage
 * and a Microchip ENC28J60 network interface for ethernet.
 *
 * It uses the ATMega328P's two first ADC pins as channels for signal acquisition.
 * */
int main(void)
{
	init(); // Initialize the framework.

	Processing oscilloscope(NULL); // The processing resource.
	TCPIPStack tcpip; // The TCPIP stack interfacing with the ENC28J60.
	EEPROM_24LCXX eeprom; // The EEPROM that stores the CSS, JS and HTML files.
	Channel channe1(1); // The oscilloscope's first channel.
	Channel channel2(2); // The oscilloscope's second channel.

	 // Redirect the server's root to this url.
	oscilloscope.redirect_url = "/fs/main.xhtml";

	// Add all the resources as child of the processing resource.
	oscilloscope.add_child("ch1", &channel1);
	oscilloscope.add_child("ch2", &channel2);
	oscilloscope.add_child("fs", &eeprom);
	oscilloscope.add_child("tcpip", &tcpip);

	oscilloscope.start(); // Start the oscilloscope.
}
