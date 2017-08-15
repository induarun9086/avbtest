# avbtest

Test application for the ALSA AVB driver. 

**Usage** `avbtest -t|-r <numframes> <file>`

* Transmit mode (-t)

  * The provided file is read and played over the AVB ALSA driver.
  * In parallel the file is also played through the analog audio card.
   
* Reception mode (-r)

   * Audio streamed over AVB AlSA driver is received and stored in the given file.
    
* \<numframes\>: Number of frames either to be transmitted or received.

* \<file\>: WAV file to be read (in transmit mode) or to be written to (in reception mode).

**Notes** 

* Only uncompressed WAV files are supported. 

* The AVB ALSA driver has to be loaded (using following steps)

   * The AVB ALSA driver can be found in the following kernel branch
   * https://github.com/induarun9086/beagleboard-linux
   * Load the driver using insmod.
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

