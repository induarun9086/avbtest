#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sched.h>
#include "asoundlib.h"

#define TWO_POW_32 ((unsigned long long)4294967296)

#define T0    0
#define T1    1
#define T2    2
#define T3    3
#define T4    4
#define AVB_TEST_MAX_NUMBER_THREADS 5

#define AVB_TEST_DBG_LVL_ERROR           0
#define AVB_TEST_DBG_LVL_WARNING         1
#define AVB_TEST_DBG_LVL_INFO            2

#define AVB_TEST_DBG_LVL_VERBOSE         3

#define AVB_TEST_MODE_AVB_PLAYBACK       0
#define AVB_TEST_MODE_AVB_RECORD         1
#define AVB_TEST_MODE_AVB_DEMO_TX        2
#define AVB_TEST_MODE_AVB_DEMO_RX_1      3
#define AVB_TEST_MODE_AVB_DEMO_RX_2      4
#define AVB_TEST_MODE_AVB_DEMO_DEV_X     5
#define AVB_TEST_MODE_AVB_DEMO_DEV_Y     6

#define AVB_TEST_MAX_FILE_NAME_SIZE    1024
#define AVB_TEST_MAX_DEV_NAME_SIZE     256
#define AVB_TEST_MAX_HWDEP_NAME_SIZE   256
#define AVB_TEST_NUM_FRAMES_IN_BUFFER  8192
#define AVB_TEST_NUM_BYTES_IN_BUFFER  (AVB_TEST_NUM_FRAMES_IN_BUFFER * 8 * 2) /* Max 8 channels, 2 bytes per sample */

#define AVB_TEST_AUD_BUFFER_NUM_FRAMES  (10 * 192000) /* for Max 10 seconds @ 192000 HZ */
#define AVB_TEST_AUD_BUFFER_SIZE        (AVB_TEST_AUD_BUFFER_NUM_FRAMES * 8 * 2) /* Max 8 channels, 2 bytes per sample */

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

struct avbtestcfg {
    int mode;
    int clkId;
    int dbgLvl;
    int maxframes;
    int samplerate;
    int numchannels;
    unsigned char sync;
    unsigned char tobuf;
    unsigned char timestamp;
    char filename[AVB_TEST_MAX_FILE_NAME_SIZE];
    char devname[AVB_TEST_MAX_DEV_NAME_SIZE];
    char hwdepname[AVB_TEST_MAX_HWDEP_NAME_SIZE];
};

struct threadargs {
    int id;
    int res;
    struct avbtestcfg cfg;
};

unsigned int syncts = 0;
int currTxIdx = 0;
int currRxIdx = 0;
int audbufsize = 0;
struct timespec synctime = {0};
unsigned char playbackDone = 0;
pthread_t tids[AVB_TEST_MAX_NUMBER_THREADS] = {0};
pthread_attr_t tattrs[AVB_TEST_MAX_NUMBER_THREADS] = {0};
struct threadargs targs[AVB_TEST_MAX_NUMBER_THREADS] = {0};
unsigned char audbuf[AVB_TEST_AUD_BUFFER_SIZE] = {0};
unsigned char buf[AVB_TEST_MAX_NUMBER_THREADS][AVB_TEST_NUM_BYTES_IN_BUFFER] = {0};

void printUsage(void);
void waitForThreads(int num);
unsigned int getAVTPTs(struct timespec* t);
void* startRecord(void* argument);
void* startPlayback(void* argument);
void setThreadPrio(pthread_attr_t* ptattrs, int prio);
void timespec_sum(struct timespec *a, struct timespec *b);
int parseargs(int argc, char* argv[], struct avbtestcfg* cfg);
void getAVTPSt(struct threadargs* arg, unsigned int ts, struct timespec* st);
void timespec_diff(struct timespec *start, struct timespec *stop, struct timespec *result);
void initAndStartThread(int id, int prio, struct avbtestcfg* cfg, void* fn, unsigned char tobuf);

void timespec_diff(struct timespec *start, struct timespec *stop, struct timespec *result)
{
    if((stop->tv_nsec - start->tv_nsec) < 0) {
        result->tv_sec = stop->tv_sec - start->tv_sec - 1;
        result->tv_nsec = stop->tv_nsec - start->tv_nsec + 1000000000;
    } else {
        result->tv_sec = stop->tv_sec - start->tv_sec;
        result->tv_nsec = stop->tv_nsec - start->tv_nsec;
    }

    return;
}

void timespec_sum(struct timespec *a, struct timespec *b)
{
    if((a->tv_nsec + b->tv_nsec) >= 1000000000) {
        a->tv_sec = a->tv_sec + b->tv_sec + 1;
        a->tv_nsec = (a->tv_nsec + b->tv_nsec) - 1000000000;
    } else {
        a->tv_sec = a->tv_sec + b->tv_sec;
        a->tv_nsec = a->tv_nsec + b->tv_nsec;
    }

    return;
}

unsigned int getAVTPTs(struct timespec* t)
{
    unsigned long long ts;

    ts = t->tv_nsec;
    ts += ((unsigned long long)t->tv_sec * 1000000000);
    ts %= TWO_POW_32;

    return (unsigned int)ts;
}

void getAVTPSt(struct threadargs* arg, unsigned int ts, struct timespec* st)
{
    unsigned int currtime = getAVTPTs(st);
    unsigned int diff = ((ts > currtime)?(ts - currtime):((TWO_POW_32 - currtime) + ts));

    if(arg->cfg.dbgLvl >= AVB_TEST_DBG_LVL_INFO)
      printf("t%d: syncts: %lu currts: %lu diff: %lu \n", arg->id, ts, currtime, diff);

    st->tv_sec  += diff / 1000000000;
    st->tv_nsec += diff % 1000000000;
    st->tv_sec  += ((st->tv_nsec >= 1000000000)?(1):(0));
    st->tv_nsec -= ((st->tv_nsec >= 1000000000)?(1000000000):(0));
}

void setThreadPrio(pthread_attr_t* ptattrs, int prio)
{
    struct sched_param param;

    pthread_attr_init(ptattrs);
    pthread_attr_getschedparam(ptattrs, &param);
    param.sched_priority = prio;
    pthread_attr_setschedpolicy(ptattrs, SCHED_FIFO);
    pthread_attr_setschedparam(ptattrs, &param);
}

void printUsage(void) {
    printf("Usage: avbtest {-p|-r|-a|-b[0|1]|x|y} <filename> -d <devicename> -n <number of frames> -c <number of channels> -r <sampling rate> -l <dbglevel>\n");
}

int parseargs(int argc, char* argv[], struct avbtestcfg* cfg)
{
    int i = 0;
    int idx = 0;

    memset(cfg, 0, sizeof(struct avbtestcfg));

    cfg->maxframes = -1;
    cfg->numchannels = 2;
    cfg->samplerate = 48000;
    cfg->mode = AVB_TEST_MODE_AVB_PLAYBACK;
    memcpy(&cfg->hwdepname[0], "hw:avb", strlen("hw:avb"));

    for(i = 1; i < argc; i++) {
        if(argv[i][0] == '-') {
            switch(argv[i][1]) {
                case 'p':
                    cfg->mode = AVB_TEST_MODE_AVB_PLAYBACK;
                    break;
                case 'r':
                    cfg->mode = AVB_TEST_MODE_AVB_RECORD;
                    break;
		case 'x':
                    cfg->mode = AVB_TEST_MODE_AVB_DEMO_DEV_X;
		    cfg->timestamp = 1;
		    cfg->sync = 1;
                    break;
		case 'y':
                    cfg->mode = AVB_TEST_MODE_AVB_DEMO_DEV_Y;
		    cfg->sync = 1;
                    break;
                case 'a':
                    cfg->mode = AVB_TEST_MODE_AVB_DEMO_TX;
		    cfg->timestamp = 1;
                    break;
                case 'b':
		    sscanf(&argv[i][2], "%d", &idx);
		    if(idx == 0) {
                    	cfg->mode = AVB_TEST_MODE_AVB_DEMO_RX_1;
		    } else {
                    	cfg->mode = AVB_TEST_MODE_AVB_DEMO_RX_2;
		    }
		    cfg->sync = 1;
                    break;
                case 'c':
                    sscanf(&argv[i][2], "%d", &cfg->numchannels);
                    break;
		case 's':
                    sscanf(&argv[i][2], "%d", &cfg->samplerate);
                    break;
                case 'n':
                    sscanf(&argv[i][2], "%d", &cfg->maxframes);
                    break;
                case 'd':
                    sscanf(&argv[i][2], "%s", cfg->devname);
                    break;
		case 'l':
		    sscanf(&argv[i][2], "%d", &cfg->dbgLvl);
		    break;
                default:
                    break;
            }
        } else {
		sscanf(&argv[i][0], "%s", cfg->filename);	
	}
    }

    if(cfg->devname[0] == 0) {
        if((cfg->mode == AVB_TEST_MODE_AVB_PLAYBACK) || (cfg->mode == AVB_TEST_MODE_AVB_DEMO_TX) || (cfg->mode == AVB_TEST_MODE_AVB_RECORD))
            memcpy(&cfg->devname[0], "hw:CARD=avb,0", strlen("hw:CARD=avb,0")+1);
        else
            memcpy(&cfg->devname[0], "hw:CARD=C8CH,0", strlen("hw:CARD=C8CH,0")+1);
    }
}

void initAndStartThread(int id, int prio, struct avbtestcfg* cfg, void* fn, unsigned char tobuf)
{
    memset(&targs[id], 0, sizeof(struct threadargs));
    targs[id].id = id; targs[id].res = 0; cfg->tobuf = tobuf;
    memcpy(&targs[id].cfg, cfg, sizeof(struct avbtestcfg));

    setThreadPrio(&tattrs[id], prio);
    pthread_create(&tids[id], &tattrs[id], fn, &targs[id]);
}

void waitForThreads(int num)
{
    int i = 0;
    int err = 0;

    for(i = 0; i < num; i++) {
        err = pthread_join(tids[i], NULL);
	printf("avbtest: Thread t%d exit with result: %d \n", i, err);
	playbackDone = 1;
    }
}

int main(int argc, char* argv[])
{
    int i = 0;
    int fd = 0;
    int err = 0;
    struct avbtestcfg cfg;

    printf("AVB Test Application %s %s\n", __DATE__, __TIME__);

    if(parseargs(argc, argv, &cfg) < 0) {
        printUsage();
        return -1;
    }

    fd = open("/dev/ptp0", O_RDWR);
    cfg.clkId = ((~(clockid_t) (fd) << 3) | 3);

    if((cfg.mode == AVB_TEST_MODE_AVB_PLAYBACK) || (cfg.mode == AVB_TEST_MODE_AVB_DEMO_TX)) {
	printf("avbtest: Playing back %d frames from file %s (ch: %d sr: %d) \n", cfg.maxframes, cfg.filename, cfg.numchannels, cfg.samplerate);
        initAndStartThread(T0, 50, &cfg, startPlayback, 0);
        waitForThreads(1);
    } else if(cfg.mode == AVB_TEST_MODE_AVB_RECORD) {
        printf("avbtest: Recording %d frames to file %s (ch: %d sr: %d) \n", cfg.maxframes, cfg.filename, cfg.numchannels, cfg.samplerate);
        initAndStartThread(T0, 50, &cfg, startRecord, 0);
        waitForThreads(1);
    } else if(cfg.mode == AVB_TEST_MODE_AVB_DEMO_DEV_X) {
	printf("avbtest: Playing back %d frames from file %s (ch: %d sr: %d) \n", cfg.maxframes, cfg.filename, cfg.numchannels, cfg.samplerate);
	cfg.sync = 1;
	cfg.timestamp = 0;
	memcpy(&cfg.devname, "hw:CARD=BeagleBoardX15,0", strlen("hw:CARD=BeagleBoardX15,0")+1);
        initAndStartThread(T0, 50, &cfg, startPlayback, 0);
	cfg.sync = 0;
	cfg.timestamp = 1;
        memcpy(&cfg.devname, "hw:CARD=avb,0", strlen("hw:CARD=avb,0")+1);
	initAndStartThread(T1, 50, &cfg, startPlayback, 0);
        waitForThreads(2);
    } else if(cfg.mode == AVB_TEST_MODE_AVB_DEMO_DEV_Y) {
	printf("avbtest: Playing back %d frames from file %s (ch: %d sr: %d) \n", cfg.maxframes, cfg.filename, cfg.numchannels, cfg.samplerate);
	cfg.timestamp = 1;
        memcpy(&cfg.devname, "hw:CARD=avb,0", strlen("hw:CARD=avb,0")+1);
        initAndStartThread(T0, 99, &cfg, startRecord, 1);
	cfg.sync = 1;
	cfg.timestamp = 0;
        memcpy(&cfg.devname, "stereo4", strlen("stereo4")+1);
	initAndStartThread(T1, 50, &cfg, startPlayback, 1);
	cfg.numchannels = 4; memcpy(&cfg.filename, "r042.wav", strlen("r042.wav")+1);
        memcpy(&cfg.devname, "hw:CARD=C8CH,0", strlen("hw:CARD=C8CH,0")+1);
        initAndStartThread(T2, 50, &cfg, startRecord, 0);
        waitForThreads(3);
    } else if(cfg.mode == AVB_TEST_MODE_AVB_DEMO_RX_2) {
        printf("avbtest: Demo mode b %d frames from file %s (ch: %d sr: %d) \n", cfg.maxframes, cfg.filename, cfg.numchannels, cfg.samplerate);
        memcpy(&cfg.devname, "stereo2", strlen("stereo2")+1);
        initAndStartThread(T0, 50, &cfg, startPlayback, 0);
	cfg.timestamp = 1;
        memcpy(&cfg.devname, "hw:CARD=avb,0", strlen("hw:CARD=avb,0")+1);
        initAndStartThread(T1, 99, &cfg, startRecord, 1);
	cfg.timestamp = 0;
        memcpy(&cfg.devname, "stereo4", strlen("stereo4")+1);
        initAndStartThread(T2, 50, &cfg, startPlayback, 1);
        waitForThreads(3);
    }  else if(cfg.mode == AVB_TEST_MODE_AVB_DEMO_RX_1) {
        printf("avbtest: Demo mode b %d frames from file %s (ch: %d sr: %d) \n", cfg.maxframes, cfg.filename, cfg.numchannels, cfg.samplerate);
        memcpy(&cfg.devname, "stereo2", strlen("stereo2")+1);
        initAndStartThread(T0, 50, &cfg, startPlayback, 0);
	cfg.timestamp = 1;
        memcpy(&cfg.devname, "hw:CARD=avb,0", strlen("hw:CARD=avb,0")+1);
        initAndStartThread(T1, 99, &cfg, startRecord, 1);
	cfg.timestamp = 0;
        memcpy(&cfg.devname, "stereo4", strlen("stereo4")+1);
        initAndStartThread(T2, 50, &cfg, startPlayback, 1);
	cfg.numchannels = 4; memcpy(&cfg.filename, "r042.wav", strlen("r042.wav")+1);
        memcpy(&cfg.devname, "hw:CARD=C8CH,0", strlen("hw:CARD=C8CH,0")+1);
        initAndStartThread(T3, 50, &cfg, startRecord, 0);
        waitForThreads(4);
    } else {
        printUsage();
        return -1;
    }

    printf("avbtest: Operation completed \n");

    return 0;
}

void* startPlayback(void* argument)
{
    int err;
    FILE* fp;
    int ferr = 0;
    int numFrames = 0;
    int readBytes = 0;
    int sentFrames = 0;
    int readFrames = 0;
    int latency = 125000;
    unsigned long ts = 0;
    unsigned char diffcalc = 0;
    snd_pcm_t *handle;
    struct wavhdr hdr;
    snd_hwdep_t* hwdep;
    snd_pcm_uframes_t ringbufsize;
    snd_pcm_uframes_t periodsize;
    struct timespec t, t1, t2, d, tp;
    struct threadargs* arg = (struct threadargs*)argument;

    if(arg->cfg.tobuf == 0){
        fp = fopen(arg->cfg.filename, "rb");

        if(fp == NULL) {
	  if(arg->cfg.dbgLvl >= AVB_TEST_DBG_LVL_ERROR)
            printf("t%d: File (%s) open error \n", arg->id, arg->cfg.filename);
          arg->res = -1;
          return &arg->res;
        }

        fread((void*)&hdr, sizeof(struct wavhdr), 1, fp);

	if(arg->cfg.dbgLvl >= AVB_TEST_DBG_LVL_VERBOSE) {
          printf("t%d: size: %lu, chunksize: %d, fmtsize: %d datasize:%d \n", arg->id, sizeof(struct wavhdr), hdr.chunksize, hdr.fmtsize, hdr.datasize);
          printf("t%d: chunkid: %c%c%c%c - ", arg->id, hdr.chunkid[0], hdr.chunkid[1], hdr.chunkid[2], hdr.chunkid[3]);
          printf("format: %c%c%c%c - ", hdr.format[0], hdr.format[1], hdr.format[2], hdr.format[3]);
          printf("fmtid: %c%c%c%c - ", hdr.fmtid[0], hdr.fmtid[1], hdr.fmtid[2], hdr.fmtid[3]);
          printf("dataid: %c%c%c%c \n", hdr.dataid[0], hdr.dataid[1], hdr.dataid[2], hdr.dataid[3]);
          printf("t%d: audioformat: %d, numchannels: %d, samplerate: %d\n", arg->id, hdr.audioformat, hdr.numchannels, hdr.samplerate);
          printf("t%d: byterate: %d, blockalign: %d, bps: %d\n", arg->id, hdr.byterate, hdr.blockalign, hdr.bps);
	}

        if((hdr.chunkid[0] != 'R') || (hdr.chunkid[1] != 'I') || (hdr.chunkid[2] != 'F') || (hdr.chunkid[3] != 'F') ||
           (hdr.format[0]  != 'W') || (hdr.format[1]  != 'A') || (hdr.format[2]  != 'V') || (hdr.format[3]  != 'E') ||
           (hdr.fmtid[0]   != 'f') || (hdr.fmtid[1]   != 'm') || (hdr.fmtid[2]   != 't') || (hdr.fmtid[3]   != ' ') ||
           (hdr.dataid[0]  != 'd') || (hdr.dataid[1]  != 'a') || (hdr.dataid[2]  != 't') || (hdr.dataid[3]  != 'a') ||
           (hdr.fmtsize != 16) || (hdr.audioformat != 1) || (hdr.bps != 16)) {
              printf("t%d: Wave header error \n", arg->id);
              arg->res = -1;
              return &arg->res;
        }
    } else {
        hdr.chunkid[0] = 'R'; hdr.chunkid[1] = 'I'; hdr.chunkid[2] = 'F'; hdr.chunkid[3] = 'F';
        hdr.format[0] = 'W'; hdr.format[1] = 'A'; hdr.format[2] = 'V'; hdr.format[3] = 'E';
        hdr.fmtid[0] = 'f'; hdr.fmtid[1] = 'm'; hdr.fmtid[2] = 't'; hdr.fmtid[3] = ' ';
        hdr.dataid[0] = 'd'; hdr.dataid[1] = 'a'; hdr.dataid[2] = 't'; hdr.dataid[3] = 'a';
        hdr.audioformat = 1; hdr.bps = 16; hdr.numchannels = arg->cfg.numchannels; 
        hdr.blockalign = (hdr.numchannels * (hdr.bps / 8)); hdr.samplerate = arg->cfg.samplerate; hdr.byterate = ((hdr.numchannels * (hdr.bps / 8)) * hdr.samplerate);
    }

    if((arg->cfg.hwdepname[0] != 0) && (arg->cfg.timestamp != 0)) {
        if((err = snd_hwdep_open(&hwdep, &arg->cfg.hwdepname[0], SND_HWDEP_OPEN_DUPLEX)) < 0) {
            if(arg->cfg.dbgLvl >= AVB_TEST_DBG_LVL_ERROR)
		printf("t%d: Playback hwdep open error: %s\n", arg->id, snd_strerror(err));
            arg->res = -1;
            return &arg->res;
        }
    }

    if((err = snd_pcm_open(&handle, &arg->cfg.devname[0], SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
      if(arg->cfg.dbgLvl >= AVB_TEST_DBG_LVL_ERROR)
	printf("t%d: Playback open error: %s\n", arg->id, snd_strerror(err));
      arg->res = -1;
      return &arg->res;
    }

    latency = ((arg->cfg.timestamp != 0)?(250000):(125000));
    if((err = snd_pcm_set_params(handle, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, hdr.numchannels, hdr.samplerate, 0, latency)) < 0) {
      if(arg->cfg.dbgLvl >= AVB_TEST_DBG_LVL_ERROR)
	printf("t%d: Playback set params error: %s\n", arg->id, snd_strerror(err));
      arg->res = -1;
      return &arg->res;
    }

    snd_pcm_get_params(handle, &ringbufsize, &periodsize);
    if(arg->cfg.dbgLvl >= AVB_TEST_DBG_LVL_INFO)
	printf("t%d: Playback params ringbufsize: %li, periodsize: %li\n", arg->id, ringbufsize, periodsize);

    if((arg->cfg.sync != 0) && (arg->cfg.timestamp == 0)) {
      clock_gettime(arg->cfg.clkId, &t1);
      if(arg->cfg.tobuf == 0) {
        do {
          usleep(100);
          clock_gettime(arg->cfg.clkId, &t);
        } while(((t.tv_sec % 3) != 0) || (t.tv_sec == t1.tv_sec));
      } else {
	do {
          usleep(100);
          clock_gettime(arg->cfg.clkId, &t);
        } while((syncts == 0) || (t.tv_sec < synctime.tv_sec) || (t.tv_nsec < synctime.tv_nsec));
      }
    }

    clock_gettime(arg->cfg.clkId, &t1);

    if(arg->cfg.dbgLvl >= AVB_TEST_DBG_LVL_INFO)
	printf("t%d: Playback starting @ %d s %d ns \n", arg->id, t1.tv_sec, t1.tv_nsec);

    usleep(1);

    while(((numFrames < arg->cfg.maxframes) || (arg->cfg.maxframes == -1)) && (ferr == 0) && (playbackDone == 0)) {
        readFrames = (((arg->cfg.maxframes - numFrames) > AVB_TEST_NUM_FRAMES_IN_BUFFER)?(AVB_TEST_NUM_FRAMES_IN_BUFFER):(arg->cfg.maxframes - numFrames));
        readFrames = ((readFrames < 0)?(AVB_TEST_NUM_FRAMES_IN_BUFFER):(readFrames));
        readFrames = ((readFrames < periodsize)?(readFrames):(periodsize));
        readBytes  = readFrames * hdr.numchannels * (hdr.bps / 8);

        if(arg->cfg.tobuf == 0)
            fread((void*)&buf[arg->id][0], readBytes, 1, fp);
        else {
            while((audbufsize < (2 * readBytes)) && (playbackDone == 0)) usleep(1000);
            memcpy(&buf[arg->id][0], &audbuf[currTxIdx], readBytes);
            currTxIdx += readBytes; currTxIdx %= AVB_TEST_AUD_BUFFER_SIZE;
            audbufsize -= readBytes;
        }

        if((arg->cfg.hwdepname[0] != 0) && (arg->cfg.timestamp != 0)) {
	    clock_gettime(arg->cfg.clkId, &t);
	    if(diffcalc == 0) {
	       diffcalc = 1;
	       t2.tv_sec = (((t.tv_sec / 3) + 1) * 3);
               t2.tv_nsec = 0;
               timespec_diff(&t, &t2, &d);
               if(arg->cfg.dbgLvl >= AVB_TEST_DBG_LVL_INFO)
		 printf("t%d: Timestamp diff @ %d s %d ns \n", arg->id, d.tv_sec, d.tv_nsec);
	    }
            timespec_sum(&t, &d);
	    ts = getAVTPTs(&t);
	    if(arg->cfg.dbgLvl >= AVB_TEST_DBG_LVL_INFO)
		printf("t%d: Sync for %lus %luns ts: %lu \n", arg->id, t.tv_sec, t.tv_nsec, ts);
            snd_hwdep_ioctl(hwdep, 0, (void*)ts);
	}


	if(numFrames == 0) {
		if(arg->cfg.dbgLvl >= AVB_TEST_DBG_LVL_INFO) {
			clock_gettime(arg->cfg.clkId, &tp);
			printf("t%d: First frame transferred @ %d s %d ns \n");
		}	
	}

        sentFrames = snd_pcm_writei(handle, &buf[arg->id][0], readFrames);

        if(sentFrames < 0)
            sentFrames = snd_pcm_recover(handle, sentFrames, 0);
        if(sentFrames < 0) {
            if(arg->cfg.dbgLvl >= AVB_TEST_DBG_LVL_ERROR)
		printf("t%d: snd_pcm_writei failed: %s (%d)\n", arg->id, snd_strerror(sentFrames), sentFrames);
            break;
        }

        if((sentFrames > 0) && (sentFrames < readFrames))
            if(arg->cfg.dbgLvl >= AVB_TEST_DBG_LVL_WARNING)
		printf("t%d: Short write (expected %d, wrote %d)\n", arg->id, readFrames, sentFrames);

        numFrames += sentFrames;

	if(arg->cfg.dbgLvl >= AVB_TEST_DBG_LVL_VERBOSE) {
	    if(arg->cfg.tobuf == 0) 
        	printf("t%d: Playback sent %d frames\n", arg->id, numFrames);
	    else
		printf("t%d: Playback sent %d frames currTxIdx: %d audbufsize: %d\n",
			arg->id, numFrames, (currTxIdx / (hdr.numchannels * (hdr.bps / 8))),
			(audbufsize / (hdr.numchannels * (hdr.bps / 8))));
	}

	if(arg->cfg.tobuf == 0) 
           if((ferror(fp) != 0) || (feof(fp) != 0))
              ferr = 1;
    }

    clock_gettime(arg->cfg.clkId, &t2);
    timespec_diff(&t1, &t2, &d);

    if(arg->cfg.dbgLvl >= AVB_TEST_DBG_LVL_VERBOSE)
	printf("t%d: Playback sent %d frames in %lds, %dms\n", arg->id, numFrames, d.tv_sec, (int)((double)d.tv_nsec / 1.0e6));

    if(arg->cfg.tobuf == 0) 
      fclose(fp);

    snd_pcm_close(handle);

    return 0;
}

void* startRecord(void* argument)
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
    unsigned long ts = 0;
    snd_hwdep_t* hwdep;
    struct timespec tp;
    snd_pcm_uframes_t ringbufsize;
     snd_pcm_uframes_t periodsize;
    unsigned char tmpfile[AVB_TEST_MAX_FILE_NAME_SIZE] = {0};
    struct threadargs* arg = (struct threadargs*)argument;

    hdr.chunkid[0] = 'R'; hdr.chunkid[1] = 'I'; hdr.chunkid[2] = 'F'; hdr.chunkid[3] = 'F';
    hdr.format[0] = 'W'; hdr.format[1] = 'A'; hdr.format[2] = 'V'; hdr.format[3] = 'E';
    hdr.fmtid[0] = 'f'; hdr.fmtid[1] = 'm'; hdr.fmtid[2] = 't'; hdr.fmtid[3] = ' ';
    hdr.dataid[0] = 'd'; hdr.dataid[1] = 'a'; hdr.dataid[2] = 't'; hdr.dataid[3] = 'a';
    hdr.audioformat = 1; hdr.bps = 16; hdr.numchannels = arg->cfg.numchannels; 
    hdr.blockalign = (hdr.numchannels * (hdr.bps / 8)); hdr.samplerate = arg->cfg.samplerate; hdr.byterate = ((hdr.numchannels * (hdr.bps / 8)) * hdr.samplerate);

    if(arg->cfg.tobuf == 0) {
        sprintf(&tmpfile[0], "r%d.tmp", arg->id);
        fp = fopen(&tmpfile[0], "wb");

        if(fp == NULL) {
            if(arg->cfg.dbgLvl >= AVB_TEST_DBG_LVL_ERROR)
		printf("t%d: File (%s) open error \n", arg->id, arg->cfg.filename);
            arg->res = -1;
            return &arg->res;
        }
    }

    if((arg->cfg.hwdepname[0] != 0) && (arg->cfg.timestamp != 0)) {
        if((err = snd_hwdep_open(&hwdep, arg->cfg.hwdepname, SND_HWDEP_OPEN_DUPLEX)) < 0) {
            if(arg->cfg.dbgLvl >= AVB_TEST_DBG_LVL_ERROR)
		printf("t%d: Playback hwdep open error: %s\n", arg->id, snd_strerror(err));
            arg->res = -1;
            return &arg->res;
        }
    }

    if((err = snd_pcm_open(&handle, arg->cfg.devname, SND_PCM_STREAM_CAPTURE, 0)) < 0) {
        if(arg->cfg.dbgLvl >= AVB_TEST_DBG_LVL_ERROR)
	  printf("t%d: Record open error: %s\n", arg->id, snd_strerror(err));
        arg->res = -1;
        return &arg->res;
    }

    if((err = snd_pcm_set_params(handle, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, hdr.numchannels, hdr.samplerate, 0, 250000)) < 0) {
        if(arg->cfg.dbgLvl >= AVB_TEST_DBG_LVL_ERROR)
	   printf("t%d: Record opset params error: %s\n", arg->id, snd_strerror(err));
        arg->res = -1;
        return &arg->res;
    }

    if((err = snd_pcm_prepare (handle)) < 0) {
        if(arg->cfg.dbgLvl >= AVB_TEST_DBG_LVL_ERROR)
	   printf ("t%d: Record cannot prepare audio interface for use (%s) %d\n", arg->id, snd_strerror(err), err);
        arg->res = -1;
        return &arg->res;
    }

    snd_pcm_get_params(handle, &ringbufsize, &periodsize);
    if(arg->cfg.dbgLvl >= AVB_TEST_DBG_LVL_INFO)
	printf("t%d: Record params ringbufsize: %li, periodsize: %li\n", arg->id, ringbufsize, periodsize);

    snd_pcm_start(handle);

    while(((numFrames < arg->cfg.maxframes) || (arg->cfg.maxframes == -1)) && (playbackDone == 0)) {
        readFrames = (((arg->cfg.maxframes - numFrames) > AVB_TEST_NUM_FRAMES_IN_BUFFER)?(AVB_TEST_NUM_FRAMES_IN_BUFFER):(arg->cfg.maxframes - numFrames));
        readFrames = ((readFrames < 0)?(AVB_TEST_NUM_FRAMES_IN_BUFFER):(readFrames));
        readFrames = ((readFrames < periodsize)?(readFrames):(periodsize));
        readBytes  = readFrames * hdr.numchannels * (hdr.bps / 8);

        rcvdFrames = snd_pcm_readi(handle, &buf[arg->id][0], readFrames);

	if((numFrames == 0) && (rcvdFrames > 0)) {
		if(arg->cfg.dbgLvl >= AVB_TEST_DBG_LVL_INFO) {
			clock_gettime(arg->cfg.clkId, &tp);
			printf("t%d: First frame received @ %d s %d ns \n");
		}	
	}

        if(rcvdFrames < 0) {
            rcvdFrames = snd_pcm_recover(handle, rcvdFrames, 0);
            snd_pcm_start(handle);
            continue;
        }
        if(rcvdFrames < 0) {
            if(arg->cfg.dbgLvl >= AVB_TEST_DBG_LVL_ERROR)
		printf("t%d: snd_pcm_readi failed: %s (%d)\n", arg->id, snd_strerror(rcvdFrames), rcvdFrames);
            break;
        }

        if((rcvdFrames > 0) && (rcvdFrames < readFrames)) {
            if(arg->cfg.dbgLvl >= AVB_TEST_DBG_LVL_WARNING)
		printf("t%d: Short read (expected %d, read %d)\n", arg->id, readFrames, rcvdFrames);

            if(arg->cfg.maxframes == -1)
              break;
        }

        if((arg->cfg.hwdepname[0] != 0) && (arg->cfg.timestamp != 0)) {
	    if(syncts == 0) {
	       snd_hwdep_ioctl(hwdep, 1, (void*)&ts);
	       syncts = ts;
	       clock_gettime(arg->cfg.clkId, &synctime);
	       if(arg->cfg.dbgLvl >= AVB_TEST_DBG_LVL_INFO)
		  printf("t%d: Record read ts %lu (%lds, %dns)\n", arg->id, ts, synctime.tv_sec, synctime.tv_nsec);
	       getAVTPSt(arg, syncts, &synctime);
	       if(arg->cfg.dbgLvl >= AVB_TEST_DBG_LVL_INFO)
		  printf("t%d: Record synctime (%lds, %dns)\n", arg->id, synctime.tv_sec, synctime.tv_nsec);
	    }
        }

        if(arg->cfg.tobuf == 0)
            fwrite((void*)&buf[arg->id][0], 1, readBytes, fp);
        else {
            memcpy(&audbuf[currRxIdx], &buf[arg->id][0], readBytes);
            audbufsize += readBytes; if(audbufsize > AVB_TEST_AUD_BUFFER_SIZE) assert(0);
            currRxIdx += readBytes; currRxIdx %= AVB_TEST_AUD_BUFFER_SIZE;
        }

        numFrames += rcvdFrames;

	if(arg->cfg.dbgLvl >= AVB_TEST_DBG_LVL_VERBOSE) {
	if(arg->cfg.tobuf == 0)
        	printf("t%d: Record received %d frames\n", arg->id, numFrames);
	else
		printf("t%d: Record received %d frames currRxIdx: %d, audbufsize: %d\n",
			arg->id, numFrames, (currRxIdx / (hdr.numchannels * (hdr.bps / 8))),
			(audbufsize / (hdr.numchannels * (hdr.bps / 8))));
	}
    }

    snd_pcm_drop(handle);

    hdr.fmtsize = 16; hdr.datasize = ((hdr.numchannels * (hdr.bps / 8)) * numFrames); hdr.chunksize = 36 + hdr.datasize;
    if(arg->cfg.dbgLvl >= AVB_TEST_DBG_LVL_VERBOSE)
	printf("t%d: Record received %d frames\n", arg->id, numFrames);

    snd_pcm_close(handle);

    if(arg->cfg.tobuf == 0) {
        fflush(fp);
        fclose(fp);
    }

    if(arg->cfg.tobuf == 0) {
        fi = fopen(&tmpfile[0], "rb");
        fp = fopen(arg->cfg.filename, "wb");

        if((fp == NULL) || (fi == NULL)) {
            if(arg->cfg.dbgLvl >= AVB_TEST_DBG_LVL_ERROR)
		printf("t%d: File open error", arg->id);
            arg->res = -1;
            return &arg->res;
        }

        fwrite((void*)&hdr, sizeof(struct wavhdr), 1, fp);

        while((ferror(fi) == 0) && (feof(fi) == 0)) {
            readBytes = fread((void*)&buf[arg->id][0], 1, AVB_TEST_NUM_BYTES_IN_BUFFER, fi);
            fwrite((void*)&buf[arg->id][0], 1, readBytes, fp);
        }

        fclose(fi);
        fclose(fp);
    }

    return 0;
}

