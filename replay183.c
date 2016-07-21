/* replay183: Program to replay data in nmea-0183 format with sentences
 * preceded by TAG timestamps in either second or millisecond resolution.
 * such logs may be recorded by kplex (add "timestamp=ms" to a file interface
 * declaration).
 * Copyright Keith Young 2015
 * This is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License or (at your option) any later
 * version.
 * See the file COPYING for full details
 */
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <signal.h>
#include <stdlib.h>

#define MAXBUF 92
#define PROGNAME "replay183"
#define VERSION "1.1"

long long saywhen ( long long, struct itimerval *);
void inittime (long long);

/* no-op handler for SIGALRM */
void alrm(int s)
{}

/* convert string time TAG to millisecond representation */
long long strtot(char* ptr)
{
    long long retval;
    int len;

    errno = 0;

    if ((retval = strtoll(ptr,NULL,10))) {
        if (errno == ERANGE)
            return(0);
        if ((len=strlen(ptr)) >= 10 && len < 13)
        /* The only way you can tell it's seconds... */ 
            retval *=1000;
        else if (len < 10 || len > 15)
            retval=0;
        /* implicit else it's milliseconds already */
    }

    return retval;
}

/* extract next TAG timestamped sentence */
/* Returns millisecond timestamp and populates sentence buffer with associated
 * sentence.  Returns -1 on error or EOF */
long long nextsen(FILE *f, char *sen)
{
    char c, *ptr;
    int count;
    long long sentime = 0;
    enum {  SEN_NODATA,
            SEN_TAGFIRST,
            SEN_TAG,
            SEN_TAGSEEN,
            SEN_PROC,
            SEN_TIME} senstate;

    /* initialise state machine */
    senstate=SEN_NODATA;

    /* Loop until we get a timestamped sentence, error or EOF */
    while ((c = getc(f)) != EOF) {
        switch(c) {
            case '$':
            case '!':
                /* sentence start delimiter */
                if (senstate != SEN_TAGSEEN) {
                    senstate=SEN_NODATA;
                    continue;
                }
                senstate=SEN_PROC;
                count=1;
                ptr=sen;
                *ptr++=c;
                continue;
            case '\\':
                /* TAG delimiter */
                if (senstate==SEN_TIME) {
                    *ptr='\0';
                    if ((sentime=strtot(sen)))
                        senstate=SEN_TAGSEEN;
                    else
                        senstate=SEN_NODATA;
                } else if (senstate==SEN_TAG) {
                    if (sentime)
                        senstate=SEN_TAGSEEN;
                    else
                        senstate=SEN_NODATA;
                } else {
                    senstate=SEN_TAGFIRST;
                }
                continue;
            case '\n':
            case '\0':
                /* End of line (the NULL shouldn't happen...) */
                if (senstate == SEN_PROC) {
                    /* if termination was \r\n, back up */
                    if (*(ptr-1) == '\r')
                        --ptr;
                    /* we send back sentence without terminator to let
                     * the user decide on their termination */
                    *ptr = '\0';
                    return(sentime);
                }
                senstate = SEN_NODATA;
                continue;
            case ',':
                /* separator for distinct TAG elements */
                if (senstate==SEN_TAG)
                    senstate=SEN_TAGFIRST;
                else if (senstate==SEN_TIME) {
                    *ptr++='\0';
                    sentime=strtot(sen);
                    senstate=SEN_TAG;
                } else if (senstate==SEN_PROC)
                    break;
                continue;
            case 'c':
                /* TAG code for timestamp */
                if (senstate == SEN_PROC)
                    break;
                else if (senstate==SEN_TAGFIRST) {
                    if (sentime != 0) {
                        senstate = SEN_NODATA;
                        sentime = 0;
                    } else if ((c = getc(f)) == ':') {
                        senstate = SEN_TIME;
                        ptr=sen;
                        count=0;
                    } else
                        senstate = SEN_NODATA;
                }
                continue;
            case '*':
                /* checksum */
                if (senstate == SEN_TIME) {
                    *ptr++='\0';
                    sentime=strtot(sen);
                    senstate=SEN_TAG;
                    continue;
                }
            default:
                if ((senstate==SEN_PROC) || (senstate==SEN_TIME))
                    break;

                continue;
        }

        if (++count < MAXBUF) {
            *ptr++ = c;
            continue;
        }
        senstate=SEN_NODATA;
    }
    return(-1);
}

int main(int argc, char **argv)
{
    FILE *f;
    int c,loop=0,err=0;
    long long t=0, n=1;
    sigset_t sm,om;
    char sentence[MAXBUF];
    char *terminator="\n";
    struct itimerval when;
    struct sigaction sa;
    struct timeval delay = {1,0};

    while ((c = getopt(argc,argv,"Vrcd:n:")) != -1) {
        switch(c) {
        case 'r':
            terminator="\r\n";
            break;
        case 'c':
            loop=1;
            break;
        case 'd':
            if ((delay.tv_usec=strtol(optarg,NULL,10)) == 0 && (errno)) {
                fprintf(stderr,"Invalid delay: %s\n",optarg);
                err++;
            } else {
                if ((delay.tv_sec = delay.tv_usec / 1000)) {
                    delay.tv_usec %= 1000;
                }
                delay.tv_usec *= 1000;
            }
            break;
        case 'n':
            if ((n=strtol(optarg,NULL,10)) == 0 && (errno)) {
                fprintf(stderr,"Invalid repetitions: %s\n",optarg);
                err++;
            }
            break;
        case 'V':
            printf("%s Version %s\n",PROGNAME,VERSION);
            return 0;
            break;
        case '?':
        default:
            err++;
            break;
        }
    }

    if (err || (argc == optind)) {
        fprintf(stderr,"Usage: %s [-r] [-c | -n <repetitions> ] [-d <delay> ] filename ...\n       %s -V\n",PROGNAME,PROGNAME);
        return(1);
    }
    argc-=optind;
    argv+=optind;



    /* set line buffering in case we want to pipe this to something */
    setlinebuf(stdout);

    /* We don't want to automatically re-set the interval timer so make
     * it_interval 0 */
    when.it_interval.tv_sec=0;
    when.it_interval.tv_usec=0;

    /* Block SIGARLM */
    sigemptyset(&sm);
    sigaddset(&sm,SIGALRM);
    sigprocmask(SIG_BLOCK,&sm,&om);

    /* Set up (no-op) handler for SIGALRM */
    sa.sa_handler = alrm;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags=0;
    sigaction(SIGALRM,&sa,NULL);

    while (n) {
        for (c=0;c<argc;c++) {
            if ((delay.tv_sec || delay.tv_usec) && t) {
                /* inter-file time set and not first file */
                when.it_value.tv_sec = delay.tv_sec;
                when.it_value.tv_usec = delay.tv_usec;
                setitimer(ITIMER_REAL, &when,NULL);
                sigsuspend(&om);
            }

            if ((f=fopen(argv[c],"r")) == NULL) {
                fprintf(stderr,"%s: Could not open %s: %s\n",PROGNAME,argv[c],
                    strerror(errno));
                return(1);
            }

            /* Get first timestamped sentence */
            if ((t = nextsen(f,sentence)) < 0) {
                fprintf(stderr,"%s does not contain timestamped data\n",
                    argv[c]);
                fclose(f);
                return(1);
            }

            printf("%s%s",sentence,terminator);

            /* initialise the base time to "now".  differences between future
             * timestamps and the first one will reflect when, relative to now,
             * each sentence is sent
             */
            inittime(t);

            /* Main loop: until the end of file, loop getting timestamped
             * sentences. Transmit those at points relative at the same time
             * relative to the first one as when they were "recorded"
             */
            while ((t = nextsen(f,sentence)) > 0) {
                if (saywhen(t,&when) > 0) {
                    setitimer(ITIMER_REAL, &when,NULL);
                    /* This changes the signal mask to unblock SIGALRM. When the
                     *  timer fires (even if it already has) the no-op handler
                     * is called and processing continues
                     */
                    sigsuspend(&om);
                }
                printf("%s%s",sentence,terminator);
            }
            fclose(f);
        }
        if (!loop)
            --n;
    }

    /* Restore original signal mask before exit */
    sigprocmask(SIG_SETMASK,&om,NULL);
    return(0);
}

long long basetime;      /* Time we started the replay (ms since the epoch) */

/* Record time we started replay */
void inittime(long long t)
{
    struct timeval tv;

    gettimeofday(&tv,NULL);
    basetime=(tv.tv_sec*1000 + tv.tv_usec/1000) - t;
}

/* Work out how long until the next sentence needs playing
 * Returns the number of ms until the next sentence */
long long saywhen(long long t, struct itimerval *when)
{
    long long s,now;
    struct timeval tv;

    gettimeofday(&tv, NULL);
    now=tv.tv_sec*1000+tv.tv_usec/1000;
    now-=basetime;
    if ((t-=now)<0)
        return 0;
    when->it_value.tv_sec=t/1000;
    when->it_value.tv_usec=(t-(when->it_value.tv_sec)*1000)*1000;
    return  t;
}
