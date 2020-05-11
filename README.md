# pluto-absb-sim

pluto-adsb-sim generates ADSB signal IQ data stream, which is then transmitted
by the software-defined radio (SDR) platform [ADALM-Pluto](https://wiki.analog.com/university/tools/pluto).

Project based on [ADSB-Out](https://github.com/lyusupov/ADSB-Out)

Three mode are available:
* Tx stream from an ASCII file;
* Tx stream from fake frame;
* fake frame or ASCII file to binary file

### compile

#### dependencies
This application require [libiio](https://github.com/analogdevicesinc/libiio), [libad9361-iio](https://github.com/analogdevicesinc/libad9361-iio)
and *pkg-config*

```bash
$ sudo apt install libiio-dev libad9361-dev libad9361-0 libiio0 pkg-config
```

#### compile

```bash
$ make
```

### usage

```bash
Usage: pluto-adsb-sim [options]
  -h                 This help
  -t <filename>      Transmit data from file
  -o <outfile>       Write to file instead of using PlutoSDR
  -a <attenuation>   Set TX attenuation [dB] (default -20.0)
  -b <bw>            Set RF bandwidth [MHz] (default 5.0)
  -f <freq>          Set RF center frequency [MHz] (default 868.0)
  -u <uri>           ADALM-Pluto URI
  -n <network>       ADALM-Pluto network IP or hostname (default pluto.local)
  -i <ICAO>
  -l <Latitude>
  -L <Longitude>
  -A <Altitude>
  -I <Aicraft identification>

```

To use **-n** or nothing, you need to have in */etc/hosts* a line like:
```
192.168.2.1 pluto pluto.local
```

#### Transmit data from file

To use this mode a file must be provided with *-t* option

The file used is an ascii, hex format, one frame by line:
```
@XXXXXXXXXXXXYYYYYYYYYYYYYYYYYYYYYYYYYYYY;
```

Each line start with a *@* and finish with a *;*
Where:
* X is a 12 char hex date in ns
* Y is the 14 char hex full frame (DF, ICAO, DATA, CRC)

__example__
```bash
./pluto-adsb-sim -f 868 -t maFile.dat
```

#### Fake signal generation

To use this mode, don't provide a data file (no *-t* option)

__example__
```bash
./pluto-adsb-sim -f 868 -i 0xABCDEF -I GGM_1980 -l 48.36 -L -4.77 -A 9999.0
```

### binary file generation

If *-o* is used the *PlutoSDR* is not used. Instead the data stream is written
in a binary, signed short IQ interleaved format

### Verify with dump1090

```bash
$ dump1090 --freq 868000000
```

*--freq* parameter must be the same as -f
