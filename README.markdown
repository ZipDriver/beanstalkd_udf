Beanstalkd MySQL UDF plugin
===========================

Based on [Gearman UDF](https://launchpad.net/gearman-mysql-udf) plugin.

## TODO
- memory leak test
- beanstalk disconnect test
- heavy usage test
- trigger test

## Example

```
DROP FUNCTION IF EXISTS beanstalkd_set_server;
DROP FUNCTION IF EXISTS beanstalkd_do;
```

```
CREATE FUNCTION beanstalkd_set_server returns string soname 'beanstalkd_udf.so';
CREATE FUNCTION beanstalkd_do returns string soname 'beanstalkd_udf.so';
```

```
SELECT beanstalkd_set_server("127.0.0.1");
SELECT beanstalkd_set_server("127.0.0.1", "test");
```

```
SELECT beanstalkd_do("1");
SELECT beanstalkd_do("1", "test");
SELECT beanstalkd_do("1", "test", "127.0.0.1");
```

## Used library
[beanstalkd client](https://github.com/deepfryed/beanstalk-client)