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
sbuf_t sbuf; // Global thread connection buffer

/* Prototype functions */
static void usage(const char*);
static void *thread(void*);
static void add_to_buf(dict_t*, char*);
static void parse_header_and_val(dict_t*, char*);
static void clienterror(int, char*, char*, char*, char*);
static int serve_client(int);
static int parse_request_headers(rio_t*, dict_t*, char*, size_t);
static int parse_request(int, char*, char*, char*, char*);
static int forward_to_server(int, rio_t*, char*, char*, char*, char*, char*);

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

/* Simple function which formats headers in the buf before request is sent to the server */
static void add_to_buf(dict_t *dictionary, char *buf) {
	dict_foreach(dictionary,
	{
		strcat(buf, key);
		strcat(buf, ":");
		strcat(buf, " ");
		strcat(buf, val);
	)};
	return;
}

/* Parses a buffer line read from the client and adds the header and val to a dictionary */
static void parse_header_and_val(dict_t *headers, char* buf) {
	char header_key[MAXLINE];
	char header_val[MAXLINE];
	char *temp_pos, *temp_pos2;
	int key_len, val_len;

	temp_pos = strpbrk(buf, ":"); 	// Find when we get to colon
	temp_pos2 = temp_pos + 2; 		// Move forward two to get rid of colon and space before val
	
	key_len = temp_pos - buf;
	val_len = strlen(temp_pos2);
	strncpy(header_key, buf, key_len);
	header_key[key_len] = '\0';
	strncpy(header_val, temp_pos2, val_len);

	// Append CLRF to header_val
	header_val[val_len] = '\n';
	header_val[val_len - 1] = '\r';

	// Put header_key and header_val in dict
	if (dict_get(headers, header_key)) {
	} else {
		dict_put(headers, header_key, header_val);
	}
	return;
}

/* Parses headers from request, places them in a dictionary for reference later */
static int parse_request_headers(rio_t *rp, dict_t *headers, char *host, size_t curr_buf_size) {
	char buf[MAXLINE];
	size_t len;
	int n;
	// Put headers we don't want to change now into dict then into buf
	dict_put(headers, "Connection", "close\r\n");
    dict_put(headers, "Proxy-Connection", "close\r\n");
    dict_put(headers, "User-Agent", USER_AGENT);
	// Headers should exist, mostly a robustness test
	if (dict_get(headers, "Connection") ||
		dict_get(headers, "Proxy-Connection") ||
		dict_get(headers, "User-Agent")) {
	} else {
		add_to_buf(headers, buf);
	}

	// Calc current buf size so we cannot exceed n bytes for header storage
	len = strlen(buf);
	len += curr_buf_size;
	n = strlen(buf) + curr_buf_size;
	len += rio_readlineb(rp, buf, MAXLINE);

	// Loop until we find CLRF and or when the line read exceeds MAXLINE bytes
	while ((strcmp(buf, "\r\n") != 0) && (len < MAXLINE)) {
		colon = strchr(buf, ':'); 
		// Only add to dict when headers are in complete form of (header_key: header_val)
		if (len < MAXLINE && colon != NULL) {
			parse_header_and_val(headers, buf);
			n += strlen(buf);
			len += rio_readlineb(rp, buf, MAXLINE);
		} else {
			return -1;
		}
	}
	// Get last line we read and put header in dict if read is close to MAXLINE bytes
	if (MAXLINE - n < 120 && strcmp(buf, "\r\n") != 0) {
		parse_header_and_val(headers, buf);
		return -2;
	} else {
		return 0;
	}
}

/* Parses uri from request, makes sure it contains the necessary beginning and stores each component in its own string */
static int parse_request(int connected_fd, char *uri, char *host, char *port, char *path) {
	// Simple request in curl
	if (strcmp(uri, "/") == 0) {
		strcpy(path, "/");
		return 0;
	}
	char *temp_pos, *temp_pos2, *temp_pos3, *temp_pos4, *temp_port;
	int host_length, port_length;
	char http_front[7];

    strncpy(http_front, uri, 7);
	http_front[7] = '\0';

	// If the URL doesn't start with http:// then error 
	if (strcmp(http_front, "http://") != 0) {
		return -1;
	}

	// Need temp_host storage as headers end with CLRF 
	char temp_host[MAXLINE];
    memset(temp_host, 0, MAXLINE);	
	temp_pos = strstr(uri, "//"); 	// Get us to // in the uri
	temp_pos += 2; 					// We move two spots ahead now we just have the host, port, and path leftover to read

	// Finds the end of host when ':', '/' '/0' or CLRF char found
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

		// Simple error check on port number to see if its not a string or negative
		if (atoi(port) <= 0) {
			return -1;
		}
	}
    // Otherwise we just have the path and can tack on the default port of 8080
	else if(*temp_pos2 == '/') {
		strcpy(path, temp_pos2);
		sprintf(port, "%d", DEFAULT_PORT);
	}
	strncpy(host, temp_host, MAXLINE); // Before leaving function copy temp_host into real host reference
	return 0;
}

/* Forwards request from client to server then writes server reply to client buffer */ 
static int forward_to_server(int client_fd, rio_t *client_rio, char *host, char *port, char *buf, char *method, char *c_len) {
	char temp_buf[MAXLINE];
	int host_fd;
	rio_t host_rio;
	size_t n;
    host_fd = open_clientfd(host, port); // Open a connection to the host on port_num

    // If file descriptor for host is an error close connection
    if (host_fd <= 0) {
		clienterror(host_fd, host, "503", "Server Unreachable", "Cannot find host");
        return -1;
    }

	// Write request and headers to server (both GET and POST need this to be done)
	if ((rio_writen(host_fd, buf, strlen(buf))) < 0) {
		return -1;
	}

	// If method is POST we need the payload_size to read and write to the server
	if (strcmp(method, "POST") == 0) {
		void *data[MAXLINE];
        size_t payload_size = atoi(c_len); // Determine payload size
			
		// Read in payload, store in data of payload_size
        if ((n = rio_readnb(client_rio, data, payload_size)) < 0) {
            return -1;
        }
		// Write payload to server 
        if ((rio_writen(host_fd, data, n)) < 0) {
            return -1;
        }
	}

	rio_readinitb(&host_rio, host_fd); // Robust reader initialize with host file descriptor

	// Read response from server and write back to client in MAXLINE bytes read per line
    while ((n = rio_readlineb(&host_rio, temp_buf, MAXLINE)) != 0) {
		rio_writen(client_fd, temp_buf, n);
    }	
	close(host_fd);
	return 0;
}

/* Thread routine which assigns a new thread to handle connection from client */
static void *thread(void *vargp) {
	Pthread_detach(pthread_self());
	while (1) {
		int connected_fd = sbuf_remove(&sbuf);
		serve_client(connected_fd);
		close(connected_fd);
	}
	return NULL;
}

/* Main proxy routine, will read in input and parse the method, uri, and protocol version, in first line of request.
 * Then calls several functions: Check if uri provided is valid. Check if headers, if any, are valid. And then, 
 * send a request to the server and read the response back to the client.
 */
static int serve_client(int connected_fd) {
	char buf[MAXLINE]; 					// Current buffer read request from client
	char temp_buf[MAXLINE];   			// temp buffer used to rewrite request in correct format to server
    char method[MAXLINE];  				// Method holds GET/POST
	char uri[MAXLINE]; 					// uri is (http://host:port/path)
	char version[MAXLINE]; 				// version holds HTTP/x.x 
	char host[MAXLINE]; 				// Host holds (mc.cdm.depaul.edu) (localhost)
	char path[MAXLINE]; 				// path holds (/) (/cgi-bin) (/home.html)
	char port_num[MAXLINE]; 			// port_num holds (8080) (3275)
	char temp_host[MAXLINE]; 			// Used when putting host into dict 
	char *c_len = NULL 					// Default Content-Length header val in case GET request instead of POST
	int valid; 							// Used for error checking in functions
	dict_t *headers = dict_create(); 	// Store headers received from request
	rio_t rio; 							// Client rio
    rio_readinitb(&rio, connected_fd); 	// Robust reader initialize with client file descriptor

	// If no request input return to listen state
    if (!rio_readlineb(&rio, buf, MAXLINE)) {
        return -1;
    }

	// Makes sure the first line in request supports the correct amount of args: GET uri HTTP/x.x
    sscanf(buf, "%s %s %s", method, uri, version); 
	if (method[0] == '\0') {
		clienterror(connected_fd, "Method", "400", "Bad Request", "Missing arg");
		return -1;
	}

	// Check to see if method is only GET/POST
	if ((strcasecmp(method, "GET") != 0) && (strcasecmp (method, "POST") != 0)) {
		clienterror(connected_fd, method, "501", "Not Implemented", "Method used is not valid");
		return -1;
    }

	// uri isn't saved as an arg return error
	if (uri[0] == '\0') {
		clienterror(connected_fd, "uri", "400", "Bad Request", "Missing arg");
		return -1;
	}

	// version isn't saved as an arg set it to default HTTP/1.0
	if (version[0] == '\0') {
		strcpy(version, "HTTP/1.0");
	}

	// Version from client should always be HTTP/1.1 or HTTP/1.0 (We translate to HTTP/1.0 if not in this form)
	if ((strcasecmp(version, "HTTP/1.1") == 0)) {
		// If the protocol version isn't 1.0 change it
		version[7] = '0'; 
	} else {
		return -1;
	}

	/* checks whether uri is valid by seeing if it begins with http://, includes a host, the port_num,
	* and a path to resources.
    * EX: http://mc.cdm.depaul.edu:8080/cgi-bin/echo.cgi
	*/
	valid = parse_request(connected_fd, uri, host, port_num, path);
	// If we get an error report to client and go back to listening state
	if (valid == -1) {
		clienterror(connected_fd, uri, "400", "Bad Request", "Received bad request");
		return -1;
	}

	// Copy host into temp_host for input into dict later
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
	valid = parse_request_headers(&rio, headers, temp_host, strlen(buf));
	if (valid == -1) {
		clienterror(connected_fd, "Bad headers", "400", "Bad Request", "Denied due to");
		return -1;
	}

	// We used all the buffer room and need to create a hold for multiple request size headers
	if (valid == -2) {
		char temp_hold[MAXLINE * 4]; 	// Large buf to hold multiple request size
		strcpy(temp_hold, buf); 		// Append first line of request to temp_hold now
        add_to_buf(headers, temp_hold); // Append to buf first set of headers 

		// Loop through all the headers and add to mass_store dict
		dict_foreach(headers,
		{
			dict_put(mass_store, key, val);
		});

		// While we have multiple headers not read up to MAXLINE in our rio struct
		while (valid == -2) {
			valid = parse_request_headers(&rio, headers, temp_host, 0); // Parse headers again

			// Check if the headers we added to the dictionary exist in mass_store, if they don't add to mass_store now
			dict_foreach(headers,
			{	
				if (dict_get(mass_store, key)) {
				} else {
					dict_put(mass_store, key, val);
				}
			});

       	 	add_to_buf(headers, temp_hold); // Append to buf with first line in req and set of headers 

			// When we have a 0 return we break looping
			if (valid == 0) {
				break;
			}
		}

		strcat(temp_hold, "\r\n"); // Add CLRF to end of buf

		// Now we send the request to the server
    	valid = forward_to_server(connected_fd, &rio, host, port_num, temp_hold, method, c_len);
    	if (valid == -1) {
        	clienterror(connected_fd, host, "500", "Internal Server Error", "Did not send to");
        	return -1;
    	}	

		bzero(temp_hold, MAXLINE * 4);	// Zero out the temp_hold buffer in case of disconnect

	} else { // Otherwise we don't need to send size > MAXLINE requests
        add_to_buf(headers, buf); 	// Add all the headers in correct format to buf before sending to server
        strcat(buf, "\r\n"); 		// Cat on a CLRF as the end of a request		

		// For POST requests need to get Content-Length size
		if (strcmp(method, "POST") == 0) {
			c_len = dict_get(headers, "Content-Length"); // Get header val of content-length

			// See if content-length is 0 (Not an int or size 0) or negative
        	if (atoi(c_len) <= 0) {
            	return -1;
			}
		}

		// Now we send the request to the server same as before
    	valid = forward_to_server(connected_fd, &rio, host, port_num, buf, method, c_len);
    	if (valid == -1) {
        	clienterror(connected_fd, host, "500", "Internal Server Error", "Did not send to");
        	return -1;
    	}
	}

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

	// Destroy headers
	dict_destroy(headers);
	dict_destroy(mass_store);
    return 0;
}

int main(int argc, char **argv) {
	int listenfd, connfd; 			// Listenfd listens for incoming connections to the proxy. connfd is the client that connects to the proxy
  	socklen_t client_len; 			// since connfd is a socket connection, need the length in accept() function
  	struct sockaddr_in client_addr; // client address used in accept() function
	pthread_t tid; 					// Thread id used when creating pre-threaded environment

	// Not enough args provided print usage function
	if (argc != 2) {
		usage (argv[0]);
	}

	/* Block SIGPIPE and treat it as an error return value of read/write rather
  	* than a signal. This will prevent crashes when client or server
  	* unexpectedly disconnects.
	*/ 
  	sigset_t mask;
  	sigemptyset (&mask);
  	sigaddset (&mask, SIGPIPE);
  	sigprocmask (SIG_BLOCK, &mask, NULL);

	sbuf_init(&sbuf, SBUFSIZE); 		// Initializes worker threads and sends to thread routine
	listenfd = Open_listenfd(argv[1]); 	// Listen for connection on port num

	// Create worker threads
	for (int i = 0; i < NTHREADS; i++) {
		Pthread_create(&tid, NULL, thread, NULL);
	}

  	// Accept connection, add to sbuf and then serve in thread routine
  	while (1) {
		client_len = sizeof(struct sockaddr_storage);
		connfd = accept(listenfd, (SA *) &client_addr, &client_len);
		sbuf_insert(&sbuf, connfd);
  	}
	sbuf_deinit(&sbuf); // Deinitialize global sbuf
  	exit(0);
}