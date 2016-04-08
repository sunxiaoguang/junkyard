#include <zookeeper.h>
#include <proto.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <sys/time.h>
#include <unistd.h>
#include <sys/select.h>
#include <time.h>
#include <errno.h>
#include <assert.h>

#define _LL_CAST_ (long long)

static zhandle_t *zh;
static clientid_t myid;

static const char* state2String(int state)
{
  if (state == 0)
    return "CLOSED_STATE";
  if (state == ZOO_CONNECTING_STATE)
    return "CONNECTING_STATE";
  if (state == ZOO_ASSOCIATING_STATE)
    return "ASSOCIATING_STATE";
  if (state == ZOO_CONNECTED_STATE)
    return "CONNECTED_STATE";
  if (state == ZOO_EXPIRED_SESSION_STATE)
    return "EXPIRED_SESSION_STATE";
  if (state == ZOO_AUTH_FAILED_STATE)
    return "AUTH_FAILED_STATE";

  return "INVALID_STATE";
}

static const char* type2String(int state)
{
  if (state == ZOO_CREATED_EVENT)
    return "CREATED_EVENT";
  if (state == ZOO_DELETED_EVENT)
    return "DELETED_EVENT";
  if (state == ZOO_CHANGED_EVENT)
    return "CHANGED_EVENT";
  if (state == ZOO_CHILD_EVENT)
    return "CHILD_EVENT";
  if (state == ZOO_SESSION_EVENT)
    return "SESSION_EVENT";
  if (state == ZOO_NOTWATCHING_EVENT)
    return "NOTWATCHING_EVENT";

  return "UNKNOWN_EVENT_TYPE";
}

void watcher(zhandle_t *zzh, int type, int state, const char *path, void* context)
{
    fprintf(stderr, "Watcher %s state = %s", type2String(type), state2String(state));
    if (path && strlen(path) > 0) {
      fprintf(stderr, " for path %s", path);
    }
    fprintf(stderr, "\n");

    if (type == ZOO_SESSION_EVENT) {
      if (state == ZOO_CONNECTED_STATE) {
        const clientid_t *id = zoo_client_id(zzh);
        if (myid.client_id == 0 || myid.client_id != id->client_id) {
          myid = *id;
          fprintf(stderr, "Got a new session id: 0x%llx\n", _LL_CAST_ id->client_id);
        }
      } else if (state == ZOO_AUTH_FAILED_STATE) {
        fprintf(stderr, "Authentication failure. Shutting down...\n");
        zookeeper_close(zzh);
        zh = NULL;
      } else if (state == ZOO_EXPIRED_SESSION_STATE) {
        fprintf(stderr, "Session expired. Shutting down...\n");
        zookeeper_close(zzh);
        zh = NULL;
      }
    }
}

int main(int argc, char **argv) {
    FILE *file;
    int size;
    char *buffer = NULL;
    struct Stat stat;
    int rc;
    const char *command = argv[2];
    const char *path = argv[3];
    char *realpath = NULL;

    if (argc < 4) {
      fprintf(stderr, "USAGE %s address get|set|create|delete path [file]\n", argv[0]);
      fprintf(stderr, "Version: ZooKeeper cli (c client) version %d.%d.%d\n", 
              ZOO_MAJOR_VERSION, ZOO_MINOR_VERSION, ZOO_PATCH_VERSION);
      return 1;
    }

    zoo_set_debug_level(ZOO_LOG_LEVEL_WARN);
    zoo_deterministic_conn_order(1); // enable deterministic order
    zh = zookeeper_init(argv[1], watcher, 30000, &myid, 0, 0);
    if (!zh) {
      return errno;
    }
    if (strcasecmp("get", command) == 0) {
      size = 1024 * 32;
      buffer = malloc(size);
      rc = zoo_get(zh, path, 0, buffer, &size, &stat);
      if (rc == 0) {
        fprintf(stderr, "%s\n", buffer);
      }
      free(buffer);
    } else if (strcasecmp("delete", command) == 0) {
      rc = zoo_delete(zh, path, -1);
    } else if (strcasecmp("set", command) == 0 || strcasecmp("create", command) == 0) {
      if (argc < 5) {
        fprintf(stderr, "Missing required input file\n");
        return 1;
      }
      if (!(file = fopen(argv[4], "rb"))) {
        fprintf(stderr, "Could not open file %s\n", argv[4]);
        return 1;
      }
      fseek(file, 0, SEEK_END);
      size = ftell(file);
      fseek(file, 0, SEEK_SET);
      buffer = malloc(size);
      fread(buffer, size, 1, file);
  
      switch (command[0]) {
        case 'c':
        case 'C':
          realpath = malloc(1024);
          rc = zoo_create(zh, path, buffer, size,  &ZOO_OPEN_ACL_UNSAFE, 0, realpath, 1024);
          free(realpath);
          break;
        default:
          rc = zoo_set2(zh, path, buffer, size, -1, &stat);
      }
      free(buffer);
    }
    if (rc) {
      fprintf(stderr, "Failed to %s %s. %d\n", command, path, rc);
    }
    zookeeper_close(zh);
    return rc;
}
