#include <stdio.h>
#include <stdlib.h>
#include "asoundlib.h"

#define AVB_TEST_NUM_FRAMES_IN_BUFFER 1024
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

int startPlayback(int maxFrames, char* filename);
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
	int numFrames = 0;

	printf("AVB Test Application \n");

	if(argc < 4) {
		printf("Not enough params: Usage: avbtest -t|-r <number of frames> <filename> \n");
		return -1;
	}

	sscanf(&argv[2][0], "%d", &numFrames);

	if((argv[1][0] == '-') && (argv[1][1] == 't')) {
		printf("avbtest: Playing back %d frames from file %s \n", numFrames, &argv[3][0]);
		return startPlayback(numFrames, &argv[3][0]);
	} else if((argv[1][0] == '-') && (argv[1][1] == 'r')) {
		printf("avbtest: Recorrding %d frames to file %s \n", numFrames, &argv[3][0]);
		return startRecord(numFrames, &argv[3][0]);
	} else {
		printf("Wrong params: Usage: avbtest -t|-r filename \n");
		return -1;
	}

	return 0;
}

int startPlayback(int maxFrames, char* filename)
{
	int err;
	FILE* fp;
	int numFrames = 0;
	int readBytes = 0;
	int sentFrames = 0;
	int readFrames = 0;
	snd_pcm_t *handle;
	struct wavhdr hdr;
	snd_pcm_uframes_t ringbufsize;
 	snd_pcm_uframes_t periodsize;
	struct timespec t1, t2, d;

	fp = fopen(filename, "rb");

	if(fp == NULL) {
		printf("File open error");
                return -1;
	}

	fread((void*)&hdr, sizeof(struct wavhdr), 1, fp);

	printf("size: %d, chunksize: %d, fmtsize: %d datasize:%d \n", sizeof(struct wavhdr), hdr.chunksize, hdr.fmtsize, hdr.datasize);
	printf("chunkid: %c%c%c%c - ", hdr.chunkid[0], hdr.chunkid[1], hdr.chunkid[2], hdr.chunkid[3]);
	printf("format: %c%c%c%c - ", hdr.format[0], hdr.format[1], hdr.format[2], hdr.format[3]);
	printf("fmtid: %c%c%c%c - ", hdr.fmtid[0], hdr.fmtid[1], hdr.fmtid[2], hdr.fmtid[3]);
	printf("dataid: %c%c%c%c \n", hdr.dataid[0], hdr.dataid[1], hdr.dataid[2], hdr.dataid[3]);
	printf("audioformat: %d, numchannels: %d, samplerate: %d\n", hdr.audioformat, hdr.numchannels, hdr.samplerate);
	printf("byterate: %d, blockalign: %d, bps: %d\n", hdr.byterate, hdr.blockalign, hdr.bps);

	if((hdr.chunkid[0] != 'R') || (hdr.chunkid[1] != 'I') || (hdr.chunkid[2] != 'F') || (hdr.chunkid[3] != 'F') ||
	   (hdr.format[0]  != 'W') || (hdr.format[1]  != 'A') || (hdr.format[2]  != 'V') || (hdr.format[3]  != 'E') ||
	   (hdr.fmtid[0]   != 'f') || (hdr.fmtid[1]   != 'm') || (hdr.fmtid[2]   != 't') || (hdr.fmtid[3]   != ' ') ||
	   (hdr.dataid[0]  != 'd') || (hdr.dataid[1]  != 'a') || (hdr.dataid[2]  != 't') || (hdr.dataid[3]  != 'a') ||
	   (hdr.fmtsize != 16) || (hdr.audioformat != 1) || (hdr.bps != 16)) {
		printf("Wave header error \n");
                return -1;
	}

	if ((err = snd_pcm_open(&handle, "default", SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
		printf("Playback open error: %s\n", snd_strerror(err));
		return -1;
        }

	if ((err = snd_pcm_set_params(handle, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, hdr.numchannels, hdr.samplerate, 0, 100000)) < 0) {
		printf("Playback set params error: %s\n", snd_strerror(err));
		return -1;
        }

	snd_pcm_get_params(handle, &ringbufsize, &periodsize);
	printf("Playback params ringbufsize: %li, periodsize: %li\n", ringbufsize, periodsize);

	clock_gettime(CLOCK_REALTIME, &t1);

	while(((numFrames < maxFrames) || (maxFrames == -1)) && (ferror(fp) == 0) && (feof(fp) == 0)) {
		readFrames = (((maxFrames - numFrames) > AVB_TEST_NUM_FRAMES_IN_BUFFER)?(AVB_TEST_NUM_FRAMES_IN_BUFFER):(maxFrames - numFrames));
		readFrames = ((readFrames < 0)?(AVB_TEST_NUM_FRAMES_IN_BUFFER):(readFrames));
		readFrames = ((readFrames < periodsize)?(readFrames):(periodsize));
		readBytes  = readFrames * hdr.numchannels * (hdr.bps / 8);

		fread((void*)&buf[0], readBytes, 1, fp);

		sentFrames = snd_pcm_writei(handle, &buf[0], readFrames);

		if (sentFrames < 0)
			sentFrames = snd_pcm_recover(handle, sentFrames, 0);
		if (sentFrames < 0) {
			printf("snd_pcm_writei failed: %s (%d)\n", snd_strerror(sentFrames), sentFrames);
			break;
		}

		if ((sentFrames > 0) && (sentFrames < readFrames))
			printf("Short write (expected %d, wrote %d)\n", readFrames, sentFrames);

		numFrames += sentFrames;

		if((numFrames != 0) && ((numFrames % 100) == 0))
			printf("\rPlayback sent %d frames", numFrames);
	}

	clock_gettime(CLOCK_REALTIME, &t2);
	timespec_diff(&t1, &t2, &d);

	printf("\rPlayback sent %d frames in %ds, %dms\n", numFrames, d.tv_sec, (int)((double)d.tv_nsec / 1.0e6));

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
	int rcvdBytes = 0;
	int readFrames = 0;
	snd_pcm_t *handle;
	struct wavhdr hdr;

	hdr.chunkid[0] = 'R'; hdr.chunkid[1] = 'I'; hdr.chunkid[2] = 'F'; hdr.chunkid[3] = 'F';
	hdr.format[0] = 'W'; hdr.format[1] = 'A'; hdr.format[2] = 'V'; hdr.format[3] = 'E';
	hdr.fmtid[0] = 'f'; hdr.fmtid[1] = 'm'; hdr.fmtid[2] = 't'; hdr.fmtid[3] = ' ';
	hdr.chunkid[0] = 'd'; hdr.chunkid[1] = 'a'; hdr.chunkid[2] = 't'; hdr.chunkid[3] = 'a';
	hdr.audioformat = 1; hdr.bps = 16; hdr.numchannels = 2; 
	hdr.blockalign = 4; hdr.samplerate = 48000; hdr.byterate = ((hdr.numchannels * (hdr.bps / 8)) * hdr.samplerate);

	fp = fopen("r.tmp", "wb");

	if(fp == NULL) {
		printf("File open error");
                return -1;
	}

	if ((err = snd_pcm_open(&handle, "default", SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
		printf("Playback open error: %s\n", snd_strerror(err));
		return -1;
        }

	if ((err = snd_pcm_set_params(handle, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, hdr.numchannels, hdr.samplerate, 0, 100000)) < 0) {
		printf("Playback opset params error: %s\n", snd_strerror(err));
		return -1;
        }

	while(numFrames < maxFrames) {
		readFrames = (((maxFrames - numFrames) > AVB_TEST_NUM_FRAMES_IN_BUFFER)?(AVB_TEST_NUM_FRAMES_IN_BUFFER):(maxFrames - numFrames));
		readBytes  = readFrames * hdr.numchannels * (hdr.bps / 8);

		rcvdBytes = snd_pcm_readi(handle, &buf[0], readBytes);

		if (rcvdBytes < 0)
			rcvdBytes = snd_pcm_recover(handle, rcvdBytes, 0);
		if (rcvdBytes < 0) {
			printf("snd_pcm_readi failed: %s (%d)\n", snd_strerror(rcvdBytes), rcvdBytes);
			break;
		}

		if ((rcvdBytes > 0) && (rcvdBytes < readBytes))
			printf("Short read (expected %d, read %d)\n", readBytes, rcvdBytes);

		fwrite((void*)&buf[0], rcvdBytes, 1, fp);

		numFrames += (rcvdBytes / (hdr.numchannels * (hdr.bps / 8)));
	}

	hdr.fmtsize = 16; hdr.datasize = ((hdr.numchannels * (hdr.bps / 8)) * numFrames); hdr.chunksize = 36 + hdr.datasize;
	printf("Record received %d frames: %d\n", numFrames);

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

	while((ferror(fp) == 0) && (feof(fp) == 0)) {
		fread((void*)&buf[0], AVB_TEST_NUM_BYTES_IN_BUFFER, 1, fi);
		fwrite((void*)&buf[0], AVB_TEST_NUM_BYTES_IN_BUFFER, 1, fp);
	}

	fclose(fi);
	fclose(fp);

	return 0;
}
