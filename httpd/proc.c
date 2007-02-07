/*
 *  Copyright(C) 2007 Cameron Rich
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <string.h>
#include "axhttp.h"

static int special_read(struct connstruct *cn, void *buf, size_t count);
static int special_write(struct connstruct *cn, 
                                        const uint8_t *buf, size_t count);
static void send_error(struct connstruct *cn, int err);
static int hexit(char c);
static void urldecode(char *buf);
static void buildactualfile(struct connstruct *cn);
static int sanitizefile(const char *buf);
static int sanitizehost(char *buf);

#if defined(CONFIG_HTTP_DIRECTORIES)
static void urlencode(const uint8_t *s, uint8_t *t);
static void procdirlisting(struct connstruct *cn);
#endif
#if defined(CONFIG_HTTP_HAS_CGI)
static void proccgi(struct connstruct *cn, int has_pathinfo);
static int trycgi_withpathinfo(struct connstruct *cn);
static void split(char *tp, char *sp[], int maxwords, char sc);
static int iscgi(const char *fn);
#endif
#ifdef CONFIG_HTTP_HAS_AUTHORIZATION
static int auth_check(struct connstruct *cn);
#endif

/* Returns 1 if elems should continue being read, 0 otherwise */
static int procheadelem(struct connstruct *cn, char *buf) 
{
    char *delim, *value;
#if defined(CONFIG_HTTP_HAS_CGI)
    char *cgi_delim;
#endif

    if ((delim = strchr(buf, ' ')) == NULL)
        return 0;

    *delim = 0;
    value = delim+1;
    printf("BUF %s - value %s\n", buf, value);

    if (strcmp(buf, "GET") == 0 ||
            strcmp(buf, "HEAD") == 0 ||
            strcmp(buf, "POST") == 0) 
    {
        if (buf[0] == 'H') 
            cn->reqtype = TYPE_HEAD;
        else if (buf[0] == 'P') 
            cn->reqtype = TYPE_POST;

        if ((delim = strchr(value, ' ')) == NULL)       /* expect HTTP type */
            return 0;
        *delim = 0;

        urldecode(value);

        if (sanitizefile(value) == 0) 
        {
            send_error(cn, 404);
            return 0;
        }

        my_strncpy(cn->filereq, value, MAXREQUESTLENGTH);
#if defined(CONFIG_HTTP_HAS_CGI)
        if ((cgi_delim = strchr(value, '?')))
        {
            *cgi_delim = 0;
            my_strncpy(cn->cgiargs, value+1, MAXREQUESTLENGTH);
        }
#endif

    } 
    else if (strcmp(buf, "Host:") == 0) 
    {
        if (sanitizehost(value) == 0) 
        {
            removeconnection(cn);
            return 0;
        }

        my_strncpy(cn->virtualhostreq, value, MAXREQUESTLENGTH);
    } 
    else if (strcmp(buf, "Connection:") == 0 && strcmp(value, "close") == 0) 
    {
        cn->close_when_done = 1;
    } 
    else if (strcmp(buf, "If-Modified-Since:") == 0) 
    {
        /* TODO: parse this date properly with getdate() or similar */
        cn->modified_since = 1;
    }
#ifdef CONFIG_HTTP_HAS_AUTHORIZATION
    else if (strcmp(buf, "Authorization:") == 0 &&
                            strncmp(value, "Basic ", 6) == 0)
    {
        if (base64_decode(&value[6], strlen(&value[6]), 
                                        cn->authorization, NULL))
            cn->authorization[0] = 0;   /* error */
    }
#endif

    return 1;
}

#if defined(CONFIG_HTTP_DIRECTORIES)
static void procdirlisting(struct connstruct *cn)
{
    char buf[MAXREQUESTLENGTH];
    char actualfile[1024];

    if (cn->reqtype == TYPE_HEAD) 
    {
        snprintf(buf, sizeof(buf), 
                "HTTP/1.1 200 OK\nContent-Type: text/html\n\n");
        write(cn->networkdesc, buf, strlen(buf));
        removeconnection(cn);
        return;
    }

    strcpy(actualfile, cn->actualfile);

#ifdef WIN32
    strcat(actualfile, "*");
    cn->dirp = FindFirstFile(actualfile, &cn->file_data);

    if (cn->dirp == INVALID_HANDLE_VALUE) 
    {
        send_error(cn, 404);
        return;
    }
#else
    if ((cn->dirp = opendir(actualfile)) == NULL) 
    {
        send_error(cn, 404);
        return;
    }

    /* Get rid of the "." */
    readdir(cn->dirp);
#endif

    snprintf(buf, sizeof(buf), "HTTP/1.1 200 OK\nContent-Type: text/html\n\n"
            "<html><body>\n<title>Directory Listing</title>\n"
            "<h3>Directory listing of %s://%s%s</h3><br />\n", 
            cn->is_ssl ? "https" : "http", cn->virtualhostreq, cn->filereq);
    special_write(cn, buf, strlen(buf));
    cn->state = STATE_DOING_DIR;
}

void procdodir(struct connstruct *cn) 
{
#ifndef WIN32
    struct dirent *dp;
#endif
    char buf[MAXREQUESTLENGTH];
    char encbuf[1024];
    char *file;

    do 
    {
       buf[0] = 0;

#ifdef WIN32
        if (!FindNextFile(cn->dirp, &cn->file_data)) 
#else
        if ((dp = readdir(cn->dirp)) == NULL)  
#endif
        {
            snprintf(buf, sizeof(buf), "</body></html>\n");
            special_write(cn, buf, strlen(buf));
            removeconnection(cn);
            return;
        }

#ifdef WIN32
        file = cn->file_data.cFileName;
#else
        file = dp->d_name;
#endif

        /* if no index file, don't display the ".." directory */
        if (cn->filereq[0] == '/' && cn->filereq[1] == '\0' &&
                strcmp(file, "..") == 0) 
            continue;

        /* don't display files beginning with "." */
        if (file[0] == '.' && file[1] != '.')
            continue;

        /* make sure a '/' is at the end of a directory */
        if (cn->filereq[strlen(cn->filereq)-1] != '/')
            strcat(cn->filereq, "/");

        /* see if the dir + file is another directory */
        snprintf(buf, sizeof(buf), "%s%s", cn->actualfile, file);
        if (isdir(buf))
            strcat(file, "/");

        urlencode(file, encbuf);
        snprintf(buf, sizeof(buf), "<a href=\"%s%s\">%s</a><br />\n",
                cn->filereq, encbuf, file);
    } while (special_write(cn, buf, strlen(buf)));
}

/* Encode funny chars -> %xx in newly allocated storage */
/* (preserves '/' !) */
static void urlencode(const uint8_t *s, uint8_t *t) 
{
    const uint8_t *p = s;
    uint8_t *tp;

    tp = t;

    for (; *p; p++) 
    {
        if ((*p > 0x00 && *p < ',') ||
                (*p > '9' && *p < 'A') ||
                (*p > 'Z' && *p < '_') ||
                (*p > '_' && *p < 'a') ||
                (*p > 'z' && *p < 0xA1)) 
        {
            sprintf((char *)tp, "%%%02X", *p);
            tp += 3; 
        } 
        else 
        {
            *tp = *p;
            tp++;
        }
    }

    *tp='\0';
}

#endif

void procreadhead(struct connstruct *cn) 
{
    char buf[MAXREQUESTLENGTH*4], *tp, *next;
    int rv;

    rv = special_read(cn, buf, sizeof(buf)-1);
    if (rv <= 0) 
    {
        if (rv < 0) /* really dead? */
            removeconnection(cn);
        return;
    }

    buf[rv] = '\0';
    next = tp = buf;

#ifdef CONFIG_HTTP_HAS_AUTHORIZATION
    cn->authorization[0] = 0;
#endif

    /* Split up lines and send to procheadelem() */
    while (*next != '\0') 
    {
        /* If we have a blank line, advance to next stage! */
        if (*next == '\r' || *next == '\n') 
        {
            buildactualfile(cn);
            cn->state = STATE_WANT_TO_SEND_HEAD;
            return;
        }

        while (*next != '\r' && *next != '\n' && *next != '\0') 
            next++;

        if (*next == '\r') 
        {
            *next = '\0';
            next += 2;
        }
        else if (*next == '\n') 
            *next++ = '\0';

        if (procheadelem(cn, tp) == 0) 
            return;

        tp = next;
    }
}

/* In this function we assume that the file has been checked for
 * maliciousness (".."s, etc) and has been decoded
 */
void procsendhead(struct connstruct *cn) 
{
    char buf[MAXREQUESTLENGTH];
    struct stat stbuf;
    time_t now = cn->timeout - CONFIG_HTTP_TIMEOUT;
    char date[32];

#ifdef CONFIG_HTTP_HAS_AUTHORIZATION
    if (auth_check(cn))
    {
        removeconnection(cn);
        return;
    }
#endif

    strcpy(date, ctime(&now));

    if (stat(cn->actualfile, &stbuf) == -1) 
    {
#if defined(CONFIG_HTTP_HAS_CGI)
        if (trycgi_withpathinfo(cn) == 0) 
        { 
            /* We Try To Find A CGI */
            proccgi(cn, 1);
            return;
        }
#endif

        send_error(cn, 404);
        return;
    }

#if defined(CONFIG_HTTP_HAS_CGI)
    if (iscgi(cn->actualfile))
    {
#ifndef WIN32
        /* Set up CGI script */
        if ((stbuf.st_mode & S_IEXEC) == 0 || isdir(cn->actualfile)) 
        {
            send_error(cn, 404);
            return;
        }
#endif

        proccgi(cn, 0);
        return;
    }
#endif

    /* look for "index.html"? */
    if ((stbuf.st_mode & S_IFMT) == S_IFDIR) 
    {
        char tbuf[MAXREQUESTLENGTH];
        sprintf(tbuf, "%s%s", cn->actualfile, "index.html");
        if (stat(tbuf, &stbuf) != -1) 
            strcat(cn->actualfile, "index.html");
        else
        {
#if defined(CONFIG_HTTP_DIRECTORIES)
            /* If not, we do a directory listing of it */
            procdirlisting(cn);
#else
            send_error(cn, 404);
#endif
            return;
        }

#if defined(CONFIG_HTTP_HAS_CGI)
        /* If the index is a CGI file, handle it like any other CGI */
        if (iscgi(cn->actualfile))
        {
            /* Set up CGI script */
            if ((stbuf.st_mode & S_IEXEC) == 0 || isdir(cn->actualfile)) 
            {
                send_error(cn, 404);
                return;
            }

            proccgi(cn, 0);
            return;
        }
#endif
    }

    if (cn->modified_since) 
    {
        /* file has already been read before */
        snprintf(buf, sizeof(buf), "HTTP/1.1 304 Not Modified\nServer: "
                "axhttpd V%s\nDate: %s\n", VERSION, date);
        special_write(cn, buf, strlen(buf));
        cn->modified_since = 0;
        cn->state = STATE_WANT_TO_READ_HEAD;
        return;
    }

#ifdef CONFIG_HTTP_VERBOSE
    printf("axhttpd: %s send %s\n", 
            cn->is_ssl ? "https" : "http", cn->actualfile);
    TTY_FLUSH();
#endif

    snprintf(buf, sizeof(buf), "HTTP/1.1 200 OK\nServer: axhttpd V%s\n"
            "Content-Type: %s\nContent-Length: %ld\n"
            "Date: %sLast-Modified: %s\n", VERSION,
            getmimetype(cn->actualfile), (long) stbuf.st_size,
            date, ctime(&(stbuf.st_mtime))); /* ctime() has a \n on the end */

    special_write(cn, buf, strlen(buf));

    if (cn->reqtype == TYPE_HEAD) 
    {
        removeconnection(cn);
        return;
    } 
    else 
    {
        int flags = O_RDONLY;
#if defined(WIN32) || defined(CONFIG_PLATFORM_CYGWIN)
        flags |= O_BINARY;
#endif

        cn->filedesc = open(cn->actualfile, flags);
        if (cn->filedesc == -1) 
        {
            send_error(cn, 404);
            removeconnection(cn);
            return;
        }

#ifdef WIN32
        for (;;)
        {
            procreadfile(cn);
            if (cn->filedesc == -1)
                break;

            do 
            {
                procsendfile(cn);
            } while (cn->state != STATE_WANT_TO_READ_FILE);
        }
#else
        cn->state = STATE_WANT_TO_READ_FILE;
#endif
    }
}

void procreadfile(struct connstruct *cn) 
{
    int rv = read(cn->filedesc, cn->databuf, BLOCKSIZE);

    if (rv == 0 || rv == -1) 
    {
        close(cn->filedesc);
        cn->filedesc = -1;

        if (cn->close_when_done)        /* close immediately */
            removeconnection(cn);
        else 
        {                               /* keep socket open - HTTP 1.1 */
            cn->state = STATE_WANT_TO_READ_HEAD;
            cn->numbytes = 0;
        }

        return;
    }

    cn->numbytes = rv;
    cn->state = STATE_WANT_TO_SEND_FILE;
}

void procsendfile(struct connstruct *cn) 
{
    int rv = special_write(cn, cn->databuf, cn->numbytes);

    if (rv < 0)
        removeconnection(cn);
    else if (rv == cn->numbytes)
        cn->state = STATE_WANT_TO_READ_FILE;
    else if (rv == 0)
    { 
        /* Do nothing */ 
    }
    else 
    {
        memmove(cn->databuf, cn->databuf + rv, cn->numbytes - rv);
        cn->numbytes -= rv;
    }
}

static int special_write(struct connstruct *cn, 
                                        const uint8_t *buf, size_t count)
{
    if (cn->is_ssl)
    {
        SSL *ssl = ssl_find(servers->ssl_ctx, cn->networkdesc);
        return ssl ? ssl_write(ssl, (uint8_t *)buf, count) : -1;
    }
    else
        return SOCKET_WRITE(cn->networkdesc, buf, count);
}

static int special_read(struct connstruct *cn, void *buf, size_t count)
{
    int res;

    if (cn->is_ssl)
    {
        uint8_t *read_buf;
        SSL *ssl = ssl_find(servers->ssl_ctx, cn->networkdesc);

        if ((res = ssl_read(ssl, &read_buf)) > SSL_OK)
            memcpy(buf, read_buf, res > (int)count ? count : res);
    }
    else
        res = SOCKET_READ(cn->networkdesc, buf, count);

    return res;
}

#if defined(CONFIG_HTTP_HAS_CGI)
static void proccgi(struct connstruct *cn, int has_pathinfo) 
{
    int tpipe[2];
    char *myargs[5];
    char buf[MAXREQUESTLENGTH];
#ifdef WIN32
    int tmp_stdout;
#endif

    snprintf(buf, sizeof(buf), "HTTP/1.1 200 OK\nServer: axhttpd V%s\n%s",
            VERSION, (cn->reqtype == TYPE_HEAD) ? "\n" : "");
    special_write(cn, buf, strlen(buf));

    if (cn->reqtype == TYPE_HEAD) 
    {
        removeconnection(cn);
        return;
    }

#ifndef WIN32
    pipe(tpipe);

    if (fork() > 0)  /* parent */
    {
        /* Close the write descriptor */
        close(tpipe[1]);
        cn->filedesc = tpipe[0];
        cn->state = STATE_WANT_TO_READ_FILE;
        cn->close_when_done = 1;
        return;
    }

    /* The problem child... */

    /* Our stdout/stderr goes to the socket */
    dup2(tpipe[1], 1);
    dup2(tpipe[1], 2);

    /* If it was a POST request, send the socket data to our stdin */
    if (cn->reqtype == TYPE_POST) 
        dup2(cn->networkdesc, 0);
    else    /* Otherwise we can shutdown the read side of the sock */
        shutdown(cn->networkdesc, 0);

    close(tpipe[0]);
    close(tpipe[1]);
    myargs[0] = cn->actualfile;
    myargs[1] = cn->cgiargs;
    myargs[2] = NULL;

    if (!has_pathinfo) 
    {
        my_strncpy(cn->cgipathinfo, "/", MAXREQUESTLENGTH);
        my_strncpy(cn->cgiscriptinfo, cn->filereq, MAXREQUESTLENGTH);
    }

    execv(cn->actualfile, myargs);
#else /* WIN32 */
    _pipe(tpipe, 4096, O_BINARY| O_NOINHERIT);

    myargs[0] = "sh";
    myargs[1] = "-c";
    myargs[2] = cn->actualfile;
    myargs[3] = cn->cgiargs;
    myargs[4] = NULL;

    /* convert all the forward slashes to back slashes */
    {
        char *t = myargs[2];
        while ((t = strchr(t, '\\')))
        {
            *t++ = '/';
        }
    }

    tmp_stdout = _dup(_fileno(stdout));
    _dup2(tpipe[1], _fileno(stdout));
    close(tpipe[1]);

    /* change to suit execution method */
    if (spawnl(P_NOWAIT, "c:\\Program Files\\cygwin\\bin\\sh.exe", 
                myargs[0], myargs[1], myargs[2], myargs[3], myargs[4]) == -1) 
    {
        removeconnection(cn);
        return;
    }

    _dup2(tmp_stdout, _fileno(stdout));
    close(tmp_stdout);
    cn->filedesc = tpipe[0];
    cn->state = STATE_WANT_TO_READ_FILE;
    cn->close_when_done = 1;

    for (;;)
    {
        procreadfile(cn);

        if (cn->filedesc == -1)
            break;

        procsendfile(cn);
        usleep(200000); /* don't know why this delay makes it work (yet) */
    }
#endif
}

static int trycgi_withpathinfo(struct connstruct *cn)
{
    char tpfile[MAXREQUESTLENGTH];
    char fr_str[MAXREQUESTLENGTH];
    char *fr_rs[MAXCGIARGS]; /* filereq splitted */
    int i = 0, offset;

    my_strncpy(fr_str, cn->filereq, MAXREQUESTLENGTH);
    split(fr_str, fr_rs, MAXCGIARGS, '/');

    while (fr_rs[i] != NULL) 
    {
        snprintf(tpfile, sizeof(tpfile), "/%s%s", 
                            cn->virtualhostreq, fr_str);

        if (iscgi(tpfile) && isdir(tpfile) == 0)
        {
            /* We've found our CGI file! */
            my_strncpy(cn->actualfile, tpfile, MAXREQUESTLENGTH);
            my_strncpy(cn->cgiscriptinfo, fr_str, MAXREQUESTLENGTH);

            offset = (fr_rs[i] + strlen(fr_rs[i])) - fr_str;
            my_strncpy(cn->cgipathinfo, cn->filereq+offset, MAXREQUESTLENGTH);

            return 0;
        }

        *(fr_rs[i]+strlen(fr_rs[i])) = '/';
        i++;
    }

    /* Couldn't find any CGIs :( */
    *(cn->cgiscriptinfo) = '\0';
    *(cn->cgipathinfo) = '\0';
    return -1;
}

static int iscgi(const char *fn)
{
    struct cgiextstruct *tp;
    int fnlen, extlen;

    fnlen = strlen(fn);
    tp = cgiexts;

    while (tp != NULL) 
    {
        extlen = strlen(tp->ext);

        if (strcasecmp(fn+(fnlen-extlen), tp->ext) == 0)
            return 1;

        tp = tp->next;
    }

    return 0;
}

static void split(char *tp, char *sp[], int maxwords, char sc)
{
    int i = 0;

    while(1) 
    {
        /* Skip leading whitespace */
        while (*tp == sc) tp++;

        if (*tp == '\0') 
        {
            sp[i] = NULL;
            break;
        }

        if (i==maxwords-2) 
        {
            sp[maxwords-2] = NULL;
            break;
        }

        sp[i] = tp;

        while(*tp != sc && *tp != '\0') 
            tp++;

        if (*tp == sc) 
            *tp++ = '\0';

        i++;
    }
}
#endif  /* CONFIG_HTTP_HAS_CGI */

/* Decode string %xx -> char (in place) */
static void urldecode(char *buf) 
{
    int v;
    char *p, *s, *w;

    w = p = buf;

    while (*p) 
    {
        v = 0;

        if (*p == '%') 
        {
            s = p;
            s++;

            if (isxdigit((int) s[0]) && isxdigit((int) s[1]))
            {
                v = hexit(s[0])*16 + hexit(s[1]);

                if (v) 
                { 
                    /* do not decode %00 to null char */
                    *w = (char)v;
                    p = &s[1];
                }
            }

        }

        if (!v) *w=*p;
        p++; 
        w++;
    }

    *w='\0';
}

static int hexit(char c) 
{
    if (c >= '0' && c <= '9')
        return c - '0';
    else if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    else if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    else
        return 0;
}

static void buildactualfile(struct connstruct *cn)
{
    snprintf(cn->actualfile, MAXREQUESTLENGTH, "%s", cn->filereq);

    /* Add directory slash if not there */
    if (isdir(cn->actualfile) && 
            cn->actualfile[strlen(cn->actualfile)-1] != '/')
        strcat(cn->actualfile, "/");

#ifdef WIN32
    /* convert all the forward slashes to back slashes */
    {
        char *t = cn->actualfile;
        while ((t = strchr(t, '/')))
            *t++ = '\\';
    }
#endif
}

static int sanitizefile(const char *buf) 
{
    int len, i;

    /* Don't accept anything not starting with a / */
    if (*buf != '/') 
        return 0;

    len = strlen(buf);
    for (i = 0; i < len; i++) 
    {
        /* Check for "/." i.e. don't send files starting with a . */
        if (buf[i] == '/' && buf[i+1] == '.') 
            return 0;
    }

    return 1;
}

static int sanitizehost(char *buf)
{
    while (*buf != '\0') 
    {
        /* Handle the port */
        if (*buf == ':') 
        {
            *buf = '\0';
            return 1;
        }

        /* Enforce some basic URL rules... */
        if ((isalnum(*buf) == 0 && *buf != '-' && *buf != '.') ||
                (*buf == '.' && *(buf+1) == '.') ||
                (*buf == '.' && *(buf+1) == '-') ||
                (*buf == '-' && *(buf+1) == '.'))
            return 0;

        buf++;
    }

    return 1;
}

#ifdef CONFIG_HTTP_HAS_AUTHORIZATION
#define AUTH_FILE       ".htpasswd"

static void send_authenticate(struct connstruct *cn, const char *realm)
{
    char buf[1024];

    snprintf(buf, sizeof(buf), "HTTP/1.1 401 Unauthorized\n"
         "WWW-Authenticate: Basic\n"
                 "realm=\"%s\"\n", realm);
    special_write(cn, buf, strlen(buf));
}

static int check_digest(char *b64_file_passwd, const char *msg_passwd)
{
    uint8_t real_salt[MAXREQUESTLENGTH];
    uint8_t real_passwd[MAXREQUESTLENGTH];
    int real_passwd_size, real_salt_size;
    char *b64_pwd;
    uint8_t md5_result[MD5_SIZE];
    MD5_CTX ctx;

    /* retrieve the salt */
    if ((b64_pwd = strchr(b64_file_passwd, '$')) == NULL)
        return -1;

    *b64_pwd++ = 0;
    if (base64_decode(b64_file_passwd, strlen(b64_file_passwd), 
                                            real_salt, &real_salt_size))
        return -1;

    if (base64_decode(b64_pwd, strlen(b64_pwd), real_passwd, &real_passwd_size))
        return -1;

    /* very simple MD5 crypt algorithm, but then the salt we use is large */
    MD5Init(&ctx);
    MD5Update(&ctx, real_salt, real_salt_size);     /* process the salt */
    MD5Update(&ctx, msg_passwd, strlen(msg_passwd)); /* process the password */
    MD5Final(&ctx, md5_result);
    return memcmp(md5_result, real_passwd, MD5_SIZE);/* 0 = ok */
}

static int auth_check(struct connstruct *cn)
{
    char dirname[MAXREQUESTLENGTH];
    char authpath[MAXREQUESTLENGTH];
    char line[MAXREQUESTLENGTH];
    char *cp;
    FILE *fp = NULL;
    struct stat auth_stat;

    strncpy(dirname, cn->actualfile, MAXREQUESTLENGTH);
    cp = strrchr(dirname, '/');
    if (cp == NULL)
        dirname[0] = 0;
    else
        *cp = 0;

    /* check that the file is not the AUTH_FILE itself */
    if (strcmp(cn->actualfile, AUTH_FILE) == 0)
    {
        send_error(cn, 403);
        goto error;
    }

    snprintf(authpath, MAXREQUESTLENGTH, "%s/%s", dirname, AUTH_FILE);
    if (stat(authpath, &auth_stat) < 0)    /* no auth file, so let though */
        return 0;

    if (cn->authorization[0] == 0)
    {
        send_authenticate(cn, dirname);
        return -1;
    }

    /* cn->authorization is in form "username:password" */
    if ((cp = strchr(cn->authorization, ':')) == NULL)
        goto error;
    else
        *cp++ = 0;  /* cp becomes the password */

    fp = fopen(authpath, "r");

    while (fgets(line, sizeof(line), fp) != NULL)
    {
        char *b64_file_passwd;
        int l = strlen(line);

        /* nuke newline */
        if (line[l-1] == '\n')
            line[l-1] = 0;

        /* line is form "username:salt(b64)$password(b64)" */
        if ((b64_file_passwd = strchr(line, ':')) == NULL)
            continue;

        *b64_file_passwd++ = 0;

        if (strcmp(line, cn->authorization)) /* our user? */
            continue;

        if (check_digest(b64_file_passwd, cp) == 0)
        {
            fclose(fp);
            return 0;
        }
    }

    fclose(fp);

error:
    send_authenticate(cn, dirname);
    return -1;
}
#endif

static void send_error(struct connstruct *cn, int err)
{
    char buf[1024];
    char *title;
    char *text;

    switch (err)
    {
        case 403:
            title = "Forbidden";
            text = "File is protected";
            break;

        case 404:
            title = "Not Found";
            text = title;
            break;
    }

    sprintf(buf, "HTTP/1.1 %d %s\nContent-Type: text/html\n"
            "<html><body>\n<title>%d %s</title>"
            "<h1>%d %s</h1>\n</body></html>\n", 
            err, title, err, title, err, text);
    special_write(cn, buf, strlen(buf));
    removeconnection(cn);
}
