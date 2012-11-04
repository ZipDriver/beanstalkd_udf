CFLAGS       = -Wall -g -I. -I/opt/local/include/mysql5/mysql/ -DDEBUG
CC           = gcc

all: beanstalkd_udf.so

test: beanstalkd_udf.so
	$(CC) $(CFLAGS) -rdynamic -o test test.c -lbeanstalk -L.

beanstalkd_udf.so:
	$(CC) $(CFLAGS) -fPIC -shared -o beanstalkd_udf.so beanstalkd_udf.c -lbeanstalk -L.

clean:
	rm -f beanstalkd_udf.so test.o
