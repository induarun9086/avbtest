#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "asoundlib.h"

#define AVB_TEST_AVB_PLAYBACK_THREAD     0
#define AVB_TEST_ANALOG_PLAYBACK_THREAD  1
#define AVB_TEST_AVB_RECORD_THREAD       2
#define AVB_TEST_MAX_NUMBER_THREADS      3

#define AVB_TEST_MAX_FILE_NAME_SIZE   1024
#define AVB_TEST_MAX_DEV_NAME_SIZE    256
#define AVB_TEST_MAX_HWDEP_NAME_SIZE  256
#define AVB_TEST_NUM_FRAMES_IN_BUFFER 2048
#define AVB_TEST_NUM_BYTES_IN_BUFFER  (AVB_TEST_NUM_FRAMES_IN_BUFFER * 2 * 2) /* 2 channels, 2 bytes per sample */

#pragma pack(push, 1)

struct wavhdr {
	char chunkid[4];
	int chunksize;
	char format[4];
	char fmtid[4];
	int fmtsize;
	short int audioformat;
	short int numchannels;
	int samplerate;
	int byterate;
	short int blockalign;
	short int bps;
	char dataid[4];
	int datasize;
};

#pragma pack(pop)

struct threadargs {
	int id;
	int res;
	int maxframes;
	char filename[AVB_TEST_MAX_FILE_NAME_SIZE];
	char devname[AVB_TEST_MAX_DEV_NAME_SIZE];
	char hwdepname[AVB_TEST_MAX_HWDEP_NAME_SIZE];
};

pthread_t tids[AVB_TEST_MAX_NUMBER_THREADS] = {0};
struct threadargs targs[AVB_TEST_MAX_NUMBER_THREADS] = {0};

void* startPlayback(void* argument);
int startRecord(int maxFrames, char* filename);
void timespec_diff(struct timespec *start, struct timespec *stop,
                   struct timespec *result);

unsigned char buf[AVB_TEST_NUM_BYTES_IN_BUFFER];

void timespec_diff(struct timespec *start, struct timespec *stop,
                   struct timespec *result)
{
    if ((stop->tv_nsec - start->tv_nsec) < 0) {
        result->tv_sec = stop->tv_sec - start->tv_sec - 1;
        result->tv_nsec = stop->tv_nsec - start->tv_nsec + 1000000000;
    } else {
        result->tv_sec = stop->tv_sec - start->tv_sec;
        result->tv_nsec = stop->tv_nsec - start->tv_nsec;
    }

    return;
}

int main(int argc, char* argv[])
{
	int i = 0;
	int err = 0;
	int numFrames = 0;

	printf("AVB Test Application \n");

	if(argc < 4) {
		printf("Not enough params: Usage: avbtest -t|-r <number of frames> <filename> \n");
		return -1;
	}

	sscanf(&argv[2][0], "%d", &numFrames);

	if((argv[1][0] == '-') && (argv[1][1] == 't')) {

		printf("avbtest: Playing back %d frames from file %s \n", numFrames, &argv[3][0]);

		memset(&targs[AVB_TEST_AVB_PLAYBACK_THREAD], 0, sizeof(struct threadargs));
		targs[AVB_TEST_AVB_PLAYBACK_THREAD].id = AVB_TEST_AVB_PLAYBACK_THREAD;
		targs[AVB_TEST_AVB_PLAYBACK_THREAD].res = 0;
		targs[AVB_TEST_AVB_PLAYBACK_THREAD].maxframes = numFrames;
		memcpy(&targs[AVB_TEST_AVB_PLAYBACK_THREAD].filename[0], &argv[3][0], strlen(&argv[3][0]));
		memcpy(&targs[AVB_TEST_AVB_PLAYBACK_THREAD].devname[0], "hw:CARD=avb,0", strlen("hw:CARD=avb,0"));
		memcpy(&targs[AVB_TEST_AVB_PLAYBACK_THREAD].hwdepname[0], "hw:avb", strlen("hw:avb"));

		pthread_create(&tids[AVB_TEST_AVB_PLAYBACK_THREAD], NULL, startPlayback, &targs[AVB_TEST_AVB_PLAYBACK_THREAD]);

		memset(&targs[AVB_TEST_ANALOG_PLAYBACK_THREAD], 0, sizeof(struct threadargs));
		targs[AVB_TEST_ANALOG_PLAYBACK_THREAD].id = AVB_TEST_ANALOG_PLAYBACK_THREAD;
		targs[AVB_TEST_ANALOG_PLAYBACK_THREAD].res = 0;
		targs[AVB_TEST_ANALOG_PLAYBACK_THREAD].maxframes = numFrames;
		memcpy(&targs[AVB_TEST_ANALOG_PLAYBACK_THREAD].filename[0], &argv[3][0], strlen(&argv[3][0]));
		memcpy(&targs[AVB_TEST_ANALOG_PLAYBACK_THREAD].devname[0], "hw:CARD=C8CH,0", strlen("hw:CARD=C8CH,0"));

		pthread_create(&tids[AVB_TEST_ANALOG_PLAYBACK_THREAD], NULL, startPlayback, &targs[AVB_TEST_ANALOG_PLAYBACK_THREAD]);

		for(i = 0; i < AVB_TEST_MAX_NUMBER_THREADS; i++) {
			err = pthread_join(tids[i], NULL);
			printf("avbtest: Thread t%d exit with result: %d \n", i, err);
		}

		printf("avbtest: Playback completed \n");

	} else if((argv[1][0] == '-') && (argv[1][1] == 'r')) {
		printf("avbtest: Recorrding %d frames to file %s \n", numFrames, &argv[3][0]);
		return startRecord(numFrames, &argv[3][0]);
	} else {
		printf("Wrong params: Usage: avbtest -t|-r filename \n");
		return -1;
	}

	return 0;
}

void* startPlayback(void* argument)
{
	int err;
	FILE* fp;
	int numFrames = 0;
	int readBytes = 0;
	int sentFrames = 0;
	int readFrames = 0;
	snd_pcm_t *handle;
	struct wavhdr hdr;
	snd_hwdep_t* hwdep;
	snd_pcm_uframes_t ringbufsize;
 	snd_pcm_uframes_t periodsize;
	struct timespec t1, t2, d;
	struct threadargs* arg = (struct threadargs*)argument;

	fp = fopen(arg->filename, "rb");

	if(fp == NULL) {
		printf("t%d: File open error", arg->id);
                arg->res = -1;
                return &arg->res;
	}

	fread((void*)&hdr, sizeof(struct wavhdr), 1, fp);

	printf("t%d: size: %lu, chunksize: %d, fmtsize: %d datasize:%d \n", arg->id, sizeof(struct wavhdr), hdr.chunksize, hdr.fmtsize, hdr.datasize);
	printf("t%d: chunkid: %c%c%c%c - ", arg->id, hdr.chunkid[0], hdr.chunkid[1], hdr.chunkid[2], hdr.chunkid[3]);
	printf("format: %c%c%c%c - ", hdr.format[0], hdr.format[1], hdr.format[2], hdr.format[3]);
	printf("fmtid: %c%c%c%c - ", hdr.fmtid[0], hdr.fmtid[1], hdr.fmtid[2], hdr.fmtid[3]);
	printf("dataid: %c%c%c%c \n", hdr.dataid[0], hdr.dataid[1], hdr.dataid[2], hdr.dataid[3]);
	printf("t%d: audioformat: %d, numchannels: %d, samplerate: %d\n", arg->id, hdr.audioformat, hdr.numchannels, hdr.samplerate);
	printf("t%d: byterate: %d, blockalign: %d, bps: %d\n", arg->id, hdr.byterate, hdr.blockalign, hdr.bps);

	if((hdr.chunkid[0] != 'R') || (hdr.chunkid[1] != 'I') || (hdr.chunkid[2] != 'F') || (hdr.chunkid[3] != 'F') ||
	   (hdr.format[0]  != 'W') || (hdr.format[1]  != 'A') || (hdr.format[2]  != 'V') || (hdr.format[3]  != 'E') ||
	   (hdr.fmtid[0]   != 'f') || (hdr.fmtid[1]   != 'm') || (hdr.fmtid[2]   != 't') || (hdr.fmtid[3]   != ' ') ||
	   (hdr.dataid[0]  != 'd') || (hdr.dataid[1]  != 'a') || (hdr.dataid[2]  != 't') || (hdr.dataid[3]  != 'a') ||
	   (hdr.fmtsize != 16) || (hdr.audioformat != 1) || (hdr.bps != 16)) {
		printf("t%d: Wave header error \n", arg->id);
		arg->res = -1;
                return &arg->res;
	}

	if(arg->hwdepname[0] != 0) {
		if ((err = snd_hwdep_open(&hwdep, &arg->hwdepname[0], SND_HWDEP_OPEN_DUPLEX)) < 0) {
			printf("t%d: Playback hwdep open error: %s\n", arg->id, snd_strerror(err));
			arg->res = -1;
		        return &arg->res;	
		} 
	}

	if ((err = snd_pcm_open(&handle, &arg->devname[0], SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
		printf("t%d: Playback open error: %s\n", arg->id, snd_strerror(err));
		arg->res = -1;
                return &arg->res;
        }

	if ((err = snd_pcm_set_params(handle, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, hdr.numchannels, hdr.samplerate, 1, 100000)) < 0) {
		printf("t%d: Playback set params error: %s\n", arg->id, snd_strerror(err));
		arg->res = -1;
                return &arg->res;
        }

	snd_pcm_get_params(handle, &ringbufsize, &periodsize);
	printf("t%d: Playback params ringbufsize: %li, periodsize: %li\n", arg->id, ringbufsize, periodsize);

	clock_gettime(CLOCK_REALTIME, &t1);

	while(((numFrames < arg->maxframes) || (arg->maxframes == -1)) && (ferror(fp) == 0) && (feof(fp) == 0)) {
		readFrames = (((arg->maxframes - numFrames) > AVB_TEST_NUM_FRAMES_IN_BUFFER)?(AVB_TEST_NUM_FRAMES_IN_BUFFER):(arg->maxframes - numFrames));
		readFrames = ((readFrames < 0)?(AVB_TEST_NUM_FRAMES_IN_BUFFER):(readFrames));
		readFrames = ((readFrames < periodsize)?(readFrames):(periodsize));
		readBytes  = readFrames * hdr.numchannels * (hdr.bps / 8);

		fread((void*)&buf[0], readBytes, 1, fp);

		if(arg->hwdepname[0] != 0)
			snd_hwdep_ioctl(hwdep, 0, (void*)t1.tv_nsec);

		sentFrames = snd_pcm_writei(handle, &buf[0], readFrames);

		if (sentFrames < 0)
			sentFrames = snd_pcm_recover(handle, sentFrames, 0);
		if (sentFrames < 0) {
			printf("t%d: snd_pcm_writei failed: %s (%d)\n", arg->id, snd_strerror(sentFrames), sentFrames);
			break;
		}

		if ((sentFrames > 0) && (sentFrames < readFrames))
			printf("t%d: Short write (expected %d, wrote %d)\n", arg->id, readFrames, sentFrames);

		numFrames += sentFrames;

		if((numFrames != 0) && ((numFrames % 100) == 0))
			printf("t%d: Playback sent %d frames\n", arg->id, numFrames);
	}

	clock_gettime(CLOCK_REALTIME, &t2);
	timespec_diff(&t1, &t2, &d);

	printf("t%d: Playback sent %d frames in %lds, %dms\n", arg->id, numFrames, d.tv_sec, (int)((double)d.tv_nsec / 1.0e6));

	fclose(fp);
	snd_pcm_close(handle);

	return 0;
}

int startRecord(int maxFrames, char* filename)
{
	int err;
	FILE* fp;
	FILE* fi;
	int numFrames = 0;
	int readBytes = 0;
	int rcvdFrames = 0;
	int readFrames = 0;
	snd_pcm_t *handle;
	struct wavhdr hdr;
	unsigned long ts;
	snd_hwdep_t* hwdep;
	snd_pcm_uframes_t ringbufsize;
 	snd_pcm_uframes_t periodsize;

	hdr.chunkid[0] = 'R'; hdr.chunkid[1] = 'I'; hdr.chunkid[2] = 'F'; hdr.chunkid[3] = 'F';
	hdr.format[0] = 'W'; hdr.format[1] = 'A'; hdr.format[2] = 'V'; hdr.format[3] = 'E';
	hdr.fmtid[0] = 'f'; hdr.fmtid[1] = 'm'; hdr.fmtid[2] = 't'; hdr.fmtid[3] = ' ';
	hdr.dataid[0] = 'd'; hdr.dataid[1] = 'a'; hdr.dataid[2] = 't'; hdr.dataid[3] = 'a';
	hdr.audioformat = 1; hdr.bps = 16; hdr.numchannels = 2; 
	hdr.blockalign = 4; hdr.samplerate = 48000; hdr.byterate = ((hdr.numchannels * (hdr.bps / 8)) * hdr.samplerate);

	fp = fopen("r.tmp", "wb");

	if(fp == NULL) {
		printf("File open error");
                return -1;
	}

	if ((err = snd_hwdep_open(&hwdep, "hw:avb", SND_HWDEP_OPEN_DUPLEX)) < 0) {
		printf("Playback hwdep open error: %s\n", snd_strerror(err));
		return -1;	
	} 

	if ((err = snd_pcm_open(&handle, "hw:CARD=avb,0", SND_PCM_STREAM_CAPTURE, 0)) < 0) {
		printf("Record open error: %s\n", snd_strerror(err));
		return -1;
        }

	if ((err = snd_pcm_set_params(handle, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, hdr.numchannels, hdr.samplerate, 0, 100000)) < 0) {
		printf("Record opset params error: %s\n", snd_strerror(err));
		return -1;
        }

	if ((err = snd_pcm_prepare (handle)) < 0) {
		printf ("Record cannot prepare audio interface for use (%s) %d\n", snd_strerror(err), err);
		return -1;
	}

	snd_pcm_get_params(handle, &ringbufsize, &periodsize);
	printf("Playback params ringbufsize: %li, periodsize: %li\n", ringbufsize, periodsize);

	snd_pcm_start(handle);

	while(numFrames < maxFrames) {
		readFrames = (((maxFrames - numFrames) > AVB_TEST_NUM_FRAMES_IN_BUFFER)?(AVB_TEST_NUM_FRAMES_IN_BUFFER):(maxFrames - numFrames));
		readBytes  = readFrames * hdr.numchannels * (hdr.bps / 8);

		printf("Record reading %d frames", readFrames);

		rcvdFrames = snd_pcm_readi(handle, &buf[0], readFrames);

		if (rcvdFrames < 0) {
			rcvdFrames = snd_pcm_recover(handle, rcvdFrames, 0);
			snd_pcm_start(handle);
			continue;
		}
		if (rcvdFrames < 0) {
			printf("snd_pcm_readi failed: %s (%d)\n", snd_strerror(rcvdFrames), rcvdFrames);
			break;
		}

		if ((rcvdFrames > 0) && (rcvdFrames < readFrames))
			printf("Short read (expected %d, read %d)\n", readFrames, rcvdFrames);

		snd_hwdep_ioctl(hwdep, 1, (void*)&ts);
		printf(" - Record read ts %ld \n", ts);

		fwrite((void*)&buf[0], 1, readBytes, fp);

		numFrames += rcvdFrames;
	}

	snd_pcm_drop(handle);

	hdr.fmtsize = 16; hdr.datasize = ((hdr.numchannels * (hdr.bps / 8)) * numFrames); hdr.chunksize = 36 + hdr.datasize;
	printf("Record received %d frames\n", numFrames);

	snd_pcm_close(handle);
	fflush(fp);
	fclose(fp);

	fi = fopen("r.tmp", "rb");
	fp = fopen(filename, "wb");

	if((fp == NULL) || (fi == NULL)) {
		printf("File open error");
                return -1;
	}

	fwrite((void*)&hdr, sizeof(struct wavhdr), 1, fp);

	while((ferror(fi) == 0) && (feof(fi) == 0)) {
		readBytes = fread((void*)&buf[0], 1, AVB_TEST_NUM_BYTES_IN_BUFFER, fi);
		fwrite((void*)&buf[0], 1, readBytes, fp);
	}

	fclose(fi);
	fclose(fp);

	return 0;
}
