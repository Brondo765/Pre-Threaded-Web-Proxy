#include <stdio.h>
#include <csapp.h>
#include <sbuf.h>
#include <dict.h>
#include "proxy.h"
#include "cache.h"


#define DEFAULT_PORT 80
#define NTHREADS 64
#define SBUFSIZE 1024

sbuf_t sbuf;

/* Prototype functions */
static void usage(const char*);
static void *thread(void*);
void serve_client(int);
void clienterror(int, char*, char*, char*, char*);
void read_request_headers(rio_t*, dict_t*);
int parse_url(char*, char*, char*, char*);

/* Usage function to assist in format on command line */
static void usage (const char *progname) {
	fprintf (stderr, "usage: %s PORT\n", progname);
	exit (1);
}

/* Prints diagnostic information to client on error */
void clienterror(int connected_fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
    char buf[MAXLINE];
	sprintf(buf, "\r\n");
    rio_writen(connected_fd, buf, strlen (buf));
    sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
    rio_writen (connected_fd, buf, strlen (buf));
    sprintf(buf, "%s: %s\r\n", longmsg, cause);
    rio_writen(connected_fd, buf, strlen (buf));
    return;
}


void read_request_headers(rio_t *rp, dict_t *headers) {
	char buf[MAXLINE];
	int len = rio_readlineb (rp, buf, MAXLINE);
	while (strcmp (buf, "\r\n")) {
		char *colon = strchr (buf, ':');
		if (len > 2 && colon != NULL) {
			colon[0] = 0;
			buf[len - 2] = 0;
			colon++;

			while (colon[0] == ' ') {
				colon++;
			}

			if (dict_get(headers, buf)) {
				clienterror(rp->rio_fd, buf, "500", "Internal Server Error",
					       	"Repeated header.");
			}

			dict_put(headers, buf, colon);
			len = rio_readlineb(rp, buf, MAXLINE);
			
		}
	}
	return;
}

int parse_url(char *url, char *host, char *port, char *path) {
	return 0;
}


/* Thread routine which assigns a new thread to handle connection from client(s) */
void *thread(void *vargp) {
	Pthread_detach(pthread_self());
	while (1) {
		int connected_fd = sbuf_remove(&sbuf);
		serve_client(connected_fd);
		close(connected_fd);
	}
	return NULL;

}


/* Requests of the form:
GET http://URL HTTP/1.x
*/

/* Used when serving client, read request from connected client, then call a function to forward request onto server */
void serve_client(int connected_fd) {
    char buf[MAXLINE], method[MAXLINE], url[MAXLINE], version[MAXLINE];
	char host[MAXLINE], path[MAXLINE], port_num[MAXLINE];

	// Will store headers received from GET request and forward to client/server
	dict_t *headers = dict_create();
	dict_t *request = dict_create();
	// int host_fd;
	rio_t rio; 
	// riot_t host_rio;
	// size_t n;

    rio_readinitb(&rio, connected_fd);

    if (!rio_readlineb(&rio, buf, MAXLINE)) {
        return;
    }

	// Makes sure the method supports the correct amount of args:
	// GET(POST) url HTTP/1.0
    if (sscanf(buf, "%s %s %s", method, url, version) != 3) {
		if ((method[0] == '\0')) {
			clienterror(connected_fd, "Method", "400", "Bad Request",
				"Missing arg");
		}

		// Check to see if the method is either GET or POST
		else if ((strcasecmp(method, "GET") != 0) && (strcasecmp (method, "POST") != 0)) {
			clienterror(connected_fd, method, "501", "Not Implemented", "Method used is not valid");
			return;
        }


		if ((url[0] == '\0')) {
			clienterror(connected_fd, "URL", "400", "Bad Request",
				"Missing arg");
		}

		
		if ((version[0] == '\0')) {
			clienterror(connected_fd, "Protocol Version", "400", "Bad Request",
				"Missing arg");
		}	

		// Makes sure the protocol version is of the form HTTP/1.0
		else if (strcasecmp(version, "HTTP/1.0") != 0) {
            clienterror(connected_fd, version, "400", "Bad Request",
				"Protcol must be HTTP and be version 1.0");
			return;
        }

		return;
	}

	// Parses URL, checks whether it begins with http://, includes a host, the port_num, and finally the path
    // EX: http://mc.cdm.depaul.edu:8080/cgi-bin/echo.cgi?x&y
    if (!parse_url(url, host, port_num, path)) {
		clienterror(connected_fd, url, "400", "Bad Request",
			"URL must start with http://, contain a host, and a path to resources");
        return;
    }

	printf("Request received: %s\n", buf);

    // Method to read the headers and store in dict_t
    read_request_headers(&rio, headers);

	//host_fd = open_clientfd(host, port_num);
	
    return;
}


int main(int argc, char **argv) {
	if (argc != 2)
		usage (argv[0]);

  	// Block SIGPIPE and treat it as an error return value of read/write rather
  	// than a signal.  This will prevent crashes when client or server
  	// unexpectedly disconnects.
  	sigset_t mask;
  	sigemptyset (&mask);
  	sigaddset (&mask, SIGPIPE);
  	sigprocmask (SIG_BLOCK, &mask, NULL);

  	// Taken from slide 100 from lecture: echo client/server
  	int listenfd, connfd;
  	socklen_t client_len;
  	struct sockaddr_storage client_addr;
  	char client_hostname[MAXLINE], client_port[MAXLINE];
  	char *port_num;
	pthread_t tid;

	// Initializes buffer used by threads
	sbuf_init(&sbuf, SBUFSIZE);
	for (int i = 0; i < NTHREADS; i++) {
		Pthread_create(&tid, NULL, thread, NULL);
	}

  	// Get the port number which should be as a command line argument
  	port_num = argv[1];

  	// Listen for connection on port_num
  	listenfd = Open_listenfd (port_num);
  	printf("Proxy started on port %s, waiting for connections.\n", port_num);

  	// Continue indefinitely until we receive a valid connection from a client
  	while (1) {
		client_len = sizeof(struct sockaddr_storage);
		connfd = accept(listenfd, (SA *) &client_addr, &client_len);
		getnameinfo((SA *) &client_addr, client_len, client_hostname,
		       		MAXLINE, client_port, MAXLINE, 0);
		printf("Accepted connection from: (%s, %s)\n", client_hostname, client_port);	
		sbuf_insert(&sbuf, connfd);
  	}
  	return 0;
}
