/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 * Updated 10/2020 cadilhac
 *   - Fix some warnings
 *   - Pass the headers to the CGI scripts
 *   - Implement a crude version of POST for CGI scripts
 *   - Implement a crude version of multithreading (code straight from slides)
 */
#include <signal.h>
#include "csapp_longform.h"
#include <dict.h>
#include <sbuf.h>

#define SBUFSIZE 1024
#define NTHREADS 128

sbuf_t sbuf;


void doit (int fd);
void read_requesthdrs (rio_t *rp, dict_t *hdrs);
int parse_uri (char *uri, char *filename, char *cgiargs);
void serve_static (int fd, char *filename, int filesize);
void get_filetype (char *filename, char *filetype);
void serve_dynamic (rio_t *rio, char *filename, dict_t *hdrs, char *cgiargs, int is_post);
void clienterror (int fd, char *cause, char *errnum,
                  char *shortmsg, char *longmsg);

static void *thread(void *vargp)
{
  Pthread_detach (pthread_self ());
  while (1) {
    int connfd = sbuf_remove (&sbuf); /* Remove connfd from buf */
    doit (connfd);
  }

  return NULL;
}

int main (int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2) {
    fprintf (stderr, "usage: %s <port>\n", argv[0]);
    exit (1);
  }

  // Block SIGPIPE, treat it as an error return value of read/write.
  sigset_t mask;
  sigemptyset (&mask);
  sigaddset (&mask, SIGPIPE);
  sigprocmask (SIG_BLOCK, &mask, NULL);

  listenfd = Open_listenfd (argv[1]);

  sbuf_init(&sbuf, SBUFSIZE);

  pthread_t tid;
  for (int i = 0; i < NTHREADS; i++)
    Pthread_create(&tid, NULL, thread, NULL);

  while (1) {
    clientlen = sizeof (clientaddr);
    connfd = Accept (listenfd, (SA *) &clientaddr, &clientlen);
    Getnameinfo ( (SA *) &clientaddr, clientlen, hostname, MAXLINE,
                  port, MAXLINE, 0);
    printf ("Accepted connection from (%s, %s)\n", hostname, port);
    sbuf_insert (&sbuf, connfd);
  }
}

/*
 * doit - handle one HTTP request/response transaction
 */
void doit (int fd) {
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;

  /* Read request line and headers */
  Rio_readinitb (&rio, fd);

  if (!Rio_readlineb (&rio, buf, MAXLINE)) {
    close (fd);
    return;
  }

  printf ("Request received: %s", buf);
  sscanf (buf, "%s %s %s", method, uri, version);

  if (strcasecmp (method, "GET") != 0 &&
      strcasecmp (method, "POST") != 0) {
    clienterror (fd, method, "501", "Not Implemented",
                 "Tiny does not implement this method");
    close (fd);
    return;
  }

  dict_t *hdrs = dict_create ();
  read_requesthdrs (&rio, hdrs);

  /* Parse URI from request */
  is_static = parse_uri (uri, filename, cgiargs);

  if (stat (filename, &sbuf) < 0) {
    clienterror (fd, filename, "404", "Not found",
                 "Tiny couldn't find this file");
    goto cleanup;
  }

  if (is_static) { /* Serve static content */
    if (! (S_ISREG (sbuf.st_mode)) || ! (S_IRUSR & sbuf.st_mode)) {
      clienterror (fd, filename, "403", "Forbidden",
                   "Tiny couldn't read the file");
      goto cleanup;
    }

    serve_static (fd, filename, sbuf.st_size);
  } else { /* Serve dynamic content */
    if (! (S_ISREG (sbuf.st_mode)) || ! (S_IXUSR & sbuf.st_mode)) {
      clienterror (fd, filename, "403", "Forbidden",
                   "Tiny couldn't run the CGI program");
      goto cleanup;
    }

    serve_dynamic (&rio, filename, hdrs, cgiargs, strcasecmp (method, "POST") == 0);
  }
  cleanup:
  dict_destroy (hdrs);
  close (fd);
}

/*
 * read_requesthdrs - read HTTP request headers
 */
void read_requesthdrs (rio_t *rp, dict_t *hdrs) {
  char buf[MAXLINE];

  int len = Rio_readlineb (rp, buf, MAXLINE);

  while (strcmp (buf, "\r\n")) {
        char *colon = strchr (buf, ':');

    // This is ugly: just for the lab.
    if (len > 2 && colon != NULL) {
      colon[0] = 0;
      buf[len - 2] = 0; // Stop at \r
      colon++;

      while (colon[0] == ' ')
        colon++;

      if (dict_get (hdrs, buf))
        clienterror (rp->rio_fd, buf, "500", "Internal Server Error",
                     "Repeated header.");
      dict_put (hdrs, buf, colon);
      len = Rio_readlineb (rp, buf, MAXLINE);
    }
  }

  return;
}

/*
 * parse_uri - parse URI into filename and CGI args
 *             return 0 if dynamic content, 1 if static
 */
int parse_uri (char *uri, char *filename, char *cgiargs) {
  char *ptr;

  if (!strstr (uri, "cgi-bin")) { /* Static content */
    strcpy (cgiargs, "");
    strcpy (filename, ".");
    strcat (filename, uri);

    if (uri[strlen (uri) - 1] == '/')
      strcat (filename, "home.html");

    return 1;
  } else { /* Dynamic content */
    ptr = index (uri, '?');

    if (ptr) {
      strcpy (cgiargs, ptr + 1);
      *ptr = '\0';
    } else
      strcpy (cgiargs, "");

    strcpy (filename, ".");
    strcat (filename, uri);
    return 0;
  }
}

/*
 * serve_static - copy a file back to the client
 */
void serve_static (int fd, char *filename, int filesize) {
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  /* Send response headers to client */
  get_filetype (filename, filetype);
  sprintf (buf, "HTTP/1.0 200 OK\r\n");
  if (rio_writen (fd, buf, strlen (buf)) < 0)
    return;
  sprintf (buf, "Server: Tiny Web Server\r\n");
  if (rio_writen (fd, buf, strlen (buf)) < 0)
    return;
  sprintf (buf, "Content-length: %d\r\n", filesize);
  if (rio_writen (fd, buf, strlen (buf)) < 0)
    return;
  sprintf (buf, "Content-type: %.100s\r\n\r\n", filetype);
  if (rio_writen (fd, buf, strlen (buf)) < 0)
    return;

  /* Send response body to client */
  srcfd = Open (filename, O_RDONLY, 0);
  srcp = Mmap (0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
  Close (srcfd);
  rio_writen (fd, srcp, filesize);
  Munmap (srcp, filesize);
}

/*
 * get_filetype - derive file type from file name
 */
void get_filetype (char *filename, char *filetype) {
  if (strstr (filename, ".html"))
    strcpy (filetype, "text/html");
  else if (strstr (filename, ".gif"))
    strcpy (filetype, "image/gif");
  else if (strstr (filename, ".png"))
    strcpy (filetype, "image/png");
  else if (strstr (filename, ".jpg"))
    strcpy (filetype, "image/jpeg");
  else
    strcpy (filetype, "text/plain");
}

/*
 * serve_dynamic - run a CGI program on behalf of the client
 */
void serve_dynamic (rio_t *rio, char *filename, dict_t *hdrs, char *cgiargs, int is_post) {
  char buf[MAXLINE], *emptylist[] = { filename, NULL };
  int fildes[2];

  /* Return first part of HTTP response */
  sprintf (buf, "HTTP/1.0 200 OK\r\n");
  if (rio_writen (rio->rio_fd, buf, strlen (buf)) < 0)
    return;
  sprintf (buf, "Server: Tiny Web Server\r\n");
  if (rio_writen (rio->rio_fd, buf, strlen (buf)) < 0)
    return;

  // Use pipe to cat rio to the stdin of the child.
  // Students should not use forks, pipe, dup, or the likes in their proxylab.
  if (is_post)
    pipe(fildes);

  if (Fork() == 0) { /* Child */
    dict_foreach (hdrs, {
        setenv (key, val, 1); });
    if (is_post) {
      close(fildes[1]);                       /* Write end is unused */
      Dup2 (fildes[0], STDIN_FILENO);         /* Read end is stdin */
    }
    /* Real server would set all CGI vars here */
    setenv ("QUERY_STRING", cgiargs, 1);
    Dup2 (rio->rio_fd, STDOUT_FILENO);        /* Redirect stdout to client */
    Execve (filename, emptylist, environ); /* Run CGI program */
  }

  if (is_post) {
    close(fildes[0]);                       /* Read end is unused */
    char *content_length = dict_get (hdrs, "Content-Length");
    if (content_length) {
      int len = atoi (content_length);
      while (len > 0) {
        int readlen = rio_readnb (rio, buf, MAXLINE < len ? MAXLINE : len);
        if (readlen == 0)
          break;
        len -= readlen;
        if (rio_writen (fildes[1], buf, readlen) < 0) {
          close (fildes[1]);
          Wait (NULL);
          return;
        }
      }
    }
    else
      printf ("No content-length!\n");
    close(fildes[1]);
  }

  Wait (NULL); /* Parent waits for and reaps child */
}

/*
 * clienterror - returns an error message to the client
 */
void clienterror (int fd, char *cause, char *errnum,
                  char *shortmsg, char *longmsg) {
  char buf[MAXLINE];

  /* Print the HTTP response headers */
  sprintf (buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen (fd, buf, strlen (buf));
  sprintf (buf, "Content-type: text/html\r\n\r\n");
  Rio_writen (fd, buf, strlen (buf));

  /* Print the HTTP response body */
  sprintf (buf, "<html><title>Tiny Error</title>");
  Rio_writen (fd, buf, strlen (buf));
  sprintf (buf, "<body bgcolor=""ffffff"">\r\n");
  Rio_writen (fd, buf, strlen (buf));
  sprintf (buf, "%s: %s\r\n", errnum, shortmsg);
  Rio_writen (fd, buf, strlen (buf));
  sprintf (buf, "<p>%s: %s\r\n", longmsg, cause);
  Rio_writen (fd, buf, strlen (buf));
  sprintf (buf, "<hr><em>The Tiny Web server</em>\r\n");
  Rio_writen (fd, buf, strlen (buf));
}
