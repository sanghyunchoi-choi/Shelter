/*
 * crc.h
 *
 *  Created on: Sep 1, 2024
 *      Author: alex.shin
 */

#ifndef INC_CRC_H_
#define INC_CRC_H_


#define	CRC_START_16			0x0000
#define	CRC_START_MODBUS		0xFFFF

#define	CRC_POLY_16				0xA001

uint16_t update_crc_16( uint16_t crc, unsigned char c );

uint16_t crc_modbus( const unsigned char *input_str, size_t num_bytes );


#endif /* INC_CRC_H_ */
