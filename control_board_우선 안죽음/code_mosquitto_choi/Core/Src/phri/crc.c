/*
 * crc.c
 *
 *  Created on: Sep 1, 2024
 *      Author: alex.shin
 */

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "crc.h"


static void init_crc16_tab( void );

static bool	crc_tab16_init = false;
static uint16_t crc_tab16[256];

uint16_t update_crc_16( uint16_t crc, unsigned char c )
{
	uint16_t tmp;
	uint16_t short_c;

	short_c = 0x00ff & (uint16_t) c;

	if ( ! crc_tab16_init ) init_crc16_tab();

	tmp =  crc       ^ short_c;
	crc = (crc >> 8) ^ crc_tab16[ tmp & 0xff ];

	return crc;
}

uint16_t crc_modbus( const unsigned char *input_str, size_t num_bytes )
{
	uint16_t crc;
	uint16_t tmp;
	uint16_t short_c;
	const unsigned char *ptr;
	size_t a;

	if ( ! crc_tab16_init ) init_crc16_tab();

	crc = CRC_START_MODBUS;
	ptr = input_str;

	if ( ptr != NULL ) for (a=0; a<num_bytes; a++) {

		short_c = 0x00ff & (uint16_t) *ptr;
		tmp     =  crc       ^ short_c;
		crc     = (crc >> 8) ^ crc_tab16[ tmp & 0xff ];

		ptr++;
	}

	return crc;
}

static void init_crc16_tab( void )
{
	uint16_t i;
	uint16_t j;
	uint16_t crc;
	uint16_t c;

	for (i=0; i<256; i++) {
		crc = 0;
		c   = i;

		for (j=0; j<8; j++) {

			if ( (crc ^ c) & 0x0001 ) crc = ( crc >> 1 ) ^ CRC_POLY_16;
			else crc =   crc >> 1;

			c = c >> 1;
		}
		crc_tab16[i] = crc;
	}
	crc_tab16_init = true;
}
