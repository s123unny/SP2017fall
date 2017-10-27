#include "csiebox_client.h"

#include "csiebox_common.h"
#include "connect.h"
#include "hash.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/inotify.h>

#define EVENT_SIZE (sizeof(struct inotify_event))
#define EVENT_BUF_LEN (1024 * (EVENT_SIZE + 16))

static int parse_arg(csiebox_client* client, int argc, char** argv);
static int login(csiebox_client* client);

//read config file, and connect to server
void csiebox_client_init(csiebox_client** client, int argc, char** argv) {
    csiebox_client* tmp = (csiebox_client*)malloc(sizeof(csiebox_client));
    if (!tmp) {
        fprintf(stderr, "client malloc fail\n");
        return;
    }
    memset(tmp, 0, sizeof(csiebox_client));
    if (!parse_arg(tmp, argc, argv)) {
        fprintf(stderr, "Usage: %s [config file]\n", argv[0]);
        free(tmp);
        return;
    }
    int fd = client_start(tmp->arg.name, tmp->arg.server);
    if (fd < 0) {
        fprintf(stderr, "connect fail\n");
        free(tmp);
        return;
    }
    tmp->conn_fd = fd;
    *client = tmp;
}
unsigned long get_size_by_fd(int fd) {
    struct stat statbuf;
    if(fstat(fd, &statbuf) < 0) exit(-1);
    return statbuf.st_size;
}

int process_Meta(csiebox_client* client, char* path, struct stat statbuf) {
    csiebox_protocol_meta req;
    memset(&req, 0, sizeof(req));
    req.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
    req.message.header.req.op = CSIEBOX_PROTOCOL_OP_SYNC_META;
    req.message.header.req.datalen = sizeof(req) - sizeof(req.message.header);
    req.message.body.pathlen = strlen(path);
    req.message.body.stat = statbuf;
    if (!S_ISDIR(statbuf.st_mode))md5_file(path, req.message.body.hash);

    //send pathlen to server so that server can know how many charachers it should receive
    if (!send_message(client->conn_fd, &req, sizeof(req))) {
        fprintf(stderr, "send fail\n");
        return 0;
    }

    //send path
    send_message(client->conn_fd, path+8, strlen(path));

    //receive csiebox_protocol_header from server
    csiebox_protocol_header header;
    memset(&header, 0, sizeof(header));
    if (recv_message(client->conn_fd, &header, sizeof(header))) {
        if (header.res.magic == CSIEBOX_PROTOCOL_MAGIC_RES &&
            header.res.op == CSIEBOX_PROTOCOL_OP_SYNC_META ) {
            if (header.res.status == CSIEBOX_PROTOCOL_STATUS_OK) {
                printf("Receive OK from server\n");
                return 1;
            } else if (header.res.status == CSIEBOX_PROTOCOL_STATUS_MORE) {
                printf("Receive MORE from server\n");
                return 2;
            }
        } else {
            return 0;
        }
    }
}

int process_file(csiebox_client* client, char *fullpath, struct stat statbuf) {
    csiebox_protocol_file req;
    memset(&req, 0, sizeof(req));
    req.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
    req.message.header.req.op = CSIEBOX_PROTOCOL_OP_SYNC_FILE;
    req.message.header.req.datalen = sizeof(req) - sizeof(req.message.header);

    int fd;
    void* file_buffer;
    unsigned long file_size;
    if (S_ISLNK(statbuf.st_mode)) {
        fprintf(stderr,"123\n");
        char temp[300];
        readlink(fullpath, temp, 300);
        fprintf(stderr, "%s\n", temp);
        file_size = strlen(temp);
        file_buffer = temp;
        req.message.body.datalen = file_size;
    } else {
        fd = open(fullpath, O_RDONLY);
        file_size = get_size_by_fd(fd);
        req.message.body.datalen = file_size;
        file_buffer = mmap(NULL, file_size, PROT_READ, MAP_SHARED , fd , 0);
    }
    
    //send data length
    if (!send_message(client->conn_fd, &req, sizeof(req))) {
        fprintf(stderr, "send fail\n");
        return 0;
    }
    //send data
    send_message(client->conn_fd, file_buffer, file_size);
    if (!S_ISLNK(statbuf.st_mode)) 
        munmap(file_buffer, file_size); 

    //receive csiebox_protocol_header from server
    csiebox_protocol_header header;
    memset(&header, 0, sizeof(header));
    if (recv_message(client->conn_fd, &header, sizeof(header))) {
        if (header.res.magic == CSIEBOX_PROTOCOL_MAGIC_RES &&
            header.res.op == CSIEBOX_PROTOCOL_OP_SYNC_FILE &&
            header.res.status == CSIEBOX_PROTOCOL_STATUS_OK) {
            printf("Receive OK from server\n");
            return 1;
        } else {
            return 0;
        }
    }
}
 
//Descend through the hierarchy, starting at "fullpath". 
//If "fullpath" is anything other than a directory, we lstat() it, 
//cFor a directory, we call ourself *recursively for each name in the directory. 
void dopath(csiebox_client* client, char *fullpath, int fd, hash* inotify_hash) {
    struct stat statbuf;
    struct dirent *dirp;
    DIR *dp;
    int n, wd;

    if (lstat(fullpath, &statbuf) < 0) {/* stat error */
        return;
    }
    if (S_ISDIR(statbuf.st_mode) == 0) {/* not a directory */
        printf("file: %s\n", fullpath);
        process_Meta(client, fullpath, statbuf);
        process_file(client, fullpath, statbuf);
        return; //file or softlink
    }
    printf("dir: %s\n", fullpath);
    n = strlen(fullpath);
    process_Meta(client, fullpath, statbuf);

    fullpath[n++] = '/';
    fullpath[n] = 0;

    if ((dp = opendir(fullpath)) == NULL) /* can't read directory */
        return;

    wd = inotify_add_watch(fd, fullpath, IN_CREATE | IN_MODIFY | IN_DELETE);
    if (wd != -1) {
        printf("Watching:: %s\n", fullpath);
        //put_into_hash(inotify_hash, (void*)fullpath, wd);
    }

    while ((dirp = readdir(dp)) != NULL) {
        if (strcmp(dirp->d_name, ".") == 0 || strcmp(dirp->d_name, "..") == 0)
            continue; /* ignore dot and dot-dot */
        strcpy(&fullpath[n], dirp->d_name); /* append name after "/" */
        dopath(client, fullpath, fd, inotify_hash);/* recursive */
    }
    fullpath[n-1] = 0; /* erase everything from slash onward */

    closedir(dp);
}

//this is where client sends request, you sould write your code here
int csiebox_client_run(csiebox_client* client) {
    if (!login(client)) {
        fprintf(stderr, "login fail\n");
        return 0;
    }
    fprintf(stderr, "login success\n");

    int length, i = 0;
    int fd;
    char buffer[EVENT_BUF_LEN];
    hash inotify_hash;

    //init_hash(&inotify_hash, 300);
    fd = inotify_init();
    if ( fd < 0 ) {
        perror( "Couldn't initialize inotify");
    }
    char fullpath[304] = "../cdir";
    dopath(client, fullpath, fd, &inotify_hash);

    while (1) {
        i = 0;
        length = read( fd, buffer, EVENT_BUF_LEN);

        /* Read the events*/
        while ( i < length ) {
            struct inotify_event *event = ( struct inotify_event * ) &buffer[ i ];
            if ( event->len ) {
                if ( event->mask & IN_CREATE) {
                    if (event->mask & IN_ISDIR) {
                        printf("%d DIR::%s CREATED\n", event->wd,event->name );
                        int wd = inotify_add_watch(fd, fullpath, IN_CREATE | IN_MODIFY | IN_DELETE);
                        if (wd != -1) {
                            printf("Watching:: %s\n", fullpath);
                            //put_into_hash(&inotify_hash, (void*)fullpath, wd);
                        }
                    } else
                        printf("%d FILE::%s CREATED\n", event->wd, event->name);      
                 }
           
                if ( event->mask & IN_MODIFY) {
                    if (event->mask & IN_ISDIR)
                        printf("%d DIR::%s MODIFIED\n", event->wd,event->name );      
                    else
                        printf("%d FILE::%s MODIFIED\n", event->wd,event->name );      
                }
           
                if ( event->mask & IN_DELETE) {
                    if (event->mask & IN_ISDIR)
                        printf("%d DIR::%s DELETED\n", event->wd,event->name );      
                    else
                        printf("%d FILE::%s DELETED\n", event->wd,event->name );      
                }
                i += EVENT_SIZE + event->len;
            }
        }
    }
    close(fd);
    return 1;
}

void csiebox_client_destroy(csiebox_client** client) {
    csiebox_client* tmp = *client;
    *client = 0;
    if (!tmp) {
        return;
    }
    close(tmp->conn_fd);
    free(tmp);
}

//read config file
static int parse_arg(csiebox_client* client, int argc, char** argv) {
    if (argc != 2) {
        return 0;
    }
    FILE* file = fopen(argv[1], "r");
    if (!file) {
        return 0;
    }
    fprintf(stderr, "reading config...\n");
    size_t keysize = 20, valsize = 20;
    char* key = (char*)malloc(sizeof(char) * keysize);
    char* val = (char*)malloc(sizeof(char) * valsize);
    ssize_t keylen, vallen;
    int accept_config_total = 5;
    int accept_config[5] = {0, 0, 0, 0, 0};
    while ((keylen = getdelim(&key, &keysize, '=', file) - 1) > 0) {
        key[keylen] = '\0';
        vallen = getline(&val, &valsize, file) - 1;
        val[vallen] = '\0';
        fprintf(stderr, "config (%d, %s)=(%d, %s)\n", keylen, key, vallen, val);
        if (strcmp("name", key) == 0) {
            if (vallen <= sizeof(client->arg.name)) {
                strncpy(client->arg.name, val, vallen);
                accept_config[0] = 1;
            }
        } else if (strcmp("server", key) == 0) {
            if (vallen <= sizeof(client->arg.server)) {
                strncpy(client->arg.server, val, vallen);
                accept_config[1] = 1;
            }
        } else if (strcmp("user", key) == 0) {
            if (vallen <= sizeof(client->arg.user)) {
                strncpy(client->arg.user, val, vallen);
                accept_config[2] = 1;
            }
        } else if (strcmp("passwd", key) == 0) {
            if (vallen <= sizeof(client->arg.passwd)) {
                strncpy(client->arg.passwd, val, vallen);
                accept_config[3] = 1;
            }
        } else if (strcmp("path", key) == 0) {
            if (vallen <= sizeof(client->arg.path)) {
                strncpy(client->arg.path, val, vallen);
                accept_config[4] = 1;
            }
        }
    }
    free(key);
    free(val);
    fclose(file);
    int i, test = 1;
    for (i = 0; i < accept_config_total; ++i) {
        test = test & accept_config[i];
    }
    if (!test) {
        fprintf(stderr, "config error\n");
        return 0;
    }
    return 1;
}

static int login(csiebox_client* client) {
  csiebox_protocol_login req;
  memset(&req, 0, sizeof(req));
  req.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
  req.message.header.req.op = CSIEBOX_PROTOCOL_OP_LOGIN;
  req.message.header.req.datalen = sizeof(req) - sizeof(req.message.header);
  memcpy(req.message.body.user, client->arg.user, strlen(client->arg.user));
  md5(client->arg.passwd,
      strlen(client->arg.passwd),
      req.message.body.passwd_hash);
  if (!send_message(client->conn_fd, &req, sizeof(req))) {
    fprintf(stderr, "send fail\n");
    return 0;
  }
  csiebox_protocol_header header;
  memset(&header, 0, sizeof(header));
  if (recv_message(client->conn_fd, &header, sizeof(header))) {
    if (header.res.magic == CSIEBOX_PROTOCOL_MAGIC_RES &&
        header.res.op == CSIEBOX_PROTOCOL_OP_LOGIN &&
        header.res.status == CSIEBOX_PROTOCOL_STATUS_OK) {
      client->client_id = header.res.client_id;
      return 1;
    } else {
      return 0;
    }
  }
  return 0;
}
