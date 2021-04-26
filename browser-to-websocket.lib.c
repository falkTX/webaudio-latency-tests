
#include <semaphore.h>
#include <jack/jack.h>
#include <jack/ringbuffer.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

// JACK stuff
static jack_ringbuffer_t* jackringbuffer;
static jack_client_t* jackclient;
static jack_port_t* jackport;
static size_t audio_pkt_size;
static sem_t jacksem;
static volatile bool pkt_xrun;

static int process_callback(jack_nframes_t nframes, void *arg)
{
    float* buffer = jack_port_get_buffer(jackport, nframes);

    const jack_nframes_t audio_pkt_size2 = nframes * sizeof(float);

    if (audio_pkt_size2 != audio_pkt_size)
    {
        memset(buffer, 0, sizeof(float)*nframes);
        // pkt_xrun = true;
        return 0;
    }

    /* Check if audio is available */
    if (jack_ringbuffer_read_space(jackringbuffer) < audio_pkt_size)
    {
        memset(buffer, 0, sizeof(float)*nframes);
        pkt_xrun = true;
        return 0;
    }

    /* Retrieve audio */
    jack_ringbuffer_read(jackringbuffer, (char*)buffer, audio_pkt_size);

    /* Report ready for more */
    sem_post(&jacksem);
    return 0;
}

static void shutdown_callback(void* arg)
{
    jackclient = NULL;
}

bool bbjack_init()
{
    /* Register as a JACK client, using the context url as client name. */
    jackclient = jack_client_open("browser2jack", JackNullOption, NULL);
    if (!jackclient) {
        printf("Unable to register as a JACK client\n");
        return false;
    }

    jackport = jack_port_register(jackclient, "output", JACK_DEFAULT_AUDIO_TYPE,
                                  JackPortIsOutput|JackPortIsTerminal|JackPortIsPhysical, 0);

    audio_pkt_size = jack_get_buffer_size(jackclient) * sizeof(float);
    sem_init(&jacksem, 0, 0);

    jackringbuffer = jack_ringbuffer_create(audio_pkt_size * 64);
    jack_ringbuffer_mlock(jackringbuffer);

    jack_set_process_callback(jackclient, process_callback, NULL);
    jack_on_shutdown(jackclient, shutdown_callback, NULL);
    jack_activate(jackclient);

    return false;
}

void bbjack_cleanup()
{
    jack_deactivate(jackclient);
    jack_client_close(jackclient);
    jackclient = NULL;

    sem_destroy(&jacksem);
    jack_ringbuffer_free(jackringbuffer);
}

static bool activated;

bool bbjack_audiodata(const char* data, unsigned size)
{
    if (jackclient == NULL)
        return false;

    /* Check if there's enough space to send everything as-is */
    if (jack_ringbuffer_write_space(jackringbuffer) >= size) {
        jack_ringbuffer_write(jackringbuffer, data, size);

    /* not everything fits, keep writing and waiting until the entire packet is in the ringbuffer */
    } else {
        struct timespec timeout = {0, 0};

        while (size) {
            if (size >= audio_pkt_size) {
                /* write one pkt size chunk at a time */
                if (jack_ringbuffer_write_space(jackringbuffer) >= audio_pkt_size) {
                    jack_ringbuffer_write(jackringbuffer, data, audio_pkt_size);
                    data += audio_pkt_size;
                    size -= audio_pkt_size;
                } else {
                    clock_gettime(CLOCK_REALTIME, &timeout);
                    timeout.tv_sec += 1;

                    if (sem_timedwait(&jacksem, &timeout)) {
                        if (errno == EINTR)
                            continue;

                        if (errno == ETIMEDOUT)
                            printf("Input error: timed out when waiting for JACK process callback input\n");

                        if (!jackclient) {
                            printf("Input error: JACK server is gone\n");
                        }

                        return false;
                    }
                }
            } else {
                /* final step, only a few samples remain, we just spin spin */
                /* FIXME would it be okay to do timed wait here? processing side will post */
                while (jack_ringbuffer_write_space(jackringbuffer) < size) {}
                jack_ringbuffer_write(jackringbuffer, data, size);
                break;
            }
        }
    }

    if (pkt_xrun) {
        printf("Audio source packet underrun\n");
        pkt_xrun = false;
    }

    return true;
}
