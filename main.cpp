/* main.cpp - startup function for an oscilloscope
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
#include <eeprom/eeprom_24LCxx.h>
#include <core/processing.h>
#include <led/blink.h>
#include <pal/pal.h>

int main(void)
{
	init();

	Processing osc(NULL);
	TCPIPStack tcpip;
	EEPROM_24LCXX eeprom;
	Channel ch1;
	Blinker blinker(500, PIN6, &DDRD, &PORTD);

	osc.redirect_url = "/fs/main.xhtml";
	osc.add_child("ch1", &ch1);
	osc.add_child("fs", &eeprom);
	osc.add_child("tcpip", &tcpip);
	osc.add_child("blinker", &blinker);

	osc.start();
}
