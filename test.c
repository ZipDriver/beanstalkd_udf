#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <string.h>
#include <ctype.h>
#include <my_global.h>
#include <my_sys.h>
#include <mysql.h>

typedef char * (*func_s)(UDF_INIT *, UDF_ARGS *, char *, unsigned long *, char *, char *);
typedef longlong (*func_i)(UDF_INIT *, UDF_ARGS *,char *, char *);
typedef double (*func_d)(UDF_INIT *, UDF_ARGS *,char *, char *);
typedef my_bool (*func_init)(UDF_INIT *, UDF_ARGS*, char *);
typedef void (*func_deinit)(UDF_INIT *);

typedef void (*func_serverlist)();
typedef void (*func_serverclean)();
typedef void (*func_serverrelease)();

int call_udf(char *name, UDF_ARGS *args, int ret);
int call_udf_command(char *name, UDF_INIT *init, UDF_ARGS *args, int ret);

void free_args(UDF_ARGS *args)
{
  int i;
  for (i=0; i<args->arg_count; i++) 
  {
    if (args->args[i] != NULL)
    {
      free(args->args[i]);
    }
  }
  free(args->args);
  
  free(args->arg_type);
  free(args->maybe_null);
  free(args->lengths);
  free(args);
}

UDF_ARGS *alloc_args(int count)
{
  UDF_ARGS *args = malloc(sizeof(UDF_ARGS));
  if (args == NULL)
  {
    fprintf(stderr, "malloc() failed: %d", errno);
    return NULL;
  }

  args->arg_count = count;
  args->arg_type = malloc(args->arg_count * sizeof(int));
  args->args = malloc(args->arg_count * sizeof(char*));
  args->maybe_null = malloc(args->arg_count * sizeof(char));
  args->lengths = malloc(args->arg_count * sizeof(unsigned long));

  int i;
  for (i=0; i<args->arg_count; i++) 
  {
    args->args[i] = NULL;
  }

  return args;
}

int set_args_string(UDF_ARGS *args, int index, char *s)
{
  if (args == NULL)
  {
    fprintf(stderr, "args is NULL\n");
    return 0;
  }

  if (index <= 0 || index > args->arg_count)
  {
    fprintf(stderr, "arg_count %d and index %d not fit in\n", args->arg_count, index);
    return 0;
  }

  args->arg_type[index - 1] = STRING_RESULT;
  args->args[index - 1] = malloc(255);
  snprintf(args->args[index - 1], 254, "%s", s);
  args->maybe_null[index - 1] = 0;
  args->lengths[index - 1] = strlen(args->args[index - 1]);

  return 1;
}

void test_disconnect_first();
void test_disconnect_middle();
void test_disconnect_last();

void *handle;
char *error;
func_serverlist serverlist = NULL;
func_serverclean serverclean = NULL;
func_serverrelease serverrelease = NULL;

int main(int argc, char **argv)
{
  handle = dlopen("beanstalkd_udf.so", RTLD_GLOBAL | RTLD_NOW);
  if (!handle)
  {
    fprintf(stderr, "%s\n", dlerror());
    exit(1);
  }

  serverrelease = dlsym(handle, "_release_servers");
  if (serverrelease == NULL)
  {
    fprintf(stderr, "dlsym() %s\n", dlerror());
    exit(0);
  }

  serverlist = dlsym(handle, "_list_servers");
  if (serverlist == NULL)
  {
    fprintf(stderr, "dlsym() %s\n", dlerror());
    exit(0);
  }

  serverclean = dlsym(handle, "_clean_servers");
  if (serverclean == NULL)
  {
    fprintf(stderr, "dlsym() %s\n", dlerror());
    exit(0);
  }

  test_disconnect_first();
  serverrelease();

  dlclose(handle);
  exit(0);
}

void test_disconnect_first()
{
  UDF_ARGS *args;

  args = alloc_args(2);
  set_args_string(args, 1, "192.0.0.1");
  set_args_string(args, 2, "tube1");
  call_udf("beanstalkd_set_server", args, STRING_RESULT);
  free_args(args);

  args = alloc_args(2);
  set_args_string(args, 1, "127.0.0.1");
  set_args_string(args, 2, "tube2");
  call_udf("beanstalkd_set_server", args, STRING_RESULT);
  free_args(args);

  args = alloc_args(2);
  set_args_string(args, 1, "127.0.0.1");
  set_args_string(args, 2, "tube3");
  call_udf("beanstalkd_set_server", args, STRING_RESULT);
  free_args(args);

  args = alloc_args(2);
  set_args_string(args, 1, "127.0.0.1");
  set_args_string(args, 2, "tube4");
  call_udf("beanstalkd_set_server", args, STRING_RESULT);
  free_args(args);

  printf("STOP beanstalk and press ENTER");
  getchar();

  args = alloc_args(2);
  set_args_string(args, 1, "job1");
  set_args_string(args, 2, "tube1");
  call_udf("beanstalkd_do", args, STRING_RESULT);
  free_args(args);

  printf("Failed server marked (f:1)\n");

  serverlist();

  serverclean();

  serverlist();

  printf("START beanstalk and press ENTER");
  getchar();
}

int call_udf(char *name, UDF_ARGS *args, int ret)
{
  func_init finit = NULL;
  func_deinit fdeinit = NULL;

  char *buffer;
  int len;

  // init
  len = strlen(name) + 5 + 1;
  buffer = malloc(len);
  if (buffer == NULL)
  {
    fprintf(stderr, "malloc() failed: %d\n", errno);
    return 0;
  }
  snprintf(buffer, len, "%s_init", name);
  finit = dlsym(handle, buffer);
  free(buffer);
  if (finit == NULL)
  {
    fprintf(stderr, "%s\n", dlerror());
  }

  // deinit
  len = strlen(name) + 7 + 1;
  buffer = malloc(len);
  if (buffer == NULL)
  {
    fprintf(stderr, "malloc() failed: %d\n", errno);
    return 0;
  }
  snprintf(buffer, len, "%s_deinit", name);
  fdeinit = dlsym(handle, buffer);
  free(buffer);

  if (fdeinit == NULL)
  {
    fprintf(stderr, "%s\n", dlerror());
  }

  UDF_INIT *init = malloc(sizeof(UDF_INIT));
  char *message = malloc(MYSQL_ERRMSG_SIZE);

  if (finit != NULL)
  {
    if (finit(init, args, message))
    {
      printf("%s_init failed: %s\n", name, message);
      return 0;
    }
  }

  call_udf_command(name, init, args, ret);

  if (fdeinit != NULL)
  {
    fdeinit(init);
  }

  free(init);
  free(message);

  return 1;
}


int call_udf_command(char *name, UDF_INIT *init, UDF_ARGS *args, int ret)
{
  func_s fs = NULL;
  func_d fd = NULL;
  func_i fi = NULL;

  // function string
  fs = dlsym(handle, name);
  if (fs == NULL)
  {
    fprintf(stderr, "STRING %s\n", dlerror());
  }

  // function double
  fd = dlsym(handle, name);
  if (fd == NULL)
  {
    fprintf(stderr, "DOUBLE %s\n", dlerror());
  }

  // function int
  fi = dlsym(handle, name);
  if (fi == NULL)
  {
    fprintf(stderr, "INT %s\n", dlerror());
  }

  int is_null = 0;
  int error = 0;
  switch ((int)ret)
  {
    case STRING_RESULT:
    {
      char* result = malloc(256);
      char* res = NULL;
      unsigned long res_length;
      res = fs(init, args, result, &res_length, (char*)&is_null, (char*)&error);

      // display result
      if (res != NULL)
      {
        printf("result: %s\n", res);
      }

      free(result);
    }
    break;

    case INT_RESULT:
    {
/*      
      longlong result;
      result = fi(init,args,(char*)&is_null,(char*)&error);
      printf("Row %d: %d\n",currow, result);
*/      
    }
    break;

    case REAL_RESULT:
    {
/*      
      double result;
      result = fd(init,args,(char*)&is_null,(char*)&error);
      printf("Row %d: %f\n",currow, result);
*/      
    }
    break;
    
    case ROW_RESULT:
    break;
    
    case DECIMAL_RESULT:
    break;
  }
  return 1;
}
