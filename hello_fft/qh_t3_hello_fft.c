/*
BCM2835 "GPU_FFT" release 3.0
Copyright (c) 2015, Andrew Holme.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the copyright holder nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
// """
// Created: 2/17/2018
// desciption:
// hello_fft with function of auto size and output to .csv
// Author:Qihao He
// """

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include "mailbox.h"
#include "gpu_fft.h"

char Usage[] =
    "Usage: hello_fft.bin log2_N [log2_M [jobs [loops [RMS_C]]]]\n"
    "log2_N = log2(FFT_length),       log2_N = 8...22\n"
    "log2_M = log2(FFT_length),       log2_M > log2_N\n"
    "jobs   = transforms per batch,   jobs>0,        default 1\n"
    "loops  = number of test repeats, loops>0,       default 1\n"
    "RMS_C  = number of test repeats, T(1),F(0),     default 0\n";

unsigned Microseconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec*1000000 + ts.tv_nsec/1000;
}

int main(int argc, char *argv[]) {
    int i, j, k, l, ret, loops, freq, log2_N, log2_M, jobs, N, mb = mbox_open(),
     RMS_C, span_log2_N;
    unsigned t[4];
    double tsq[2];

    struct GPU_FFT_COMPLEX *base;
    struct GPU_FFT *fft;

    log2_N = argc>1? atoi(argv[1]) : 12; // 8 <= log2_N <= 22
    log2_M = argc>2? atoi(argv[2]) : log2_N + 1; // 8 <= log2_N <= 22
    jobs   = argc>3? atoi(argv[3]) : 1;  // transforms per batch
    loops  = argc>4? atoi(argv[4]) : 1;  // test repetitions
    RMS_C  = argc>5? atoi(argv[5]) : 1;  // RMS_controller

    if (!(argc>=2 && argc<=6) || jobs<1 || loops<1 || !(RMS_C>=0 && RMS_C<=1) ||
    log2_N >= log2_M ) {
        printf(Usage);
        return -1;
    }

    span_log2_N =log2_M - log2_N;

    double time_elapsed[span_log2_N][loops][4]; //3D array
    for(i=0; i<span_log2_N; i++){
      for(j=0; j<loops; j++){
        for(k=0; k<4; k++){
          time_elapsed[i][j][k] = 0;
        }
      }
    }
    double REL_RMS_ERR[span_log2_N][loops]; //2D array
    for(i=0; i<span_log2_N; i++){
      for(j=0; j<loops; j++){
        REL_RMS_ERR[i][j] = 0;
      }
    }

    printf("log2_N,N,Init_T,FFT_T,RMS_T,Total_T\n");

    for(l=0; l<span_log2_N; l++){
        N = 1<<(log2_N + l); // FFT length
        ret = gpu_fft_prepare(mb, log2_N + l, GPU_FFT_REV, jobs, &fft); // call once

        switch(ret) {
            case -1: printf("Unable to enable V3D. Please check your firmware is up to date.\n"); return -1;
            case -2: printf("log2_N=%d not supported.  Try between 8 and 22.\n", log2_N + l);         return -1;
            case -3: printf("Out of memory.  Try a smaller batch or increase GPU memory.\n");     return -1;
            case -4: printf("Unable to map Videocore peripherals into ARM memory space.\n");      return -1;
            case -5: printf("Can't open libbcm_host.\n");                                         return -1;
        }

        for (k=0; k<loops; k++) {
            t[0] = Microseconds();

            for (j=0; j<jobs; j++) {
                base = fft->in + j*fft->step; // input buffer
                for (i=0; i<N; i++) base[i].re = base[i].im = 0;
                freq = j+1;
                base[freq].re = base[N-freq].re = 0.5;
            }

            usleep(1); // Yield to OS
            t[1] = Microseconds();
            gpu_fft_execute(fft); // call one or many times
            t[2] = Microseconds();

            if(RMS_C == 1){
                tsq[0]=tsq[1]=0;
                for (j=0; j<jobs; j++) {
                    base = fft->out + j*fft->step; // output buffer
                    freq = j+1;
                    for (i=0; i<N; i++) {
                        double re = cos(2*GPU_FFT_PI*freq*i/N);
                        tsq[0] += pow(re, 2);
                        tsq[1] += pow(re - base[i].re, 2) + pow(base[i].im, 2);
                    }
                    REL_RMS_ERR[l][k] = sqrt(tsq[1] / tsq[0]);
                }
            }
            t[3] = Microseconds();
            printf("%i,%i,%d,%d,%d,%d\n",log2_N + l,N,t[1] - t[0],t[2] - t[1],
            t[3] - t[2],t[3] - t[0]);
        }
        gpu_fft_release(fft); // Videocore memory lost if not freed !
    }


    if(RMS_C == 1){
      for (i = 0; i < span_log2_N; i++) {
        printf("REL_RMS_ERR for log2_N:%d\n", log2_N + i);
          for (j = 0; j < loops; j++) {
            printf("%.8f,",REL_RMS_ERR[i][j]);
          }
          printf("\n");
      }
      printf("\n");
    }

    // gpu_fft_release(fft); // Videocore memory lost if not freed !
    return 0;
}
