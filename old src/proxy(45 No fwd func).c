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
//static void *thread(void*);
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

	// Put headers we don't want to change now into dict then into buf
	dict_put(headers, "Connection", "close\r\n");
    dict_put(headers, "Proxy-Connection", "close\r\n");
    dict_put(headers, "User-Agent", USER_AGENT);

	dict_foreach(headers, 
			{
			strcat(buf, key);
			strcat(buf, ":");
			strcat(buf, " ");
			strcat(buf, val);
			strcat(buf, "\r\n");
			});

	// Calc current buf size so we cannot exceed n bytes for header storage
	int len = strlen(buf);
	len += rio_readlineb(rp, buf, MAXLINE);
	// Loop until we find CLRF 
	while ((strcmp(buf, "\r\n") != 0) && (len < MAXLINE)) {
		char *colon = strchr(buf, ':'); 
		// Only add to dict when headers are in complete form of (header_key: header_val)
		if (colon != NULL) {
			temp_pos = strpbrk(buf, ":"); // Find when we get to colon
			temp_pos2 = temp_pos + 2; // Move forward two to get rid of colon and space before val
			key_len = temp_pos - buf;
			val_len = strlen(temp_pos2);
			strncpy(header_key, buf, key_len);
			strncpy(header_val, temp_pos2, val_len);
			header_key[key_len] = '\0';
			// Append CLRF to header_val
			header_val[val_len] = '\n';
			header_val[val_len - 1] = '\r';
			// If the key already exists in the dict don't add
			if (dict_get(headers, header_key)) {

			} 
			// Otherwise add to the dict
			else {
				dict_put(headers, header_key, header_val);
			}
			bzero(header_key, MAXLINE);
			bzero(header_val, MAXLINE);
			len += rio_readlineb(rp, buf, MAXLINE);
		} 
		// If no colon in header, then break looping as we have an error in request input
		else {
			break;
		}
	}

	return 0;
}
	
/* Parses uri from request, makes sure it contains the necessary beginning and stores each component in its own string */
static int parse_request(int connected_fd, char *uri, char *host, char *port, char *path) {
	if (strcmp(uri, "/") == 0) {
		strcpy(path, "/");
		return 0;
	}

	// Garbage, but gets the job done
	int host_length, port_length;
	char http_front[7];
       	strncpy(http_front, uri, 7);
	http_front[7] = '\0';

	// If the URL doesn't start with http:// then error 
	if (strcmp(http_front, "http://") != 0) {
		return -1;
	}

	char *temp_pos, *temp_pos2, *temp_pos3, *temp_pos4, *temp_port;
	char temp_host[MAXLINE];
    memset(temp_host, 0, MAXLINE);	

	// Get us to // in the URL
	temp_pos = strstr(uri, "//");

	// We move two spots ahead now we just have the host (maybe port) and path leftover to read
	temp_pos += 2;

	// Finds the end of host when it finds ':', '/' '/0' or CLRF char
	temp_pos2 = strpbrk(temp_pos, " :/\r\n\0");
	host_length = temp_pos2 - temp_pos;
	strncpy(temp_host, temp_pos, host_length);
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
			return -1;
		}
	}
       	// Otherwise we just have the path and can tack on the default port of 80
	else if(*temp_pos2 == '/') {
		strcpy(path, temp_pos2);
		sprintf(port, "%d", DEFAULT_PORT);
	}

	strncpy(host, temp_host, MAXLINE);
	return 0;
}


/* Thread routine which assigns a new thread to handle connection from client */
/*
static void *thread(void *vargp) {
	Pthread_detach(pthread_self());
	free(vargp);
	int connected_fd = sbuf_remove(&sbuf);
	serve_client(connected_fd);
	close(connected_fd);
	return NULL;
}
*/

/* Used when serving client, will read in input and parse method, uri, and protocol version, calls functions to check
 * if uri provided is valid then finally checks if headers, if any, are valid. Finally sends a request to the server
 * with the client request and returns back to the client with the response.
 */
static void serve_client(int connected_fd) {
	// Current buffer read request and then a temp buffer used to rewrite request once we need to send to server
	char buf[MAXLINE], temp_buf[MAXLINE]; 
	// Method holds GET/POST, uri is (http://host:port/path), version holds HTTP/x.x 
    char method[MAXLINE], uri[MAXLINE], version[MAXLINE];
	// Host holds (mc.cdm.depaul.edu) (localhost), path holds (/) (/cgi-bin) (/home.html),
	// port_num holds (8080) (3275)
	char host[MAXLINE], path[MAXLINE], port_num[MAXLINE];
	char temp_host[MAXLINE]; // Used when putting host into dict 

	// Will store headers received from request
	dict_t *headers = dict_create();

    int host_fd;	// Host fd when we connected eventaully
	rio_t rio; 	// Client rio
	rio_t host_rio; // Host rio
	size_t n;	// Size bytes to write

    rio_readinitb(&rio, connected_fd);

	// If no request input return to listen state
    if (!rio_readlineb(&rio, buf, MAXLINE)) {
            return;
    }

	// Makes sure the first line in request supports the correct amount of args:
	// GET uri HTTP/x.x
    sscanf(buf, "%s %s %s", method, uri, version);

	if (method[0] == '\0') {
		clienterror(connected_fd, "Method", "400", "Bad Request",
				"Missing arg");
		return;
	}

	// Check to see if method is only GET/POST
	if ((strcasecmp(method, "GET") != 0) && (strcasecmp (method, "POST") != 0)) {
		clienterror(connected_fd, method, "501", "Not Implemented", "Method used is not valid");
		return;
    }


	if (uri[0] == '\0') {
		clienterror(connected_fd, "uri", "400", "Bad Request",
				"Missing arg");
		return;
	}

		
	if (version[0] == '\0') {
		clienterror(connected_fd, "Protocol Version", "400", "Bad Request",
				"Missing arg");
		return;
	}

	// Version from client should always be HTTP/1.1 or HTTP/0.9 (We translate to HTTP/1.0)
	if ((strcasecmp(version, "HTTP/1.1") == 0) ||
	    (strcasecmp(version, "HTTP/0.9") == 0)) {

		// If the protocol version isn't 1.0 change it
		if (strcasecmp(version, "HTTP/1.1") == 0) {
			version[7] = '0';
			version[8] = '\0';
		}
		else if(strcasecmp(version, "HTTP/0.9") == 0) {
			version[5] = '1';
			version[7] = '0';
			version[8] = '\0';
		}	
	}
	else {
		return;
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

	strncpy(temp_host, host, MAXLINE);
	strcat(temp_host, "\r\n");

	// Remake buf with correct format to send to server GET /path HTTP/1.0
	bzero(buf, MAXLINE);
	strcat(temp_buf, method);
	strcat(temp_buf, " ");
	strcat(temp_buf, path);
	strcat(temp_buf, " ");
	strcat(temp_buf, version);
	strcat(temp_buf, "\r\n");
	strcpy(buf, temp_buf);
	bzero(temp_buf, MAXLINE);

	// Reset valid flag and then parse headers making sure they are valid
	valid = 0;
	valid = parse_request_headers(&rio, headers, temp_host);
	if (valid == -1) {
		clienterror(connected_fd, "Bad headers", "400", "Bad Request", "Denied due to");
		return;
	}

	// Add all the headers in correct format to buf before sending to server
	dict_foreach(headers,
			{
				if (strlen(buf) < MAXLINE) {
					strcat(buf, key);
					strcat(buf, ":");
					strcat(buf, " ");
					strcat(buf, val);
				}
			});


	// Cat on a CLRF as the end of a request ends with this
	strcat(buf, "\r\n");

	printf("sending this to server: %s\n", buf);

	// Open a connection to the host on port_num
	host_fd = open_clientfd(host, port_num);
	
	if (host_fd <= 0) {
		clienterror(host_fd, host, "503", "Server Unreachable", "Cannot find host");
		return;
	}

	// Open rio reader to read response from host
	rio_readinitb(&host_rio, host_fd);
	// Write request to host (has first line GET /path HTTP/1.0
	// then the headers that follow
	rio_writen(host_fd, buf, MAXLINE);

	// Read response from server and write to temp_buf, then write temp_buf to output of client
	while ((n = rio_readlineb(&host_rio, temp_buf, MAXLINE)) != 0) {
		if (n <= 0) {
			close(host_fd);
			break;
		}

		rio_writen(connected_fd, temp_buf, n);
	}

	close(host_fd);

	// In case connection closes prematurely with client, zero out all char arrays
	bzero(temp_buf, MAXLINE);
	bzero(buf, MAXLINE);
	bzero(method, MAXLINE);
	bzero(uri, MAXLINE);
	bzero(temp_host, MAXLINE);
	bzero(host, MAXLINE);
	bzero(version, MAXLINE);
	bzero(path, MAXLINE);
	bzero(port_num, MAXLINE);

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
	// pthread_t tid;

	// Initializes worker threads and sends to thread routine
	// sbuf_init(&sbuf, SBUFSIZE);
	// for (int i = 0; i < NTHREADS; i++) {
		// Pthread_create(&tid, NULL, thread, NULL);
	//}

  	// Get the port number which should be as a command line argument
  	port_num = argv[1];

  	// Listen for connection on port_num
  	listenfd = open_listenfd (port_num);
  	printf("Proxy started on port %s, waiting for connections.\n", port_num);

  	// Continue indefinitely until we receive a valid connection from a client
  	while (1) {
		client_len = sizeof(struct sockaddr_storage);
		connfd = accept(listenfd, (SA *) &client_addr, &client_len);
		getnameinfo((SA *) &client_addr, client_len, client_hostname,
		       		MAXLINE, client_port, MAXLINE, 0);
		printf("Accepted connection from: (%s, %s)\n", client_hostname, client_port);
		serve_client(connfd);	
		close(connfd);
		// sbuf_insert(&sbuf, connfd);
  	}

	// sbuf_deinit(&sbuf);
  	return 0;
}
