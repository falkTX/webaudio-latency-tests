
#include <libwebsockets.h>

/* one of these created for each message */
struct msg {
        void *payload; /* is malloc'd */
        size_t len;
};

/* one of these is created for each client connecting to us */
struct per_session_data__minimal {
        struct per_session_data__minimal *pss_list;
        struct lws *wsi;
        uint32_t tail;
};

static int
callback_minimal(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len)
{
    struct per_session_data__minimal *pss = (struct per_session_data__minimal *)user;
    /*
    struct per_vhost_data__minimal *vhd =
                    (struct per_vhost_data__minimal *)
                    lws_protocol_vh_priv_get(lws_get_vhost(wsi), lws_get_protocol(wsi));
    const struct msg *pmsg;
    struct msg amsg;
    */

    return 0;
}

static struct lws_protocols protocols[] = {
//         { "http", lws_callback_http_dummy, 0, 0 },
    {
        "lws-minimal-proxy",
        callback_minimal,
        sizeof(struct per_session_data__minimal),
        128,
        0, NULL,
#if 0
        0
#endif
    },
    { NULL, NULL, 0, 0 } /* terminator */
};

static const struct lws_http_mount mount = {
    .mount_next = NULL,		/* linked-list "next" */
    .mountpoint = "/",		/* mountpoint URL */
    .origin = 	"./browser-to-websocket.mount", /* serve from dir */
    .def = "index.html",	/* default filename */
#if 0
    .protocol = NULL,
#endif
    .cgienv = NULL,
    .extra_mimetypes = NULL,
#if 0
    .interpret = NULL,
#endif
    .cgi_timeout = 0,
    .cache_max_age = 0,
#if 0
    .auth_mask = 0,
#endif
    .cache_reusable = 0,
    .cache_revalidate = 0,
    .cache_intermediaries = 0,
    .origin_protocol = LWSMPRO_FILE,	/* files in a dir */
    .mountpoint_len = 1,		/* char count */
#if 0
    .basic_auth_login_file = NULL,
#endif
};

#if 1
#define LWS_SERVER_OPTION_HTTP_HEADERS_SECURITY_BEST_PRACTICES_ENFORCE 0
#define LLL_USER LLL_NOTICE
#define lwsl_user lwsl_notice
#endif

static int interrupted;

void sigint_handler(int sig)
{
    interrupted = 1;
}

int main()
{
    struct lws_context_creation_info info;
    struct lws_context *context;
    const char *p;
    int n = 0, logs = LLL_USER | LLL_ERR | LLL_WARN | LLL_NOTICE
                    /* for LLL_ verbosity above NOTICE to be built into lws,
                        * lws must have been configured and built with
                        * -DCMAKE_BUILD_TYPE=DEBUG instead of =RELEASE */
                    /* | LLL_INFO */ /* | LLL_PARSER */ /* | LLL_HEADER */
                    /* | LLL_EXT */ /* | LLL_CLIENT */ /* | LLL_LATENCY */
                    /* | LLL_DEBUG */;

    signal(SIGINT, sigint_handler);

    lws_set_log_level(logs, NULL);
    lwsl_user("LWS minimal ws proxy | visit http://localhost:7681\n");

    memset(&info, 0, sizeof info); /* otherwise uninitialized garbage */
    /*
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT |
                LWS_SERVER_OPTION_HTTP_HEADERS_SECURITY_BEST_PRACTICES_ENFORCE;
    */
    info.port = 7681;
    info.mounts = &mount;
    info.protocols = protocols;

    context = lws_create_context(&info);
    if (!context) {
        lwsl_err("lws init failed\n");
        return 1;
    }

    while (n >= 0 && !interrupted)
        n = lws_service(context, 0);

    lws_context_destroy(context);

    return 0;
}
