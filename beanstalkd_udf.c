#include "beanstalk.h"
#include <stdio.h>
#include <assert.h>

#include <my_global.h>
#include <my_sys.h>
#include <mysql.h>

#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

typedef struct beanstalkd_server beanstalkd_server_st;

struct beanstalkd_server
{
  char *host;
  char *tube;
  int socket;
  beanstalkd_server_st *next;
  beanstalkd_server_st *prev;
};

pthread_mutex_t _server_lock = PTHREAD_MUTEX_INITIALIZER;
beanstalkd_server_st *_server_list = NULL;

void udf_debug( char *msg, ... ) {
#ifdef DEBUG
  va_list ap;

  va_start(ap, msg);
  vfprintf(stderr, msg, ap);
  va_end(ap);

  fflush(stderr);
#endif
}

my_bool _do_init(UDF_INIT *initid, UDF_ARGS *args, char *message);
void _do_deinit(UDF_INIT *initid);
bool _find_server(beanstalkd_server_st **client, UDF_ARGS *args);

my_bool beanstalkd_set_server_init(UDF_INIT *initid, UDF_ARGS *args, char *message);
char *beanstalkd_set_server(UDF_INIT *initid, UDF_ARGS *args, char *result, unsigned long *length, char *is_null, char *error);

my_bool beanstalkd_do_init(UDF_INIT *initid, UDF_ARGS *args, char *message);
char *beanstalkd_do(UDF_INIT *initid, UDF_ARGS *args, char *result, unsigned long *length, char *is_null, char *error);
void beanstalkd_do_deinit(UDF_INIT *initid);

my_bool _do_init(UDF_INIT *initid, UDF_ARGS *args, char *message)
{
  if (args->arg_count < 1 || args->arg_count > 3)
  { 
    strncpy(message, "Must give two or three arguments.", MYSQL_ERRMSG_SIZE);
    message[MYSQL_ERRMSG_SIZE - 1]= 0;
    return 1;
  }

  if (args->maybe_null[0] == 1 || args->arg_type[0] != STRING_RESULT)
  {
    strncpy(message, "First argument must be a string.", MYSQL_ERRMSG_SIZE);
    message[MYSQL_ERRMSG_SIZE - 1]= 0;
    return 1;
  }

  if (args->arg_count >= 2)
  {
    args->arg_type[1] = STRING_RESULT;
  }
  if (args->arg_count == 3)
  {
      args->arg_type[2] = STRING_RESULT;
  }
/*
  initid->ptr = calloc(1, sizeof(beanstalkd_server_st));
  if (initid->ptr == NULL)
  { 
    snprintf(message, MYSQL_ERRMSG_SIZE, "calloc() failed: %d", errno);
    return 1;
  }
*/  
/*
  initid->maybe_null= 1;
  initid->max_length= GEARMAN_UDF_RESULT_SIZE;
  initid->const_item= 0;
*/
  return 0;
}

void _do_deinit(UDF_INIT *initid)
{
  beanstalkd_server_st *client= (beanstalkd_server_st *)(initid->ptr);

  if (client == NULL)
  {
    return;
  }

  free(client);
}

bool _find_server(beanstalkd_server_st **client, UDF_ARGS *args)
{
  beanstalkd_server_st *server;
  beanstalkd_server_st *default_server= NULL;

  pthread_mutex_lock(&_server_lock);

  #ifdef DEBUG
  for (server = _server_list; server != NULL; server = server->next)
  {
    udf_debug("server: s:%p h:%p t:%p s:%d n:%p p:%p\n", server, server->host, server->tube, server->socket, server->next, server->prev);
  }
  #endif

  bool found = false;
  for (server = _server_list; server != NULL; server = server->next)
  {
    // job, tube
    if (args->arg_count == 2)
    {
      if (server->tube != NULL && strcmp(server->tube, args->args[1]) == 0)
      {
        found = true;
        break;
      }
    }

    // job, tube, server
    if (args->arg_count == 3)
    {
      if (server->tube != NULL && strcmp(server->tube, args->args[1]) == 0 && strcmp(server->host, args->args[2]) == 0)
      {
        found = true;
        break;
      }
    }

    // job
    default_server = server;
  }

  if (server == NULL)
  {
    if (default_server == NULL)
    {
      pthread_mutex_unlock(&_server_lock);
      return false;
    }

    server = default_server;
  }

  *client = server;
  pthread_mutex_unlock(&_server_lock);

  return true;
}

my_bool beanstalkd_set_server_init(UDF_INIT *initid, UDF_ARGS *args, char *message)
{
  if (args->arg_count < 1 || args->arg_count > 2 )
  { 
    strncpy(message, "Must give one or two arguments.", MYSQL_ERRMSG_SIZE);
    message[MYSQL_ERRMSG_SIZE - 1]= 0;
    return 1;
  }

  if (args->arg_count == 1 && (args->maybe_null[0] == 1 || args->arg_type[0] != STRING_RESULT))
  {
    strncpy(message, "First argument (server) must be a string.", MYSQL_ERRMSG_SIZE);
    message[MYSQL_ERRMSG_SIZE - 1]= 0;
    return 1;
  }

  if (args->arg_count == 2 && (args->maybe_null[1] == 1 || args->arg_type[1] != STRING_RESULT))
  {
    strncpy(message, "Second argument (tube) must be a number.", MYSQL_ERRMSG_SIZE);
    message[MYSQL_ERRMSG_SIZE - 1]= 0;
    return 1;
  }

  args->arg_type[0] = STRING_RESULT;
  if (args->arg_count == 2)
  {
    args->arg_type[1] = STRING_RESULT;
  }

  initid->maybe_null = 1;

  return 0;
}

char *beanstalkd_set_server(UDF_INIT *initid, UDF_ARGS *args, char *result, unsigned long *length, char *is_null, char *error)
{
  (void) initid;
  (void) is_null;
  beanstalkd_server_st *server;

  pthread_mutex_lock(&_server_lock);

  bool found = false;
  for (server = _server_list; server != NULL; server = server->next)
  {
    if (args->arg_count == 1 && server->tube == NULL && strcmp(args->args[0], server->host) == 0)
    {
      found = true;
      break;
    }
    if (args->arg_count == 2 && server->tube != NULL && strcmp(args->args[0], server->host) == 0 && strcmp(args->args[1], server->tube) == 0)
    {
      found = true;
      break;
    }
  }

  if (found == false)
  {
    server = NULL;
  }

  if (server == NULL)
  {
    server = malloc(sizeof(beanstalkd_server_st));
    if (server == NULL)
    {
      snprintf(result, 255, "malloc() failed: %d", errno);
      *length = strlen(result);
      pthread_mutex_unlock(&_server_lock);
      return result;
    }
    server->tube = NULL;
    server->socket = 0;
    server->next = NULL;
    server->prev = NULL;

    server->host = malloc(args->lengths[0]);
    if (server->host == NULL)
    {
      free(server);

      snprintf(result, 255, "malloc() failed: %d", errno);
      *length = strlen(result);
      pthread_mutex_unlock(&_server_lock);
      return result;
    }
    memcpy(server->host, args->args[0], args->lengths[0] + 1);

    if (args->arg_count == 2)
    {
      server->tube = malloc(args->lengths[1]);
      if (server->tube == NULL)
      {
        free(server->host);
        free(server);

        snprintf(result, 255, "malloc() failed: %d", errno);
        *length = strlen(result);
        pthread_mutex_unlock(&_server_lock);
        return result;
      }
      memcpy(server->tube, args->args[1], args->lengths[1] + 1);
    }

    server->socket = bs_connect(server->host, 11300);
    if (server->socket == BS_STATUS_FAIL)
    {
      free(server->host);
      if (server->tube != NULL)
      {
        free(server->tube);
      }
      free(server);

      strncpy(result, "bs_connect() failed.", 255);
      result[254] = 0;
      *length = strlen(result);
      pthread_mutex_unlock(&_server_lock);
      return result;
    }

    if (server->tube != NULL)
    {
      if(bs_use(server->socket, server->tube) != BS_STATUS_OK)
      {
        free(server->host);
        free(server->tube);
        free(server);

        snprintf(result, 255, "bs_use() failed");
        *length = strlen(result);
        pthread_mutex_unlock(&_server_lock);
        return result;
      }
      if (bs_watch(server->socket, server->tube) != BS_STATUS_OK)
      {
        free(server->host);
        free(server->tube);
        free(server);

        snprintf(result, 255, "bs_watch() failed");
        *length = strlen(result);
        pthread_mutex_unlock(&_server_lock);
        return result;
      }
      if (bs_ignore(server->socket, "default") != BS_STATUS_OK)
      {
        free(server->host);
        free(server->tube);
        free(server);

        snprintf(result, 255, "bs_ignore() failed");
        *length = strlen(result);
        pthread_mutex_unlock(&_server_lock);
        return result;
      }
    }

    if (_server_list != NULL)
    {
      _server_list->prev = server;
    }
    server->next = _server_list;
    _server_list = server;
  }

  snprintf(result, 255, "%s", args->args[0]);
  *length = strlen(result);
  pthread_mutex_unlock(&_server_lock);
  return result;
}

my_bool beanstalkd_do_init(UDF_INIT *initid, UDF_ARGS *args, char *message)
{
  return _do_init(initid, args, message);
}

void beanstalkd_do_deinit(UDF_INIT *initid)
{
  _do_deinit(initid);
}

char *beanstalkd_do(UDF_INIT *initid, UDF_ARGS *args, char *result, unsigned long *length, char *is_null, char *error)
{
  beanstalkd_server_st *client = (beanstalkd_server_st *)(initid->ptr);
  udf_debug("client: %p\n", client);
  char buffer[32];

  if (_find_server(&client, args) == false)
  {
    *error= 1;
    return NULL;
  }  

  udf_debug("client: %p\n", client);

  int id = bs_put(client->socket, 0, 0, 3600, args->args[0], args->lengths[0]);
  if (id < 1)
  {
    udf_debug("error: %d\n", id);
  }

  snprintf(buffer, 31, "%d", id);
  strcpy(result, buffer);
  *length = strlen(result);

  return result;
}
