# avbtest

Test application for the ALSA AVB driver

Usage: `avbtest -t|-r <numframes> <file>`

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

