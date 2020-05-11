#ifndef __ADSB_ENCODE_H__
#define __ADSB_ENCODE_H__

/* buffer must have a size of 4096 */
void adsb_encode(int16_t *buffer, uint32_t icao, float lat, float lon, float alt,
	uint8_t ca, uint8_t tc, uint8_t ss, uint8_t nicsb, uint8_t time, uint8_t surface);
void adsb_airCraftIdent(int16_t *buffer, uint32_t icao, uint8_t ec, uint8_t ca, uint8_t tc, uint8_t *name);

/* convert trame to manchester
 * odd may be NULL
 * length of ppm (output) must be 256B
 * 	or 
 * 	for an even only 112bit trame : 
 * 	48 (pause) + 2 (preamble) + 2 x even + 130 (pause)
 *  for an odd+even 112bits trame : 
 *  2 (preamble) + 16 x even + 100 (pause) + 2 (preamble) + 16 * odd + 48
 */
void frame_1090es_ppm_modulate(uint8_t *even, uint8_t *odd, uint8_t *ppm);

/* out buffer must have :
 * length * 8(convert each bit to val) * 2 (I/Q)
 */ 
void prepare_to_send(uint8_t *rawframe, int length, int16_t min, int16_t max, int16_t *out);

#endif
