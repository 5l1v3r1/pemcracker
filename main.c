#include <iostream>
#include <iomanip>
#include <stdio.h>
#include <pthread.h>
#include <queue>
#include "include/Blob.h"
#include <sys/time.h>
#include <openssl/ssl.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>

#ifdef _WIN32 || _WIN64
#include <Windows.h>
#define msleep(x) Sleep(x)
#else
#include <unistd.h>
#define msleep(x) usleep(x * 1000)
#endif

#define ITERATION_REPORT 100000

using namespace std;

vector<pthread_t*> threads;
vector<queue<Blob>* > blobs;
vector<pthread_mutex_t*> mutexes;
volatile bool done = false;
volatile bool started = false;
volatile bool found = false;
volatile unsigned long long iterationCount = 0;
string filePathPEM;
struct timeval startTime;


int GetCoreCount()
{
    int cores = 1;
    #ifdef _WIN32 || _WIN64
        SYSTEM_INFO sysinfo;
        GetSystemInfo( &sysinfo );
        cores = sysinfo.dwNumberOfProcessors;
    #else
        cores = sysconf( _SC_NPROCESSORS_ONLN );
    #endif
    return cores;
}

void *crackThread(void *threadid)
{
    long index = (long)threadid;
    queue<Blob>*  localQueue = blobs.at(index);
    while(!started) msleep(1);
    gettimeofday(&startTime, NULL);


    // Start of precomputation
    pthread_mutex_t * mutex = mutexes.at(index);
    pthread_mutex_lock(mutex);

    FILE * fp = fopen(filePathPEM.c_str(), "rb");
    BIO *kbio;
    fseek(fp, 0, SEEK_END);
    int size = ftell(fp);
    char * kbuf = new char[size];
    fseek(fp, 0, SEEK_SET);
    fread(kbuf, 1, size, fp);
    fclose(fp);
    pthread_mutex_unlock(mutex);

    // Memory version of BIO
    kbio = BIO_new_mem_buf((void*)kbuf, -1);

    Blob * s;
    while(!done && !found)
    {
        while(localQueue->size() > 0 && !found)
        {
            pthread_mutex_lock(mutex);
            s = &localQueue->front();
            pthread_mutex_unlock(mutex);

            EVP_PKEY *pkey = 0;

            if (PEM_read_bio_PrivateKey(kbio, &pkey, NULL, s->data))
            {
                cout << "Password is " << s->data << " for " << filePathPEM.c_str() << endl;
                flush(cout);
                done = true;
                found = true;
                localQueue->pop();
                return NULL;
            }
            BIO_reset(kbio);

            iterationCount++;
            if(iterationCount % ITERATION_REPORT == 0)
            {
                struct timeval now;
                gettimeofday(&now, NULL);
                double seconds = (now.tv_sec + ((double)now.tv_usec / 1000000)) - (startTime.tv_sec + ((double)startTime.tv_usec / 1000000));
                cerr << endl << dec << "Hashes(" << iterationCount << ") Per Second(" << seconds << "s): " << ((double)iterationCount)/(seconds);
            }
            pthread_mutex_lock(mutex);
            localQueue->pop();
            pthread_mutex_unlock(mutex);
        }
    }
    return NULL;
}

int main(int argc, char ** argv)
{
    if(argc == 3)
    {
        OpenSSL_add_all_algorithms();
        FILE *fp = fopen(argv[1], "rb");
        if (fp == NULL) {
            perror(argv[1]);
            exit(1);
        }
        fclose(fp);

        filePathPEM.assign(argv[1]);
        string dictionarylocation(argv[2]);

        // todo Attempt to open PEM
        // todo Validate PEM format


        FILE * dict;
        if(dictionarylocation != "-")
        {
            dict = fopen(dictionarylocation.c_str(), "r");
        }
        else
        {
            dict = stdin;
        }
        if(dict == NULL)
        {
            cerr << "Failed to open dictionary file." << endl;
            return 0;
        }

        done = false;
        started = false;
        int thread_count = GetCoreCount();
        for(long x = 0; x < thread_count; x++)
        {
            pthread_t * cThread = new pthread_t();
            threads.push_back(cThread);
            blobs.push_back(new queue<Blob>());
            pthread_mutex_t * mutex = new pthread_mutex_t();
            *mutex = PTHREAD_MUTEX_INITIALIZER;
            mutexes.push_back(mutex);
            pthread_create(cThread, NULL, crackThread, (void*)x);
        }

        char * password = new char[1024];
        unsigned long long loaded = 0;
        while (!feof(dict) && !done)
        {
            if (fgets(password, 1024, dict) != NULL)
            {
                if(strchr(password, 0x0d) != NULL) *(strchr(password, 0x0d)) = 0x00;
                else if(strchr(password, 0x0a) != NULL) *(strchr(password, 0x0a)) = 0x00;
                if(strlen(password) == 0)
                    continue;
                Blob p(strlen(password), password);
                int count = 0;
                for(int x = 0; x < thread_count; x++)
                {
                    pthread_mutex_lock(mutexes.at(x));
                    count += (blobs.at(x))->size();
                    pthread_mutex_unlock(mutexes.at(x));
                }
                while(count > 100000)
                {
                    msleep(10);
                    count = 0;
                    for(int x = 0; x < thread_count; x++)
                    {
                        pthread_mutex_lock(mutexes.at(x));
                        count += (blobs.at(x))->size();
                        pthread_mutex_unlock(mutexes.at(x));
                    }
                }
                loaded++;
                pthread_mutex_lock(mutexes.at(loaded % thread_count));
                (blobs.at(loaded % thread_count))->push(p);
                pthread_mutex_unlock(mutexes.at(loaded % thread_count));
                started = true;
            }
        }
        fclose(dict);
        int count = 0;
        for(int x = 0; x < thread_count; x++)
        {
            pthread_mutex_lock(mutexes.at(x));
            count += (blobs.at(x))->size();
            pthread_mutex_unlock(mutexes.at(x));
        }
        while(count > 0 && !found)
        {
            msleep(100);
            count = 0;
            for(int x = 0; x < thread_count; x++)
            {
                pthread_mutex_lock(mutexes.at(x));
                count += (blobs.at(x))->size();
                pthread_mutex_unlock(mutexes.at(x));
            }
        }
        cerr << endl;
        done = true;
        for(int x = 0; x < thread_count; x++)
        {
            pthread_join(*threads.at(x), NULL);
        }
        return 0;
    }
    else
    {
        //Print args
        cout << "pemcracker 0.1.0" << endl << "pemcracker <path to pem> <word file>" << endl;
        cout << endl << "pemcracker by Brian Wallace (@botnet_hunter)" << endl;
    }
    return 0;
}