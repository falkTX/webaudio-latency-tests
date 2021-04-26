
#include <libwebsockets.h>

#include <semaphore.h>
#include <jack/jack.h>
#include <jack/ringbuffer.h>

#undef lwsl_user
#define lwsl_user lwsl_warn

#undef LLL_USER
#define LLL_USER LLL_WARN

// JACK stuff
static jack_ringbuffer_t* jackringbuffer;
static jack_client_t* jackclient;
static jack_port_t* jackport;
static float* audio_pkt;
static sem_t jacksem;

#define RINGBUFFER_SIZE sizeof(float) * 4096 * 16
/* one of these created for each message */
struct msg {
    void *payload; /* is malloc'd */
    size_t len;
};

/* one of these is created for each client connecting to us */
struct per_session_data__minimal {
    struct per_session_data__minimal *pss_list;
    struct lws *wsi;
    int last; /* the last message number we sent */
};

/* one of these is created for each vhost our protocol is used with */
struct per_vhost_data__minimal {
    struct lws_context *context;
    struct lws_vhost *vhost;
    const struct lws_protocols *protocol;

    struct per_session_data__minimal *pss_list; /* linked-list of live pss*/

    struct msg amsg; /* the one pending message... */
    int current; /* the current message number we are caching */
};

/* destroys the message when everyone has had a copy of it */
static void
__minimal_destroy_message(void *_msg)
{
    struct msg *msg = _msg;

    free(msg->payload);
    msg->payload = NULL;
    msg->len = 0;
}

static int
callback_minimal(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len)
{
    struct per_session_data__minimal *pss = (struct per_session_data__minimal *)user;
    struct per_vhost_data__minimal *vhd =
                    (struct per_vhost_data__minimal *)
                    lws_protocol_vh_priv_get(lws_get_vhost(wsi), lws_get_protocol(wsi));

    int m;

    switch (reason) {
    case LWS_CALLBACK_PROTOCOL_INIT:
        vhd = lws_protocol_vh_priv_zalloc(lws_get_vhost(wsi), lws_get_protocol(wsi),
                                          sizeof(struct per_vhost_data__minimal));
        vhd->context = lws_get_context(wsi);
        vhd->protocol = lws_get_protocol(wsi);
        vhd->vhost = lws_get_vhost(wsi);
        lwsl_user("protocol init\n");
        break;

    case LWS_CALLBACK_ESTABLISHED:
        /* add ourselves to the list of live pss held in the vhd */
        lws_ll_fwd_insert(pss, pss_list, vhd->pss_list);
        pss->wsi = wsi;
        pss->last = vhd->current;
        lwsl_user("connetion established\n");
        break;

    case LWS_CALLBACK_CLOSED:
        /* remove our closing pss from the list of live pss */
        lws_ll_fwd_remove(struct per_session_data__minimal, pss_list, pss, vhd->pss_list);
        lwsl_user("connetion close\n");
        break;

    case LWS_CALLBACK_SERVER_WRITEABLE:
        lwsl_user("writable data\n");
        if (!vhd->amsg.payload)
            break;

        if (pss->last == vhd->current)
            break;

        /* notice we allowed for LWS_PRE in the payload already */
        m = lws_write(wsi, ((unsigned char *)vhd->amsg.payload) + LWS_PRE, vhd->amsg.len, LWS_WRITE_TEXT);
        if (m < (int)vhd->amsg.len) {
            lwsl_err("ERROR %d writing to ws\n", m);
            return -1;
        }

        pss->last = vhd->current;
        break;

    case LWS_CALLBACK_RECEIVE:
        lwsl_user("connetion receive\n");
        if (vhd->amsg.payload)
            __minimal_destroy_message(&vhd->amsg);

        vhd->amsg.len = len;
        /* notice we over-allocate by LWS_PRE */
        vhd->amsg.payload = malloc(LWS_PRE + len);
        if (!vhd->amsg.payload) {
            lwsl_user("OOM: dropping\n");
            break;
        }

        memcpy((char *)vhd->amsg.payload + LWS_PRE, in, len);
        vhd->current++;

        /*
          * let everybody know we want to write something on them
          * as soon as they are ready
          */
        lws_start_foreach_llp(struct per_session_data__minimal **, ppss, vhd->pss_list) {
                lws_callback_on_writable((*ppss)->wsi);
        } lws_end_foreach_llp(ppss, pss_list);
        break;

    default:
        break;
    }

    return 0;
}

static struct lws_protocols protocols[] = {
    { "http", lws_callback_http_dummy, 0, 0 },
    {
        "lws-minimal-proxy",
        callback_minimal,
        sizeof(struct per_session_data__minimal),
        128,
        0, NULL, 0
    },
    { NULL, NULL, 0, 0, 0, NULL, 0 } /* terminator */
};

static const struct lws_http_mount mount = {
    .mountpoint = "/",
    .mountpoint_len = 1,
    .origin = "./browser-to-websocket.mount",
    .def = "index.html",
    .origin_protocol = LWSMPRO_FILE,
};

// #if 0
// #define LWS_SERVER_OPTION_HTTP_HEADERS_SECURITY_BEST_PRACTICES_ENFORCE 0
// #endif

static int interrupted;

static void sigint_handler(int sig)
{
    interrupted = 1;
}

static int process_callback(jack_nframes_t nframes, void *arg)
{
    /* Warning: this function runs in realtime. One mustn't allocate memory here
     * or do any other thing that could block. */

    const jack_nframes_t audio_pkt_size = nframes * sizeof(float);
    float* buffer = jack_port_get_buffer(jackport, nframes);

    /* Check if audio is available */
    if (jack_ringbuffer_read_space(jackringbuffer) < audio_pkt_size) {

        memset(buffer, 0, sizeof(float)*nframes);
        //pkt_xrun = 1;
        return 0;
    }

    /* Retrieve audio */
    jack_ringbuffer_read(jackringbuffer, (char*)audio_pkt, audio_pkt_size);

    /* Copy and interleave audio data from the packet into the JACK buffer */
    memcpy(buffer, audio_pkt, audio_pkt_size);

    sem_post(&jacksem);
    return 0;
}


int main()
{
    signal(SIGINT, sigint_handler);

    int extra = LLL_DEBUG;
    lws_set_log_level(LLL_USER | LLL_ERR | LLL_WARN | LLL_NOTICE | extra, NULL);
    lwsl_user("LWS minimal ws proxy | visit http://localhost:7681\n");

    struct lws_context_creation_info info = {
      .port = 8021,
      .mounts = &mount,
      .protocols = protocols,
      .vhost_name = "localhost",
//       .options = LWS_SERVER_OPTION_HTTP_HEADERS_SECURITY_BEST_PRACTICES_ENFORCE,
    };

#if 0
    info.options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    info.ssl_cert_filepath = "localhost-100y.cert";
    info.ssl_private_key_filepath = "localhost-100y.key";
#endif

    struct lws_context* context = lws_create_context(&info);
    if (!context) {
        lwsl_err("lws init failed\n");
        return 1;
    }

    /* Register as a JACK client, using the context url as client name. */
    jackclient = jack_client_open("browser2jack", JackNullOption, NULL);
    if (!jackclient) {
        lwsl_err("Unable to register as a JACK client\n");
        return 1;
    }

    jackport = jack_port_register(jackclient, "output", JACK_DEFAULT_AUDIO_TYPE,
                                  JackPortIsOutput|JackPortIsTerminal|JackPortIsPhysical, 0);

    jackringbuffer = jack_ringbuffer_create(RINGBUFFER_SIZE);
    jack_ringbuffer_mlock(jackringbuffer);

    audio_pkt = (float*)malloc(jack_get_buffer_size(jackclient) * sizeof(float));
    sem_init(&jacksem, 0, 0);

    jack_set_process_callback(jackclient, process_callback, NULL);
    jack_activate(jackclient);

    int n = 0;
    while (n >= 0 && !interrupted)
        n = lws_service(context, 0);

    lws_context_destroy(context);

    jack_deactivate(jackclient);
    jack_client_close(jackclient);

    sem_destroy(&jacksem);
    jack_ringbuffer_free(jackringbuffer);

    return 0;
}
