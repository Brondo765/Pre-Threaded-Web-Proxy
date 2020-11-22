#include <stdio.h>
#include <csapp.h>
#include <string.h>
#include <sbuf.h>
#include <dict.h>
#include "proxy.h"
#include "cache.h"

#define DEFAULT_PORT 8080
#define NTHREADS 64
#define SBUFSIZE 1024

sbuf_t sbuf;

/* Prototype functions */
static void usage(const char*);
static void *thread(void*);
static void serve_client(int);
static void clienterror(int, char*, char*, char*, char*);
static int parse_request_headers(rio_t*, dict_t*, char*);
static int parse_request(int, char*, char*, char*, char*);

/* Usage function to assist in format on command line */
static void usage (const char *progname) {
	fprintf (stderr, "usage: %s PORT\n", progname);
	exit (1);
}

/* Prints diagnostic information to client on error */
static void clienterror(int connected_fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
    char buf[MAXLINE];
	sprintf(buf, "\r\n");
    rio_writen(connected_fd, buf, strlen (buf));
    sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
    rio_writen (connected_fd, buf, strlen (buf));
    sprintf(buf, "%s: %s\r\n", longmsg, cause);
    rio_writen(connected_fd, buf, strlen (buf));
    return;
}

/* Parses headers from request, places them in a dictionary for reference later */
static int parse_request_headers(rio_t *rp, dict_t *headers, char *host) {
	char buf[MAXLINE];
	char header_key[MAXLINE];
	char header_val[MAXLINE];
	char *temp_pos, *temp_pos2;
	int key_len, val_len;

	dict_put(headers, "Connection", "close\r\n");
    dict_put(headers, "Proxy-Connection", "close\r\n");
    dict_put(headers, "Host", host);
    dict_put(headers, "User-Agent", USER_AGENT);

	dict_foreach(headers, 
		{
			strcat(buf, key);
			strcat(buf, ":");
			strcat(buf, " ");
			strcat(buf, val);
		});


	int len = strlen(buf);
	len += rio_readlineb(rp, buf, MAXLINE);
	// Loop until end of headers
	while ((strcmp(buf, "\r\n") != 0) && (len < MAXLINE)) {
		char *colon = strchr(buf, ':');
		// Only add to dict when headers are in complete form of (header_key: header_val) and input < MAXLINE
		if (colon != NULL) {
			temp_pos = strpbrk(buf, ":");
			temp_pos2 = temp_pos + 2;
			key_len = temp_pos - buf;
			val_len = strlen(temp_pos2);
			strncpy(header_key, buf, key_len);
			strncpy(header_val, temp_pos2, val_len);
			header_key[key_len] = '\0';
			dict_put(headers, header_key, header_val);
			bzero(header_key, MAXLINE);
			bzero(header_val, MAXLINE);
			len += rio_readlineb(rp, buf, MAXLINE);
		} 
		
		else {
			break;
		}
	}

	// Updated headers with correct vals when returning back to function
	dict_put(headers, "Connection", "close\r\n");
    dict_put(headers, "Proxy-Connection", "close\r\n");
    dict_put(headers, "Host", host);
    dict_put(headers, "User-Agent", USER_AGENT);
	return 0;
}
	
/* Parses uri from request, makes sure it contains the necessary beginning and stores each component in its own array */
static int parse_request(int connected_fd, char *uri, char *host, char *port, char *path) {
	int host_length, port_length;
	char http_front[7];
    strncpy(http_front, uri, 7);
	http_front[7] = '\0';

	// If the URL doesn't start with http:// then error 
	if (strcmp(http_front, "http://") != 0) {
		return -1;
	}

	char *temp_pos, *temp_pos2, *temp_pos3, *temp_pos4, *temp_port;

	// Get us to // in the URL
	temp_pos = strstr(uri, "//");

	// We move two spots ahead now we just have the host (maybe port) and path leftover
	temp_pos += 2;

	// Finds the end of host when it finds ':', '/' '/0' or CLRF
	temp_pos2 = strpbrk(temp_pos, " :/\r\n\0");
	host_length = temp_pos2 - temp_pos;
	strncpy(host, temp_pos, host_length);
	host[host_length] = '\0';

	// Check to see if there is a port num specified
	if (*temp_pos2 == ':') {
		temp_pos3 = temp_pos2 + 1;
		temp_port = temp_pos3;
		temp_pos4 = strchr(temp_pos, '/');
		strcpy(path, temp_pos4);
		port_length = temp_pos4 - temp_pos3;
		temp_port[port_length] = '\0';
		strncpy(port, temp_pos3, port_length);
		if (strcasecmp(port, "TINYPORT") == 0) {
			sprintf(port, "%d", DEFAULT_PORT);
		}
	}
       	// Otherwise we just have the path and can tack on the default port of 80
	else if(*temp_pos2 == '/') {
		strcpy(path, temp_pos2);
		sprintf(port, "%d", DEFAULT_PORT);
	}
	return 0;
}


/* Thread routine which assigns a new thread to handle connection from client */
static void *thread(void *vargp) {
	Pthread_detach(pthread_self());
	free(vargp);
	int connected_fd = sbuf_remove(&sbuf);
	serve_client(connected_fd);
	close(connected_fd);
	return NULL;
}

/* Used when serving client, will read in input and parse method, uri, and protocol version, calls functions to check
 * if uri provided is valid then finally checks if headers, if any, are valid. Finally sends a request to the server
 * with the client request and returns back to the client with the response.
 */
static void serve_client(int connected_fd) {
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
	char host[MAXLINE], path[MAXLINE], port_num[MAXLINE];

	// Will store headers received from request
	dict_t *headers = dict_create();
    //int host_fd;
	rio_t rio; 
	//riot_t host_rio;
	//size_t n;

        rio_readinitb(&rio, connected_fd);

	// If no request input return to listen state
    if (!rio_readlineb(&rio, buf, MAXLINE)) {
        return;
    }

	// Makes sure the first line in request supports the correct amount of args:
	// GET(POST) uri HTTP/1.0
    sscanf(buf, "%s %s %s", method, uri, version);

	if ((method[0] == '\0') || method[0] == ' ') {
		clienterror(connected_fd, "Method", "400", "Bad Request",
				"Missing arg");
		return;
	}

	// Check to see if method is only GET/POST
	if ((strcasecmp(method, "GET") != 0) && (strcasecmp (method, "POST") != 0)) {
		clienterror(connected_fd, method, "501", "Not Implemented", "Method used is not valid");
		return;
    }


	if ((uri[0] == '\0') || version[0] == ' ') {
		clienterror(connected_fd, "uri", "400", "Bad Request",
				"Missing arg");
		return;
	}

		
	if ((version[0] == '\0') || version[0] == ' ') {
		clienterror(connected_fd, "Protocol Version", "400", "Bad Request",
				"Missing arg");
		return;
	}

	if (strcmp(version, "HTTP/1.1") != 0) {
		return;
	}

	// Copy contents of uri to a saved local variable as parse_request alters the uri contents
    char uri_unaltered[MAXLINE];
	// Most uri's don't go above 2000 characters
    for (int i = 0; i < 2000; i++) {
        uri_unaltered[i] = uri[i];
    }

	// If the protocol version isn't 1.0 change it to it now
	if (strcasecmp(version, "HTTP/1.0") != 0) {
		version[7] = '0';
	}

	int valid = 0;
	/* checks whether uri is valid by seeing if it begins with http://, includes a host, the port_num,
     * and a path to resources.
     * EX: http://mc.cdm.depaul.edu:8080/cgi-bin/echo.cgi?x&y
	 */
	valid = parse_request(connected_fd, uri, host, port_num, path);

	// If we get an error report to client and go back to listening state
	if (valid == -1) {
		clienterror(connected_fd, uri, "400", "Bad Request", "Received bad request");
		return;
	}

	// Add CRLF to end of host for header input
	strcat(host, "\r\n");

	// Remake buf with correct HTTP/1.x version
	char temp_buf[MAXLINE];
	bzero(buf, MAXLINE);
	strcat(temp_buf, method);
	strcat(temp_buf, " ");
	strcat(temp_buf, uri_unaltered);
	strcat(temp_buf, " ");
	strcat(temp_buf, version);
	strcat(temp_buf, "\r\n");
	strcpy(buf, temp_buf);
	bzero(temp_buf, MAXLINE);

	// Reset valid flag and then parse headers making sure they are valid
	valid = 0;
	valid = parse_request_headers(&rio, headers, host);
	if (valid == -1) {
		clienterror(connected_fd, "Bad headers", "400", "Bad Request", "Denied due to");
		return;
	}

	// Add all the headers in correct format to the buffer before sending to server
	dict_foreach(headers,
		{
			if (strlen(buf) < MAXLINE || strlen(key) > MAXLINE) {
				strcat(buf, key);
				strcat(buf, ":");
				strcat(buf, " ");
				strcat(buf, val);
			}
		});

	// Sanity check in proxy window
	printf("%s\n", buf);

	// Now we request a connection to the server 
	// host_fd = open_clientfd(host, port_num);	

	// Destroy headers as they this counts as allocation in the heap
	dict_destroy(headers);
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
  	struct sockaddr_in client_addr;
  	char client_hostname[MAXLINE], client_port[MAXLINE];
  	char *port_num;
	pthread_t tid;

	// Initializes worker threads and sends to thread routine
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

	sbuf_deinit(&sbuf);
  	return 0;
}
