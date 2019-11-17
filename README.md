# avbtest

Test application for the ALSA AVB driver. 

**Usage** `avbtest {-p|-r|-a|-b} <filename> -n <number of frames> -d <devicename> -t -s`

* Playback mode (-t)

  * The provided file is read and played over the AVB ALSA driver
  * Or through the specified device using -d switch.
   
* Record mode (-r)

   * Audio streamed over AVB AlSA driver is recorded and stored in the given file.
   * Or audio streamed through the specified device with -d switch is recorded and stored in the given file.
   
* Demo mode -a: Demo mode device 'a'. Streams the provided stream over AVB with timestamp.

* Demo mode -b: Demot mode device 'b'. Playsback the provided file and also reocrds the AVB stream input and play it back through the prodivded analog audio device with the playback synchronized to the presentation time.

* \<file\>: WAV file to be read (in transmit mode) or to be written to (in reception mode).
    
* -n \<numframes\>: Number of frames either to be transmitted or received.

* -d \<devname\>: Name of the analog audio device to be used.

* -t: Timestamp mode: The AVB frames are transmitted along with the presentation time.
                      Or in record mode the presentation time are received and can be used for synchronization.

**Notes** 

* Only uncompressed WAV files are supported. 

* The AVB ALSA driver has to be loaded (using following steps)

   * The AVB ALSA driver can be found in the following kernel branch
   * https://github.com/induarun9086/beagleboard-linux/sound/drivers/avb.c
   * Rebuild the kernel Load the driver using insmod.
   * The driver depends on the ALSA hardware dependent interface
   * The snd-hwdep.ko module has to be loaded before AVB driver can be loaded.
   
* The gPTPd daemon has to running to test the synchronized audio streaming over AVB

   * The gPTP deamon is found in the following repo
   * https://github.com/induarun9086/gPTPd
   * The build and executions instructions are in the above repo.
   
**Test Environment**

* 2 Beagle bone blacks connected togther with a cross ehternet cable.
* The beagle bone black in the transmit mode should have a analog audio card connected.
* 2 Channels of the analog audio card are connected to a test PC.

**License**

MIT License Copyright (c) [2018] [Indumathi Duraipandian]
