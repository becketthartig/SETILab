#define _GNU_SOURCE
#include <sched.h>    // for processor affinity
#include <unistd.h>   // unix standard apis
#include <pthread.h> 

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <assert.h>

#include "filter.h"
#include "signal.h"
#include "timing.h"

#define MAXWIDTH 40
#define THRESHOLD 2.0
#define ALIENS_LOW  50000.0
#define ALIENS_HIGH 150000.0

long num_threads;
long num_proc;
pthread_t* tid;

int* wows;

void usage() {
    printf("usage: p_band_scan text|bin|mmap signal_file Fs filter_order num_bands num_threads num_processors\n");
}

double avg_power(double* data, int num) {

    double ss = 0;
    for (int i = 0; i < num; i++) {
        ss += data[i] * data[i];
    }
  
    return ss / num;
}

double max_of(double* data, int num) {

    double m = data[0];
    for (int i = 1; i < num; i++) {
        if (data[i] > m) {
        m = data[i];
        }
    }
    return m;
}

double avg_of(double* data, int num) {

    double s = 0;
    for (int i = 0; i < num; i++) {
        s += data[i];
    }
    return s / num;
}

void remove_dc(double* data, int num) {

    double dc = avg_of(data,num);

    printf("Removing DC component of %lf\n",dc);

    for (int i = 0; i < num; i++) {
        data[i] -= dc;
    }
}

int analyze_signal(signal* sig, int filter_order, int num_bands, double* lb, double* ub) {

    int wow = 0;

    return wow;
}

void* worker(void* arg) {
    long myid = (long)arg;
    long mytid = tid[myid];

    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(myid % num_proc, &set);
    if (sched_setaffinity(0,sizeof(set),&set) < 0) {
        perror("Can't setaffinity");
        exit(-1);
    }


    pthread_exit(NULL);
}

int main(int argc, char* argv[]) {

    if (argc != 8) {
        usage();
        return -1;
    }

    char sig_type    = toupper(argv[1][0]);
    char* sig_file   = argv[2];
    double Fs        = atof(argv[3]);
    int filter_order = atoi(argv[4]);
    int num_bands    = atoi(argv[5]);

    assert(Fs > 0.0);
    assert(filter_order > 0 && !(filter_order & 0x1));
    assert(num_bands > 0);

    printf("type:     %s\n\
file:     %s\n\
Fs:       %lf Hz\n\
order:    %d\n\
bands:    %d\n",
           sig_type == 'T' ? "Text" : (sig_type == 'B' ? "Binary" : (sig_type == 'M' ? "Mapped Binary" : "UNKNOWN TYPE")),
           sig_file,
           Fs,
           filter_order,
           num_bands);

    printf("Load or map file\n");

    signal* sig;
    switch (sig_type) {
        case 'T':
        sig = load_text_format_signal(sig_file);
        break;

        case 'B':
        sig = load_binary_format_signal(sig_file);
        break;

        case 'M':
        sig = map_binary_format_signal(sig_file);
        break;

        default:
        printf("Unknown signal type\n");
        return -1;
    }

    if (!sig) {
        printf("Unable to load or map file\n");
        return -1;
    }

    sig->Fs = Fs;

    num_threads = atoi(argv[6]);
    num_proc = atoi(argv[7]);
    
    tid = (pthread_t*)malloc(sizeof(pthread_t) * num_threads);

    long num_started = 0;
    for (long i = 0; i < num_threads; i++) {
        int returncode = pthread_create(&(tid[i]), // thread id gets put here
                                        NULL, // use default attributes
                                        worker, // thread will begin in this function
                                        (void*)i // we'll give it i as the argument
                                        );
        if (returncode == 0) {
            printf("Started thread %ld, tid %lu\n", i, tid[i]);
            num_started++;
        } else {
            printf("Failed to start thread %ld\n", i);
            perror("Failed to start thread");
            tid[i] = 0xdeadbeef;
        }
    }

    return 0;
}
