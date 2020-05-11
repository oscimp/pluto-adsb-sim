#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <iio.h>
#include <ad9361.h>

#include <unistd.h>
#include <time.h>

#include "adsb_encode.h"

#define NOTUSED(V) ((void) V)
#define MHZ(x) ((long long)(x*1000000.0 + .5))
#define GHZ(x) ((long long)(x*1000000000.0 + .5))
//#define NUM_SAMPLES 2600000
#define NUM_SAMPLES 2048
#define BUFFER_SIZE (NUM_SAMPLES * 2 * sizeof(int16_t))


struct stream_cfg {
    long long bw_hz; // Analog banwidth in Hz
    long long fs_hz; // Baseband sample rate in Hz
    long long lo_hz; // Local oscillator frequency in Hz
    const char* rfport; // Port name
    double gain_db; // Hardware gain
};

static void usage() {
    fprintf(stderr, "Usage: pluto-adsb-sim [options]\n"
		"  -h                 This help\n"
        "  -t <filename>      Transmit data from file\n"
		"  -o <outfile>       Write to file instead of using PlutoSDR\n"
        "  -a <attenuation>   Set TX attenuation [dB] (default -20.0)\n"
        "  -b <bw>            Set RF bandwidth [MHz] (default 5.0)\n"
        "  -f <freq>          Set RF center frequency [MHz] (default 868.0)\n"
        "  -u <uri>           ADALM-Pluto URI\n"
        "  -n <network>       ADALM-Pluto network IP or hostname (default pluto.local)\n"
	    "  -i <ICAO>\n"
	    "  -l <Latitude>\n"
	    "  -L <Longitude>\n"
	    "  -A <Altitude>\n"
	    "  -I <Aircraft identification>\n");
    return;
}

static bool stop = false;

static void handle_sig(int sig)
{
    NOTUSED(sig);
    stop = true;
}

/* ********************** */
/* read trame from a file */
/* ********************** */

/* read from FD until '\n'
 * drop '\r' if present
 * return number of char in line buffer
 */
int readline(FILE *fd, char *line)
{
	int pos = 0, ret;
	char c;
	do {
		ret = fscanf(fd, "%c", &c);
		if (ret == EOF) {
			printf("ret empty\n");
			break;
		}
		if (c != '\n' && c != '\r') {
			line[pos] = c;
			pos++;
		}
	} while (c != '\n');
	return pos;
}

/* parse one line where
 * @ date frame ;
 * return date (uint64) and trame (uint8_t *)
 */

void parseline(char *line, int length, uint64_t *date, uint8_t *trame)
{
	/* first char is @ => drop
	 * follow 12 char timestamp
	 * last char is ; => drop
	 */
	char *ptr = line + 1;

	/* dump date [1:13]*/
	sscanf(ptr, "%12lx", date);
	ptr += 12;

	int i = 0;
	int end = (length==42)?14:7;
	for (i=0; i < end; i++) {
		sscanf(ptr+(2*i), "%02hhx", &trame[i]);
	}
}

/*
 * 
 */
int main(int argc, char** argv) {
    char buf[1024];
    int opt;
    const char* path = NULL;
    struct stream_cfg txcfg;
    FILE *fp = NULL;
    const char *uri = NULL;
    const char *ip = NULL;
    
    // TX stream default config
    txcfg.bw_hz = MHZ(3.0); // 3.0 MHz RF bandwidth
    txcfg.fs_hz = MHZ(2.0); // 2.6 MS/s TX sample rate
    txcfg.lo_hz = MHZ(868); // 1.57542 GHz RF frequency
    txcfg.rfport = "A";
    txcfg.gain_db = -20.0;

	uint32_t icao = 0xABCDEF;
	float lat = 12.34;
	float lon = 56.78;
	float alt = 9999.0;
	uint8_t *name = NULL;

	const char *outfile = NULL;
	FILE *fout = NULL;
    
    struct iio_context *ctx = NULL;
    struct iio_device *tx = NULL;
    struct iio_device *phydev = NULL;    
    struct iio_channel *tx0_i = NULL;
    struct iio_channel *tx0_q = NULL;
    struct iio_buffer *tx_buffer = NULL;    
    
    while ((opt = getopt(argc, argv, "ht:a:b:n:u:f:i:l:L:A:o:")) != EOF) {
        switch (opt) {
            case 't':
                path = optarg;
                break;
            case 'a':
                txcfg.gain_db = atof(optarg);
                if(txcfg.gain_db > 0.0) txcfg.gain_db = 0.0;
                if(txcfg.gain_db < -80.0) txcfg.gain_db = -80.0;
                break;
            case 'b':
                txcfg.bw_hz = MHZ(atof(optarg));
                if(txcfg.bw_hz > MHZ(5.0)) txcfg.bw_hz = MHZ(5.0);
                if(txcfg.bw_hz < MHZ(1.0)) txcfg.bw_hz = MHZ(1.0);
                break;
            case 'u':
                uri = optarg;
                break;
            case 'n':
                ip = optarg;
                break;
			case 'f':
				txcfg.lo_hz = MHZ(atof(optarg));
				break;
			case 'i':
				sscanf(optarg, "%x", &icao);
				break;
			case 'l':
				lat = atof(optarg);
				break;
			case 'L':
				lon = atof(optarg);
				break;
			case 'A':
				alt = atof(optarg);
				break;
			case 'I':
				name = (uint8_t *)optarg;
				break;
			case 'o':
				outfile = optarg;
				break;
			case 'h':
                usage();
                return EXIT_SUCCESS;
				break;
            default:
                printf("Unknown argument '-%c %s'\n", opt, optarg);
                usage();
                return EXIT_FAILURE;
        }
    }
	printf("%Ld\n", txcfg.lo_hz);
  
    signal(SIGINT, handle_sig);
    
    if( path != NULL ) {
    	fp = fopen(path, "r");
    	if (fp==NULL) {
    	    fprintf(stderr, "ERROR: Failed to open TX file: %s\n", path);
    	    return EXIT_FAILURE;
    	}
    }

	short *ptx_buffer;
    int32_t ntx = 0;
    
	if (outfile == NULL) {
    	printf("* Acquiring IIO context\n");
    	ctx = iio_create_default_context();
    	if (ctx == NULL) {
    	    if(ip != NULL) {
    	        ctx = iio_create_network_context(ip);
    	    } else if (uri != NULL) {
    	        ctx = iio_create_context_from_uri(uri);
    	    } else {
    	        ctx = iio_create_network_context("pluto.local");
    	    }
    	}
   
    	if (ctx == NULL) {
    	    iio_strerror(errno, buf, sizeof(buf));
    	    fprintf(stderr, "Failed creating IIO context: %s\n", buf);
    	    return false;
    	}

    	struct iio_scan_context *scan_ctx;
    	struct iio_context_info **info;
    	scan_ctx = iio_create_scan_context(NULL, 0);    
    	if (scan_ctx) {
    	    int info_count = iio_scan_context_get_info_list(scan_ctx, &info);
    	    if(info_count > 0) {
    	        printf("* Found %s\n", iio_context_info_get_description(info[0]));
    	        iio_context_info_list_free(info);
    	    }
    	iio_scan_context_destroy(scan_ctx);        
    	}    
    	
    	printf("* Acquiring devices\n");
    	int device_count = iio_context_get_devices_count(ctx);
    	if (!device_count) {
    	    fprintf(stderr, "No supported PLUTOSDR devices found.\n");
    	    goto error_exit;
    	}
    	fprintf(stderr, "* Context has %d device(s).\n", device_count);
    	
    	printf("* Acquiring TX device\n");
    	tx = iio_context_find_device(ctx, "cf-ad9361-dds-core-lpc");
    	if (tx == NULL) {
    	    iio_strerror(errno, buf, sizeof(buf));
    	    fprintf(stderr, "Error opening PLUTOSDR TX device: %s\n", buf);
    	    goto error_exit;
    	}    

    	iio_device_set_kernel_buffers_count(tx, 8);
    	
    	phydev = iio_context_find_device(ctx, "ad9361-phy");
    	//long long value = 40000000;
    	//iio_device_attr_write_longlong(phydev, "xo_correction", value);
    	struct iio_channel* phy_chn = iio_device_find_channel(phydev, "voltage0", true);
    	iio_channel_attr_write(phy_chn, "rf_port_select", txcfg.rfport);
    	iio_channel_attr_write_longlong(phy_chn, "rf_bandwidth", txcfg.bw_hz);
    	iio_channel_attr_write_longlong(phy_chn, "sampling_frequency", txcfg.fs_hz);    
    	iio_channel_attr_write_double(phy_chn, "hardwaregain", txcfg.gain_db);

    	iio_channel_attr_write_bool(
    	    iio_device_find_channel(phydev, "altvoltage0", true)
    	    , "powerdown", true); // Turn OFF RX LO
    	
    	iio_channel_attr_write_longlong(
    	    iio_device_find_channel(phydev, "altvoltage1", true)
    	    , "frequency", txcfg.lo_hz); // Set TX LO frequency
    	
    	printf("* Initializing streaming channels\n");
    	tx0_i = iio_device_find_channel(tx, "voltage0", true);
    	if (!tx0_i)
    	    tx0_i = iio_device_find_channel(tx, "altvoltage0", true);

    	tx0_q = iio_device_find_channel(tx, "voltage1", true);
    	if (!tx0_q)
    	    tx0_q = iio_device_find_channel(tx, "altvoltage1", true);
   
    	printf("* Enabling IIO streaming channels\n");
    	iio_channel_enable(tx0_i);
    	iio_channel_enable(tx0_q);    
    	
    	ad9361_set_bb_rate(iio_context_find_device(ctx, "ad9361-phy"), txcfg.fs_hz);
    	
    	printf("* Creating TX buffer\n");

    	tx_buffer = iio_device_create_buffer(tx, NUM_SAMPLES, false);
    	if (!tx_buffer) {
    	    fprintf(stderr, "Could not create TX buffer.\n");
    	    goto error_exit;
    	}
    	
    	iio_channel_attr_write_bool(
    	    iio_device_find_channel(iio_context_find_device(ctx, "ad9361-phy"), "altvoltage1", true)
    	    , "powerdown", false); // Turn ON TX LO

    	ptx_buffer = (short *)iio_buffer_start(tx_buffer);
	} else {
		fout = fopen(outfile, "w+");
		if (!fout) {
			printf("Error: fail to open %s\n", outfile);
			return EXIT_FAILURE;
		}
		ptx_buffer = (short *)malloc(4096 * sizeof(short));
		if (!ptx_buffer) {
			printf("Error: malloc fail\n");
			fclose(fout);
			return EXIT_FAILURE;
		}
	}

    printf("* Transmit starts...\n");    


	if (fp != NULL) {
		printf("Emit file content\n");

		struct timespec tm;
		tm.tv_sec = 0;
		int lineid = 0;
		int ret;
		char buffer[43];
		uint64_t date, prevdate = 0;
		uint8_t trame[14]; // 112 bits adsb
		uint8_t df17_array[256];

		do {
			buffer[42] = '\0';
			ret = readline(fp, buffer);
			if (ret == 0) {
				printf("fin\n");
				break;
			}
			lineid++;

			parseline(buffer, ret, &date, trame);
			tm.tv_sec = date - prevdate;
			prevdate = date;

			if (ret == 42) {
				frame_1090es_ppm_modulate(trame, NULL, df17_array);
				prepare_to_send(df17_array, 256, 0, 4096, ptx_buffer);

				if (outfile == NULL) {
    	    		ntx = iio_buffer_push(tx_buffer);
    	    		if (ntx < 0) {
    	    		    printf("Error pushing buf %d\n", (int) ntx);
    	    		    break;
    	    		}
				} else {
					fwrite(ptx_buffer, sizeof(short), 4096, fout);
				}
			}
			nanosleep(&tm, NULL);

		} while (ret != 0);
    	fclose(fp);
	} else { /* generate trame */
		if (name == NULL) {
			printf("Error: missing aircraft identification\n");
			usage();
			return EXIT_SUCCESS;
		}
		printf("Pseudo signal generation\n");
		//int length;
		int direction = 100;
		
		uint8_t ca = 5;
		uint8_t tc = 11;
		uint8_t ss = 0;
		uint8_t nicsb = 0;
		uint8_t time = 0;
		uint8_t surface = 0;


		while(!stop) {
			adsb_encode(ptx_buffer, icao, lat, lon, alt, ca, tc, ss, nicsb, time, surface);
    	    ntx = iio_buffer_push(tx_buffer);
    	    if (ntx < 0) {
    	        printf("Error pushing buf %d\n", (int) ntx);
    	        break;
    	    }       

			if (alt == 10000 && direction == 100)
				direction = -100;
			else if (alt == 100 && direction == -100)
				direction = 100;
			alt += direction;

			adsb_airCraftIdent(ptx_buffer, icao, 0, ca, tc, name);
			if (outfile == NULL) {
    	    	ntx = iio_buffer_push(tx_buffer);
    	    	if (ntx < 0) {
    	    	    printf("Error pushing buf %d\n", (int) ntx);
    	    	    break;
    	    	}       
				sleep(1);
			} else {
				fwrite(ptx_buffer, sizeof(short), 4096, fout);
			}
		}
    }

    printf("Done.\n");

error_exit:
	if (outfile == NULL) {
    	iio_channel_attr_write_bool(
    	    iio_device_find_channel(iio_context_find_device(ctx, "ad9361-phy"), "altvoltage1", true)
    	    , "powerdown", true); // Turn OFF TX LO                
    	            
    	if (tx_buffer) { iio_buffer_destroy(tx_buffer); }
    	if (tx0_i) { iio_channel_disable(tx0_i); }
    	if (tx0_q) { iio_channel_disable(tx0_q); }
    	if (ctx) { iio_context_destroy(ctx); }
	} else {
		fclose(fout);
		free(ptx_buffer);
	}
    return EXIT_SUCCESS;
}

