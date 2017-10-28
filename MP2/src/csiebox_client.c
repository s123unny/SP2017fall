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
#define FILE_MAX 300
#define LEN_MAX 312

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

int process_Meta(csiebox_client* client, char* path, struct stat statbuf) {
    printf("process_Meta\n");
    csiebox_protocol_meta req;
    memset(&req, 0, sizeof(req));
    req.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
    req.message.header.req.op = CSIEBOX_PROTOCOL_OP_SYNC_META;
    req.message.header.req.datalen = sizeof(req) - sizeof(req.message.header);
    req.message.body.pathlen = strlen(path+8);
    req.message.body.stat = statbuf;
    if (!S_ISDIR(statbuf.st_mode))md5_file(path, req.message.body.hash);

    //send pathlen to server so that server can know how many charachers it should receive
    if (!send_message(client->conn_fd, &req, sizeof(req))) {
        fprintf(stderr, "send fail\n");
        return 0;
    }

    //send path
    send_message(client->conn_fd, path+8, req.message.body.pathlen);

    //receive csiebox_protocol_header from server
    csiebox_protocol_header header;
    memset(&header, 0, sizeof(header));
    if (recv_message(client->conn_fd, &header, sizeof(header))) {
        if (header.res.magic == CSIEBOX_PROTOCOL_MAGIC_RES &&
            header.res.op == CSIEBOX_PROTOCOL_OP_SYNC_META ) {
            if (header.res.status == CSIEBOX_PROTOCOL_STATUS_OK) {
                printf("meta_Receive OK from server\n");
                return 1;
            } else if (header.res.status == CSIEBOX_PROTOCOL_STATUS_MORE) {
                printf("meta_Receive MORE from server\n");
                return 2;
            }
        } else {
            return 0;
        }
    }
}

int process_file(csiebox_client* client, char *fullpath, struct stat statbuf) {
    printf("process_file\n");
    csiebox_protocol_file req;
    memset(&req, 0, sizeof(req));
    req.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
    req.message.header.req.op = CSIEBOX_PROTOCOL_OP_SYNC_FILE;
    req.message.header.req.datalen = sizeof(req) - sizeof(req.message.header);

    int fd;
    void* file_buffer;
    unsigned long file_size;
    if (S_ISLNK(statbuf.st_mode)) { //symbolic link
        char temp[LEN_MAX];
        int len = readlink(fullpath, temp, LEN_MAX);
        temp[len] = '\0';
        file_size = strlen(temp);
        file_buffer = temp;
    } else { //regular file
        fd = open(fullpath, O_RDONLY);
        file_size = statbuf.st_size;
        file_buffer = mmap(NULL, file_size, PROT_READ, MAP_SHARED , fd , 0);
    }
    req.message.body.datalen = file_size;
    
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
            printf("file_Receive OK from server\n");
            return 1;
        } else {
            return 0;
        }
    }
}

int process_Hardlink(csiebox_client* client, char *fullpath, char *targetpath) {
    printf("process_Hardlink\n");
    csiebox_protocol_hardlink req;
    memset(&req, 0, sizeof(req));
    req.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
    req.message.header.req.op = CSIEBOX_PROTOCOL_OP_SYNC_HARDLINK;
    req.message.header.req.datalen = sizeof(req) - sizeof(req.message.header);
    req.message.body.srclen = strlen(fullpath+8);
    req.message.body.targetlen = strlen(targetpath+8);
    if (!send_message(client->conn_fd, &req, sizeof(req))) {
        fprintf(stderr, "send fail\n");
        return 0;
    }

    //send path
    send_message(client->conn_fd, fullpath+8, req.message.body.srclen);
    send_message(client->conn_fd, targetpath+8, req.message.body.targetlen);

    //receive csiebox_protocol_header from server
    csiebox_protocol_header header;
    memset(&header, 0, sizeof(header));
    if (recv_message(client->conn_fd, &header, sizeof(header))) {
        if (header.res.magic == CSIEBOX_PROTOCOL_MAGIC_RES &&
            header.res.op == CSIEBOX_PROTOCOL_OP_SYNC_HARDLINK &&
            header.res.status == CSIEBOX_PROTOCOL_STATUS_OK) {
            printf("hardlink_Receive OK from server\n");
            return 1;
        } else {
            return 0;
        }
    }
}

int process_RM(csiebox_client* client, char *path) {
    printf("process_RM\n");
    printf("%s\n", path);
    csiebox_protocol_rm req;
    memset(&req, 0, sizeof(req));
    req.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
    req.message.header.req.op = CSIEBOX_PROTOCOL_OP_RM;
    req.message.header.req.datalen = sizeof(req) - sizeof(req.message.header);
    req.message.body.pathlen = strlen(path+8);

    //send pathlen to server so that server can know how many charachers it should receive
    if (!send_message(client->conn_fd, &req, sizeof(req))) {
        fprintf(stderr, "send fail\n");
        return 0;
    }

    //send path
    send_message(client->conn_fd, path+8, req.message.body.pathlen);

    //receive csiebox_protocol_header from server
    csiebox_protocol_header header;
    memset(&header, 0, sizeof(header));
    if (recv_message(client->conn_fd, &header, sizeof(header))) {
        if (header.res.magic == CSIEBOX_PROTOCOL_MAGIC_RES &&
            header.res.op == CSIEBOX_PROTOCOL_OP_RM &&
            header.res.status == CSIEBOX_PROTOCOL_STATUS_OK) {
            printf("rm_Receive OK from server\n");
            return 1;
        } else {
            return 0;
        }
    }
}
 
static int depth = 0;
static char longestpath[LEN_MAX];
static int num_inode = 0;
static ino_t inode[FILE_MAX];
static char inodepath[FILE_MAX][LEN_MAX];
static char wd_path[FILE_MAX][LEN_MAX];
static int wds[FILE_MAX];
static int num_of_wds = 0;

void put_into_wdpath(char *path, int wd) {
    if (num_of_wds < FILE_MAX) {
        wds[num_of_wds] = wd;
        strcpy(wd_path[num_of_wds], path);
        num_of_wds ++;
    } else {
        for (int i = 0; i < num_of_wds; i++) {
            if (!wds[i]) {
                wds[i] = wd;
                strcpy(wd_path[i], path);
                return;
            }
        }
    }
}
void get_path_from_wd(char *path, int wd) {
    for (int i = 0; i < num_of_wds; i++) {
        if (wds[i] == wd) {
            strcpy(path, wd_path[i]);
            return;
        }
    }
}
void delete_wd(int wd) {
    for (int i = 0; i < num_of_wds; i++) {
        if (wds[i] == wd) {
            wds[i] = 0;
            wd_path[i][0] = '\0';
            return;
        }
    }
}

//Descend through the hierarchy, starting at "fullpath". 
//If "fullpath" is anything other than a directory, we lstat() it, 
//cFor a directory, we call ourself *recursively for each name in the directory. 
void dopath(csiebox_client* client, char *fullpath, int fd, int d) {
    struct stat statbuf;
    struct dirent *dirp;
    DIR *dp;
    int n, wd;

    if (lstat(fullpath, &statbuf) < 0) {/* stat error */
        return;
    }
    printf(": %s\n", fullpath);

    //longest path
    if (d > depth) {
        depth = d;
        strcpy(longestpath, fullpath);
    }

    if (statbuf.st_nlink > 1 && !S_ISDIR(statbuf.st_mode)) {
        for (int i = 0; i < num_inode; i++) {
            if (inode[i] == statbuf.st_ino) {
                process_Hardlink(client, fullpath, inodepath[i]);
                return;
            }
        }
    }
    inode[num_inode] = statbuf.st_ino;
    strcpy(inodepath[num_inode], fullpath);
    num_inode ++;

    if (process_Meta(client, fullpath, statbuf) == 2) {
        process_file(client, fullpath, statbuf); //not dir
        return;
    }
   
    n = strlen(fullpath);
    fullpath[n++] = '/';
    fullpath[n] = 0;

    if ((dp = opendir(fullpath)) == NULL) /* can't read directory */
        return;

    wd = inotify_add_watch(fd, fullpath, IN_CREATE | IN_MODIFY | IN_DELETE | IN_ATTRIB);
    if (wd != -1) {
        printf("%d Watching:: %s\n", wd, fullpath);
        put_into_wdpath(fullpath, wd);
    }

    while ((dirp = readdir(dp)) != NULL) {
        if (strcmp(dirp->d_name, ".") == 0 || strcmp(dirp->d_name, "..") == 0)
            continue; /* ignore dot and dot-dot */
        strcpy(&fullpath[n], dirp->d_name); /* append name after "/" */
        dopath(client, fullpath, fd, d+1);/* recursive */
    }
    fullpath[n] = 0; /* erase everything from slash onward */
    lstat(fullpath, &statbuf);
    printf("return: %s\n", fullpath);
    process_Meta(client, fullpath, statbuf);
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
    
    fd = inotify_init();
    if ( fd < 0 ) {
        perror( "Couldn't initialize inotify");
    }
    char fullpath[LEN_MAX] = "../cdir";
    dopath(client, fullpath, fd, 0);

    int longestpath_fd = open("../cdir/longestPath.txt", O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
    printf("longestpath: %s\n", longestpath);
    write(longestpath_fd, longestpath, strlen(longestpath));
    close(longestpath_fd);

    struct stat statbuf;
    int len;
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
                        get_path_from_wd(fullpath, event->wd);
                        len = strlen(fullpath);
                        strcpy(fullpath+len, event->name);
                        int wd = inotify_add_watch(fd, fullpath, IN_CREATE | IN_MODIFY | IN_DELETE | IN_ATTRIB);
                        if (wd != -1) {
                            printf("Watching:: %s\n", fullpath);
                            put_into_wdpath(fullpath, wd);
                        }
                        lstat(fullpath, &statbuf);
                        process_Meta(client, fullpath, statbuf);
                        fullpath[len] = '\0';
                        lstat(fullpath, &statbuf);
                        process_Meta(client, fullpath, statbuf);
                    } else {
                        printf("%d FILE::%s CREATED\n", event->wd, event->name);   
                        get_path_from_wd(fullpath, event->wd);
                        len = strlen(fullpath);
                        strcpy(fullpath+len, event->name);
                        lstat(fullpath, &statbuf);
                        if (process_Meta(client, fullpath, statbuf) == 2)
                            process_file(client, fullpath, statbuf);
                        fullpath[len] = '\0';
                        lstat(fullpath, &statbuf);
                        process_Meta(client, fullpath, statbuf);
                    }
                }
                if (event->mask & IN_ATTRIB) {
                    if (event->mask & IN_ISDIR) {
                        printf("%d DIR::%s ATTRIB\n", event->wd,event->name );  
                        get_path_from_wd(fullpath, event->wd);
                        len = strlen(fullpath);
                        strcpy(fullpath+len, event->name);
                        lstat(fullpath, &statbuf);
                        process_Meta(client, fullpath, statbuf);
                    }
                    else {
                        printf("%d FILE::%s ATTRIB\n", event->wd,event->name );
                        get_path_from_wd(fullpath, event->wd);
                        len = strlen(fullpath);
                        strcpy(fullpath+len, event->name);
                        lstat(fullpath, &statbuf);
                        process_Meta(client, fullpath, statbuf);
                    }
                }
                if ( event->mask & IN_MODIFY) {
                    if (event->mask & IN_ISDIR) {
                        printf("%d DIR::%s MODIFIED\n", event->wd,event->name );      
                    }
                    else {
                        printf("%d FILE::%s MODIFIED\n", event->wd,event->name );
                        get_path_from_wd(fullpath, event->wd);
                        len = strlen(fullpath);
                        strcpy(fullpath+len, event->name);
                        lstat(fullpath, &statbuf);
                        if (process_Meta(client, fullpath, statbuf) == 2)
                            process_file(client, fullpath, statbuf);
                        fullpath[len] = '\0';
                        lstat(fullpath, &statbuf);
                        process_Meta(client, fullpath, statbuf);    
                    }
                }
                if ( event->mask & IN_DELETE_SELF) {
                    printf("%d DIR::%s DELETED\n", event->wd,event->name );
                    fprintf(stderr, "QQQ\n");
                    delete_wd(event->wd);
                    inotify_rm_watch(fd, event->wd);
                }
                if ( event->mask & IN_DELETE) {
                    if (event->mask & IN_ISDIR) {
                        printf("%d DIR::%s DELETED\n", event->wd,event->name );
                        fprintf(stderr, "WWW\n");
                    }
                    else {
                        printf("%d FILE::%s DELETED\n", event->wd,event->name );
                        get_path_from_wd(fullpath, event->wd);
                        len = strlen(fullpath);
                        strcpy(fullpath+len, event->name);
                        process_RM(client, fullpath);
                        fullpath[len] = '\0';
                        lstat(fullpath, &statbuf);
                        process_Meta(client, fullpath, statbuf);
                    }
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
