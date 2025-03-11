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

int filter_order;
int num_bands;
signal* sig;

long num_threads;
long num_proc;
pthread_t* tid;

double Fc;
double bandwidth;
double* band_power;

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

void* worker(void* arg) {
    long myid = (long)arg;
    // long mytid = tid[myid];

    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(myid % num_proc, &set);
    if (sched_setaffinity(0,sizeof(set),&set) < 0) {
        perror("Can't setaffinity");
        exit(-1);
    }

    int bbands = num_bands / num_threads;
    int ebands = num_bands % num_threads;
    int start_i = myid * bbands;
    int end_i = bbands;
    if (myid < ebands) {
        start_i += myid;
        end_i += start_i + 1;
    }
    else {
        start_i += ebands;
        end_i += start_i;
    }

    double filter_coeffs[filter_order + 1];

    for (int band = start_i; band < end_i; band++) {
        generate_band_pass(sig->Fs,
                           band * bandwidth + 0.0001,
                           (band + 1) * bandwidth - 0.0001,
                           filter_order,
                           filter_coeffs);
        hamming_window(filter_order,filter_coeffs);
        convolve_and_compute_power(sig->num_samples,
                                   sig->data,
                                   filter_order,
                                   filter_coeffs,
                                   &(band_power[band]));
    }

    pthread_exit(NULL);
}

int analyze_signal(double* lb, double* ub) {

    double max_band_power = max_of(band_power,num_bands);
    double avg_band_power = avg_of(band_power,num_bands);
    int wow = 0;
    *lb = -1;
    *ub = -1;

    for (int band = 0; band < num_bands; band++) {
        double band_low  = band * bandwidth + 0.0001;
        double band_high = (band + 1) * bandwidth - 0.0001;

        printf("%5d %20lf to %20lf Hz: %20lf ",
            band, band_low, band_high, band_power[band]);

        for (int i = 0; i < MAXWIDTH * (band_power[band] / max_band_power); i++) {
        printf("*");
        }

        if ((band_low >= ALIENS_LOW && band_low <= ALIENS_HIGH) ||
            (band_high >= ALIENS_LOW && band_high <= ALIENS_HIGH)) {

        // band of interest
        if (band_power[band] > THRESHOLD * avg_band_power) {
            printf("(WOW)");
            wow = 1;
            if (*lb < 0) {
            *lb = band * bandwidth + 0.0001;
            }
            *ub = (band + 1) * bandwidth - 0.0001;
        } else {
            printf("(meh)");
        }
        } else {
        printf("(meh)");
        }

        printf("\n");
    }
    
    return wow;
}

int main(int argc, char* argv[]) {

    if (argc != 8) {
        usage();
        return -1;
    }

    char sig_type = toupper(argv[1][0]);
    char* sig_file = argv[2];
    double Fs = atof(argv[3]);
    filter_order = atoi(argv[4]);
    num_bands = atoi(argv[5]);

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
    

    // ASSIGN CONSTANTS
    Fc = sig->Fs / 2;
    bandwidth = Fc / num_bands;

    remove_dc(sig->data,sig->num_samples);

    band_power = (double*)malloc(sizeof(double) * num_bands);

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

    printf("Finished starting threads (%ld started)\n", num_started);

    printf("Now joining\n");

    for (long i = 0; i < num_threads; i++) {
        if (tid[i] != 0xdeadbeef) {
            printf("Joining with %ld, tid %lu\n", i, tid[i]);
            int returncode = pthread_join(tid[i], NULL);   //
            if (returncode != 0) {
                printf("Failed to join with %ld!\n", i);
                perror("join failed");
            } else {
                printf("Done joining with %ld\n", i);
            }
        } else {
            printf("Skipping %ld (wasn't started successfully)\n", i);
        }
    }

    double start = 0;
    double end   = 0;
    if (analyze_signal(&start, &end)) {
        printf("POSSIBLE ALIENS %lf-%lf HZ (CENTER %lf HZ)\n", start, end, (end + start) / 2.0);
    } else {
        printf("no aliens\n");
    }

    free_signal(sig);

    return 0;
}
