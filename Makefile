CFLAGS       = -Wall -g -I. -I/opt/local/include/mysql5/mysql/ -DDEBUG
CC           = gcc

all: beanstalkd_udf.so

beanstalkd_udf.so:
	$(CC) $(CFLAGS) -Wall -fPIC -shared -o beanstalkd_udf.so beanstalkd_udf.c -lbeanstalk -L.

clean:
	rm -f beanstalkd_udf.so
