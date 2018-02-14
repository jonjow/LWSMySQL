/*
    compile: gcc -O0 -ggdb -fno-builtin -o webserver webserver.c -luv -lwebsockets
*/

#include <stdio.h>
#include <stdlib.h>

#include <sys/time.h>

#include <uv.h>

#include <libwebsockets.h>

#define WEBSERVER_HOST_PORT 7688

typedef struct WebServer_t WebServer;

struct WebServer_t {
	uv_loop_t *loop;

	uv_signal_t sigint;
	uv_signal_t sigterm;

	uv_timer_t timer;

	struct lws_context *context;
};

WebServer *g_web_server = NULL;

/* http server gets files from this path */
#define LOCAL_RESOURCE_PATH "web"
char *resource_path = LOCAL_RESOURCE_PATH;

enum WEBSERVER_PROTOCOLS {
	/* always first */
	WEBSERVER_PROTOCOL_HTTP = 0,

	/* always last */
	WEBSERVER_PROTOCOL_NONE
};

struct per_session_data__http {
	char cookie[512];
};

const char *WebServer_getMimeType(const char *file)
{
	int n = strlen(file);

	if (n < 5)
		return NULL;

	if (!strcmp(&file[n - 4], ".ico"))
		return "image/x-icon";

	if (!strcmp(&file[n - 4], ".png"))
		return "image/png";

	if (!strcmp(&file[n - 4], ".gif"))
		return "image/gif";		

	if (!strcmp(&file[n - 4], ".jpg"))
		return "image/jpg";	

	if (!strcmp(&file[n - 5], ".jpeg"))
		return "image/jpeg";	

	if (!strcmp(&file[n - 5], ".html"))
		return "text/html";

	if (!strcmp(&file[n - 4], ".css"))
		return "text/css";

	if (!strcmp(&file[n - 3], ".js"))
		return "text/javascript";

	if (!strcmp(&file[n - 4], ".ttf"))
		return "application/octet-stream";

	if (!strcmp(&file[n - 5], ".woff"))
		return "application/octet-stream";

	if (!strcmp(&file[n - 6], ".woff2"))
		return "application/octet-stream";		

	if (!strcmp(&file[n - 4], ".map"))
		return "application/octet-stream";	

	return NULL;
}

/* this protocol server (always the first one) handles external poll */
int WebServer_callbackHTTP(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len)
{
	struct per_session_data__http *pss = (struct per_session_data__http *)user;
	unsigned char buffer[4096 + LWS_PRE];
	unsigned long sent;
	char leaf_path[1024];
	const char *mimetype;
	char *other_headers;
	struct timeval tv;
	unsigned char *end, *start;
	unsigned char *p;
    char b64[64];
	char buf[256];
	int n, m;

	char cookie[1024];

	switch (reason) {
		case LWS_CALLBACK_HTTP :
			printf("LWS_CALLBACK_HTTP\n");

			if (len < 1) {
				lws_return_http_status(wsi, HTTP_STATUS_BAD_REQUEST, NULL);
				printf("HTTP_STATUS_BAD_REQUEST\n");
				goto try_to_reuse;
			}
	
			/* if a legal POST URL, let it continue and accept data */
			if (lws_hdr_total_length(wsi, WSI_TOKEN_POST_URI)) {
				/* else something else working out where to handle api ?? */
				return 0;
			}

			/* copy the resource path */
			strcpy(buf, resource_path);			

			if(len)	{
				int compare = strcmp(in, "/");
				if(compare) {  /* if we have a file to serve */
					if (*((const char *)in) != '/') {
						strcat(buf, "/");
					}
					strncat(buf, in, sizeof(buf) - strlen(buf) - 1);
				} else { /* default file to serve */
					strcat(buf, "/index.html");
				}				
			}

			buf[sizeof(buf) - 1] = '\0';

			/* refuse to serve files we don't understand */
			mimetype = WebServer_getMimeType(buf);
			if (!mimetype) {
				lws_return_http_status(wsi, HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE, NULL);
				goto terminate;
			}

            other_headers = leaf_path;
            p = (unsigned char *)leaf_path;
            if (!strcmp((const char *)in, "/") &&
                !lws_hdr_total_length(wsi, WSI_TOKEN_HTTP_COOKIE)) {
                /* this isn't very unguessable but it'll do for us */
                gettimeofday(&tv, NULL);
                n = sprintf(b64, "test=LWS_%u_%u_COOKIE;Max-Age=360000",
                    (unsigned int)tv.tv_sec,
                    (unsigned int)tv.tv_usec);

                if (lws_add_http_header_by_name(wsi,
                    (unsigned char *)"set-cookie:",
                    (unsigned char *)b64, n, &p,
                    (unsigned char *)leaf_path + sizeof(leaf_path)))
                    return 1;
            }

			n = (char *)p - leaf_path;

			n = lws_serve_http_file(wsi, buf, mimetype, other_headers, n);
			if (n < 0 || ((n > 0) && lws_http_transaction_completed(wsi))) {
				goto terminate; /* error or can't reuse connection: close the socket */
			}

			/*
			* notice that the sending of the file completes asynchronously,
			* we'll get a LWS_CALLBACK_HTTP_FILE_COMPLETION callback when
			* it's done
			*/
			break;

		case LWS_CALLBACK_HTTP_BODY :
			printf("LWS_CALLBACK_HTTP_BODY\n");
			break;

		case LWS_CALLBACK_HTTP_BODY_COMPLETION :		
			printf("LWS_CALLBACK_HTTP_BODY_COMPLETION\n");
			goto try_to_reuse;

		case LWS_CALLBACK_HTTP_DROP_PROTOCOL :
			/* called when our wsi user_space is going to be destroyed */
			printf("LWS_CALLBACK_HTTP_DROP_PROTOCOL\n");
			break;

		case LWS_CALLBACK_HTTP_FILE_COMPLETION :
			goto try_to_reuse;

		case LWS_CALLBACK_HTTP_WRITEABLE :
			printf("LWS_CALLBACK_HTTP_WRITEABLE\n");
			return -1;

		case LWS_CALLBACK_CLOSED_CLIENT_HTTP:
			printf("LWS_CALLBACK_CLOSED_CLIENT_HTTP\n");
			return -1;

		default:
			break;
	}

	return 0;

	/* if we're on HTTP1.1 or 2.0, will keep the idle connection alive */
try_to_reuse :
	if (lws_http_transaction_completed(wsi)) {	
		return -1;
	}
	return 0;

terminate :
	return -1;

failed :
	return 1;

later:
	lws_callback_on_writable(wsi);
	return 0;	
}

int WebServer_callbackWS(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len)
{
    unsigned char buf[LWS_PRE + 512];
    struct per_session_data__dumb_increment *pss = (struct per_session_data__dumb_increment *)user;
    unsigned char *p = &buf[LWS_PRE];
    int n, m;

    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED :
			printf("LWS_CALLBACK_ESTABLISHED\n");
            break;

        case LWS_CALLBACK_RECEIVE :
			printf("LWS_CALLBACK_RECEIVE\n");
            break;
        
        case LWS_CALLBACK_SERVER_WRITEABLE :
			printf("LWS_CALLBACK_SERVER_WRITEABLE\n");
            break;

        case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION :
			printf("LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION\n");
            break;

        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR :
			printf("LWS_CALLBACK_CLIENT_CONNECTION_ERROR\n");
            break;

        case LWS_CALLBACK_CLOSED :
			printf("LWS_CALLBACK_CLOSED\n");
            break;

        case LWS_CALLBACK_WS_PEER_INITIATED_CLOSE :
			printf("LWS_CALLBACK_WS_PEER_INITIATED_CLOSE\n");
            break;

        default:
            break;
    }

    return 0;
}

/* list of supported protocols and callbacks */
static struct lws_protocols protocols[] = {
	/* first protocol must always be HTTP handler */
	{
		"http-only", /* name */
		WebServer_callbackHTTP, /* callback */
		sizeof (struct per_session_data__http), /* per_session_data_size */
		0, /* max frame size / rx buffer */
		0,
		NULL
	},

	{
		"websocket-protocol",
		WebServer_callbackWS,
		64,
		64, /* rx buf size must be >= permessage-deflate rx size */
		0,
		NULL
	},

	{ NULL, NULL, 0, 0, 0, NULL } /* terminator */
};

static const struct lws_extension exts[] = {
	{ NULL, NULL, NULL /* terminator */ }
};

static void WebServer_createLWS(WebServer *ws)
{
    struct lws_context_creation_info info;
    const char *iface = NULL;

	memset(&info, 0, sizeof info);
	info.port = WEBSERVER_HOST_PORT;

	info.iface = iface;
	info.protocols = protocols;
	info.extensions = exts;

	info.ssl_cert_filepath = NULL;
	info.ssl_private_key_filepath = NULL;

	info.gid = -1;
	info.uid = -1;
	info.max_http_header_pool = 1;
	info.timeout_secs = 5;
	info.options = LWS_SERVER_OPTION_LIBUV;

	ws->context = lws_create_context(&info);

	/* we have our own uv loop outside of lws */
    lws_uv_initloop(ws->context, ws->loop, 0);
}

static void WebServer_closeLWS(WebServer *ws)
{
    /* detach lws */
    lws_context_destroy(ws->context);
    ws->context = NULL;
}

static void WebServer_stopLWS(WebServer *ws)
{
    if(ws->context) {
        lws_libuv_stop(ws->context);
    }
}

static int WebServer_run(WebServer *ws)
{
    printf("Running..\n");
    
    /* Runs the event loop until there are no more active and 
        referenced handles or requests. */
    return uv_run(ws->loop, UV_RUN_DEFAULT);
}

static void WebServer_signal(uv_signal_t *handle, int signum)
{
    printf("Shutdown triggered..\n");

    if(g_web_server) {
        uv_signal_stop(handle);

        /* Stop the event loop, causing uv_run() to end as soon as possible */
        uv_stop(g_web_server->loop);
    }
}

static void WebServer_timer(uv_timer_t *handle)
{
    printf("timeout..\n");
}

static void WebServer_timerClose(uv_handle_t *handle)
{
    printf("timeout close handles..\n");
}

static WebServer *WebServer_create(void)
{
    WebServer *ws = (WebServer *)calloc(1, sizeof(*ws));

    /* uv_loop_t* uv_default_loop(void)
    Returns the initialized default loop. It may return NULL in case of allocation failure. */
    ws->loop = uv_default_loop();
    if(ws->loop == NULL) {
        printf("Failed to allocate uv default loop...\n");
        free(ws);
        return NULL;
    }

    uv_signal_init(ws->loop, &ws->sigterm);
    uv_signal_start(&ws->sigterm, WebServer_signal, SIGTERM);

    uv_signal_init(ws->loop, &ws->sigint);
    uv_signal_start(&ws->sigint, WebServer_signal, SIGINT);

    /* setup a 1 second repeated timeout as a test */
    uv_timer_init(ws->loop, &ws->timer);
    uv_timer_start(&ws->timer, WebServer_timer, 1000, 1000);

    WebServer_createLWS(ws);

    return ws;
}

static void WebServer_close(uv_handle_t *handle)
{
    printf("Handles still active %u...\n", g_web_server->loop->active_handles);

    if(g_web_server->loop->active_handles == 0) {
        /* Stop the event loop, causing uv_run() to end as soon as possible. */
        uv_stop(g_web_server->loop);
    }
}

static void WebServer_walk(uv_handle_t *handle, void *arg)
{
    printf("Walking through remaining handles...\n");

    /* Request handle to be closed. close_cb will be called asynchronously after this call. 
        This MUST be called on each handle before memory is released.*/
    uv_close(handle, WebServer_close);
}

static void WebServer_destroy(WebServer *ws)
{
    printf("Web Server destroying instance...\n");

    if(ws) {
		WebServer_stopLWS(ws);
		WebServer_closeLWS(ws);

        /* Stop and close the timer */
        uv_timer_stop(&ws->timer);
        uv_close((uv_handle_t *)&ws->timer, WebServer_timerClose);

        /* Stop handling the signals */
        uv_signal_stop(&ws->sigterm);
        uv_signal_stop(&ws->sigint);

        /* Run the service again so closing can be handled */
        WebServer_run(ws);

        /* void uv_walk(uv_loop_t* loop, uv_walk_cb walk_cb, void* arg)
        Walk the list of handles: walk_cb will be executed with the given arg. */
        uv_walk(ws->loop, WebServer_walk, NULL);
        
        /* Runing the service again so closing can be handled */
        WebServer_run(ws);

        /* free instance */
        free(ws);
    }
}

int main(int argc, char *argv[])
{
    /* allocate instance of webserver */
    WebServer *ws = g_web_server = WebServer_create();
    
    printf("Web Server Starting...\n");
    
    if(ws) {
        if(WebServer_run(ws) < 0) {
            /* uv_stop() was called 
                and there are still active handles or requests. */
            printf("Exiting with active handles...\n");
        } else {
            /* Returns zero in all other cases. */
            printf("Exiting...\n");
        }

        WebServer_destroy(ws);
    }

    printf("Web Server terminated...\n");
}
