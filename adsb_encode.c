#include <stdint.h>
#include <math.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include "adsb_encode.h"

# define M_PI       3.14159265358979323846
/* format
 * message 112b
 * DF(5b) | CA (3b)  | ICAO (24b) | DATA (56b) | PI (24b)
 * DF: Downlink format -> always 17
 */
/* a message is 14Bytes
 * [0]    DF << 3 | CA
 * [1-3]  ICA0
 * [4-10] DATA (start with TC (5b))
 * [11-13] PI
 */
int encode_alt_modes(float alt, uint8_t bit13)
{
	uint8_t mbit = 0;
	uint8_t qbit = 1;
	int encalt = (int)((float)(alt + 1000) / 25);
	uint32_t tmp1, tmp2;

	if (bit13) {
		tmp1 = (encalt & 0xfe0) << 2;
		tmp2 = (encalt & 0x010) << 1;
	} else {
		tmp1 = (encalt & 0xff8) << 1;
		tmp2 = 0;
	}

	return (encalt & 0x0f) | tmp1 | tmp2 | (mbit << 6) | (qbit << 4);
}

int latz = 15;

int nz(int ctype)
{
	return 4 * latz - ctype;
}

float dlat(int ctype, uint8_t surface)
{
	float tmp;
	if (surface == 1)
		tmp = 90.0f;
	else
		tmp = 360.0f;
	float nzcalc = (float)nz(ctype);
	if (nzcalc == 0)
		return tmp;
	else
		return tmp / nzcalc;
}

float nl(float declat_in)
{
	if (fabs(declat_in) >= 87.0f)
		return 1.0;
	float v1 = pow(cos((M_PI/180.0f) * fabs(declat_in)), 2);
	float v2 = pow(acos(1.0-(1.0-cos(M_PI/(2.0f*latz))) / v1), -1);
	return floor((2.0 * M_PI) * v2);
}

float dlon(float declat_in, float ctype, uint8_t surface)
{
	float tmp;
	if (surface)
		tmp = 90.0f;
	else
		tmp = 360.0f;
	float nlcalc = fmax(nl(declat_in)-ctype, 1.0f);
	return tmp / nlcalc;
}

                                                                     // lat       lon
void cpr_encode(float lat, float lon, float ctype, uint8_t surface, int32_t *yz, int32_t *xz)
{
	float scalar;
	if (surface)
		scalar = pow(2, 19);
	else
		scalar = pow(2, 17);

	// encode using 360 constant for segment size
	float dlati = dlat(ctype, 0);
	float yz_tmp = floor(scalar * ((fmod(lat,dlati))/dlati) + 0.5);

	// encode using 360 constant for segment size
	float dloni = dlon(lat, ctype, 0);
	float xz_tmp = floor(scalar * ((fmod(lon, dloni))/dloni) + 0.5);

	*yz = (int32_t)(yz_tmp) & ((1 << 17)-1);
	*xz = (int32_t)(xz_tmp) & ((1 << 17)-1);
}

uint32_t crc(uint8_t *msg)
{
	uint16_t index, offset;
	uint32_t generator = 0x1FFF409;
	uint8_t tmp[14];
	memcpy(tmp, msg, 11);
	tmp[11] = tmp[12] = tmp[13] = 0;

	for (index = 0; index < 88; index++) {
		if ((tmp[index >> 3] & (1 << (7-(index & 0x7)))) != 0) {
			for (offset = 0; offset < 25; offset++) {
				/* generator bit */
				uint16_t curr_offset = (25-offset-1);
				int bit2 = (generator >> curr_offset) & 0x01;
				/* msg bit */
				uint8_t *curr_byte = &tmp[(index+offset) >> 3];
				uint16_t curr_index = 7-(((uint16_t)(index + offset)) & 0x007);
				int bit = ((*curr_byte) >> curr_index) & 0x01;
				if ((bit ^ bit2) == 0)
					(*curr_byte) &= ~(1 << curr_index);
				else
					(*curr_byte) |= (1 << curr_index);
			}
		}
	}
	return (tmp[11] << 16) | (tmp[12] << 8) | tmp[13];
}

/* GGM TODO bin2dec, get_parity */

/*
 * Encode a byte using Manchester encoding. Returns an array of bits.
 * Adds two start bits (1, 1) and one stop bit (0) to the array.
 */
uint16_t manchester_encode(uint8_t byte)
{
	int i;
	uint16_t tmp;
	for (i = 0; i < 16; i+=2) {
		tmp <<= 2;
		tmp |= ((0x01 ^ (0x01 & (byte >> 7))) << 1) | ((byte >> 7) & 0x01);
		byte <<= 1;
	}
	return tmp;
}

void df17_pos_rep_encode(uint8_t * df17_even, uint8_t *df17_odd, uint8_t ca, uint32_t icao, uint8_t tc, uint8_t ss, uint8_t nicsb, float alt, uint8_t time,
					float lat, float lon, uint8_t surface)
{
	uint8_t format = 17;
	uint8_t ff = 0; // cpr off/even flag;

	uint16_t enc_alt =   encode_alt_modes(alt, surface);

	int32_t evenclat, evenclon;
	int32_t oddclat, oddclon;
	cpr_encode(lat, lon, 0, surface, &evenclat, &evenclon);
	cpr_encode(lat, lon, 1, surface, &oddclat, &oddclon);

	/* since this part is always the same
	 * maybe set only the first time
	 */
	df17_even[0] = format << 3 | ca;
	df17_even[1] = (icao >> 16) & 0xff;
	df17_even[2] = (icao >> 8) & 0xff;
	df17_even[3] = (icao) & 0xff;
	// data
	df17_even[4] = (tc << 3) | ((0x03 & ss) << 1) | (nicsb & 0x01); // c'est de la position
	df17_even[5] = (enc_alt >> 4) & 0xff;
	df17_even[6] = ((enc_alt & 0xf) << 4) | ((0x01 & time) << 3) | (ff << 2) | (evenclat >> 15);
	df17_even[7] = (evenclat >> 7) & 0xff;
	df17_even[8] = ((evenclat & 0x7f) << 1) | (evenclon >> 16);
	df17_even[9] = (evenclon >> 8) & 0xff;
	df17_even[10] = evenclon & 0xff;

	//[141, 171, 205, 239, 88, 55, 112, 58, 6, 75, 184, 46, 1, 158]
	//int a;
	//printf("8dabcdef5837703a064bb8\n");
	//for (a=0; a < 11; a++)
	//	printf("%02x", df17_even[a]);
	//printf("\n");

	uint32_t checksum = crc(df17_even);
	//printf("2e019e\n");
	//printf("%08x\n", checksum);
	df17_even[11] = (checksum >> 16) & 0xff;
	df17_even[12] = (checksum >> 8) & 0xff;
	df17_even[13] = checksum & 0xff;
	
	int i;
	ff = 1;
	for (i=0; i < 6; i++)
		df17_odd[i] = df17_even[i];
	df17_odd[6] = ((enc_alt & 0xf) << 4) | ((0x01 & time) << 3) | (ff << 2) | (oddclat >> 15);
	df17_odd[7] = (oddclat >> 7) & 0xff;
	df17_odd[8] = ((oddclat & 0x7f) << 1) | (oddclon >> 16);
	df17_odd[9] = (oddclon >> 8) & 0xff;
	df17_odd[10] = oddclon & 0xff;
	

	checksum = crc(df17_odd);
	df17_odd[11] = (checksum >> 16) & 0xff;
	df17_odd[12] = (checksum >> 8) & 0xff;
	df17_odd[13] = checksum & 0xff;

	/* TODO: to complete */
}

void frame_1090es_ppm_modulate(uint8_t *even, uint8_t *odd, uint8_t *ppm)
{
	int i;
	int start = 48; // pause
	ppm[start++] = 0xA1;
	ppm[start++] = 0x40;
	for (i=0; i < 14; i++) {
		uint16_t enc = manchester_encode(~even[i]);
		ppm[start++] = (enc >> 8) & 0xff;
		ppm[start++] = (enc     ) & 0xff;
	}	

	start += 100; // pause
	if (odd == NULL) {
		for (i=0; i < 30; i++)
			ppm[start++] = 0x00;
		return;
	}
	ppm[start++] = 0xA1;
	ppm[start++] = 0x40;
	//printf("%d\n", start);

	for (i=0; i < 14; i++) {
		uint16_t enc = manchester_encode(~odd[i]);
		ppm[start++] = (enc >> 8) & 0xff;
		ppm[start++] = (enc     ) & 0xff;
	}

	// 48 byte pause after
}



/* 
 * name is 8byte long
 * A-Z ->  1 - 26
 * 0-9 -> 48 - 57
 * _   -> 32
 */
void adsb_airCraftIdent(int16_t *buffer, uint32_t icao, uint8_t ec, uint8_t ca, uint8_t tc, uint8_t *name)
{
	int i;
	char c;
	tc = 1;
	uint8_t format = 17;
	uint8_t msg[14];
	uint8_t codeName[8];
	for (i=0; i < 8; i++) {
		c = name[i];
		if (c == 0x5F) { // _
			c = 32;
		} else if (c >= 0x30 && c <= 0x39) { // 0-9
			// nothing
		} else if (c >= 0x61 && c < 0x7A) { // a-z
			c -= 0x60;
		} else if (c >= 0x41 && c < 0x5A) { // A-Z
			c -= 0x40;
		} else {
			printf("not supported char %02x\n", c);
			c = 32;
		}
		codeName[i] = c;
	}
	msg[0] = format << 3 | ca;
	msg[1] = (icao >> 16) & 0xff;
	msg[2] = (icao >> 8) & 0xff;
	msg[3] = (icao) & 0xff;
	// data
	msg[4] =  (tc << 3) | (0x07 & ec);
	//			[5:0]                 [5:4]
	msg[5] =  (codeName[0] << 2) | ((codeName[1] >> 4) & 0x3);
	//          [3:0]                 [5:2]
	msg[6] =  (codeName[1]<< 4) | ((codeName[2] >> 2) & 0xf);
	//          [1:0]                 [5:0]
	msg[7] =  (codeName[2] << 6) | (codeName[3] & 0x3F);
	//			[5:0]                 [5:4]
	msg[8] =  (codeName[4] << 2) | ((codeName[5] >> 4) & 0x3);
	//          [3:0]                 [5:2]
	msg[9] =  (codeName[5]<< 4) | ((codeName[6] >> 2) & 0xf);
	//          [1:0]                 [5:0]
	msg[10] = (codeName[6] << 6) | (codeName[7] & 0x3F);

	uint32_t checksum = crc(msg);
	msg[11] = (checksum >> 16) & 0xff;
	msg[12] = (checksum >> 8) & 0xff;
	msg[13] = checksum & 0xff;

	uint8_t df17_array[256];
	bzero(df17_array, 256);
	frame_1090es_ppm_modulate(msg, NULL, df17_array);

	prepare_to_send(df17_array, 256, 0, 4096, buffer);


}

void prepare_to_send(uint8_t *rawframe, int length, int16_t min, int16_t max, int16_t *out)
{
	int i, ii;
	/* convert bit to I/Q values */
	int size = 256 * 8;
	for (i=0, ii=0; i < size; i++, ii+=2) {
		uint8_t index = i >> 3;
		uint8_t shift = 7 - (i & 0x7); 
		if ((rawframe[index] & (1 << shift)) != 0) {
			out[ii] = max;
			out[ii+1] = max;
		} else {
			out[ii] = min;
			out[ii+1] = min;
		}
	}
}

void adsb_encode(int16_t *buffer, uint32_t icao, float lat, float lon, float alt, uint8_t ca, uint8_t tc,
	uint8_t ss, uint8_t nicsb, uint8_t time, uint8_t surface)
{

/*int main(void)
{
	//uint8_t test[] = {0x8D, 0x48, 0x40, 0xD6, 0x20, 0x2C, 0xC3, 0x71, 0xC3, 0x2C, 0xE0, 0x57, 0x60, 0x98};
	uint8_t test2[] = {
		0x8d,
		0xab,
		0xcd,
		0xef,
		0x58,
		0x37,
		0x70,
		0x3a,
		0x06,
		0x4b,
		0xb8, 0x00, 0x00, 0x00};
	uint32_t checksum = crc(test2);
	printf("%08x\n", checksum);
	printf("\n\n\n");
	//return 0;

	uint32_t icao = 0xABCDEF;
	float lat = 12.34;
	float lon = 56.78;
	float alt = 9999.0;

	uint8_t ca = 5;
	uint8_t tc = 11;
	uint8_t ss = 0;
	uint8_t nicsb = 0;
	uint8_t time = 0;
	uint8_t surface = 0;
*/
	uint8_t df17_even[14], df17_odd[14];
	df17_pos_rep_encode(df17_even, df17_odd, ca, icao, tc, ss, nicsb, alt, time, lat, lon, surface);
/*	printf("8dabcdef5837703a64bb82e19e\n");
	for (i=0; i < 14; i++)
		printf("%02x", (uint8_t)df17_even[i]);
	printf("\n");
	printf("8dabcdef58377416effaf73ef33e\n");
	for (i=0; i < 14; i++)
		printf("%02x", (uint8_t)df17_odd[i]);
	printf("\n");

	for (i=0; i < 14; i++)
		printf("%d ", (uint8_t)df17_even[i]);
	printf("\n");
*/
	uint8_t df17_array[256];
	bzero(df17_array, 256);
	//printf("modulate\n");
	frame_1090es_ppm_modulate(df17_even, df17_odd, df17_array);
	//printf("ecriture\n");

	/*FILE *fd = fopen("toto.dat", "w+");
	for (i = 0; i < 256; i++)
		fprintf(fd, "%02x\n", df17_array[i]);
	fclose(fd);*/

	prepare_to_send(df17_array, 256, 0, 4096, buffer);

}
