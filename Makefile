CFLAGS       = -Wall -g -I. -I/opt/local/include/mysql5/mysql/ -I/usr/local/include/mysql -DDEBUG
CC           = gcc

all: beanstalkd_udf.so

test: clean beanstalkd_udf.so
	$(CC) $(CFLAGS) -rdynamic -o test test.c -lbeanstalk -L.

install:
	rm -f /opt/local/lib/mysql5/mysql/plugin/beanstalkd_udf.so
	cp ./beanstalkd_udf.so /opt/local/lib/mysql5/mysql/plugin/

beanstalkd_udf.so:
	$(CC) $(CFLAGS) -fPIC -shared -o beanstalkd_udf.so beanstalkd_udf.c -lbeanstalk -L.

clean:
	rm -f beanstalkd_udf.so test.o
