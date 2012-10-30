#!/usr/bin/env bash

## Test basic Input DNS Adapter
## Start OpenDNSSEC and see if zone gets transferred and signed.

if [ -n "$HAVE_MYSQL" ]; then
	ods_setup_conf conf.xml conf-mysql.xml
fi &&

ods_reset_env &&

## Start master name server
ods_ldns_testns 15353 ods.datafile &&

## Start OpenDNSSEC
log_this_timeout ods-control-start 60 ods-control start &&
syslog_waitfor 60 'ods-enforcerd: .*Sleeping for' &&
syslog_waitfor 60 'ods-signerd: .*\[engine\] signer started' &&

## Wait for signed zone file
syslog_waitfor 60 'ods-signerd: .*\[STATS\] ods' &&

## Check signed zone file [when we decide on auditor tool]
test -f "$INSTALL_ROOT/var/opendnssec/signed/ods" &&

ods-signer verbosity 5 &&

## Fake notify
ldns-notify -p 15354 -s 1001 -r 2 -z ods 127.0.0.1 &&

## Request IXFR/UDP
syslog_waitfor 10 'ods-signerd: .*\[xfrd\] zone ods sending udp query id=.* qtype=IXFR to 127.0.0.1' &&
syslog_waitfor 10 'ods-signerd: .*\[xfrd\] zone ods received error code NOTIMPL from 127.0.0.1' &&

## Request AXFR/TCP
syslog_waitfor 10 'ods-signerd: .*\[xfrd\] zone ods request axfr to 127.0.0.1' &&

## Stop
log_this_timeout ods-control-stop 60 ods-control stop &&
syslog_waitfor 60 'ods-enforcerd: .*all done' &&
syslog_waitfor 60 'ods-signerd: .*\[engine\] signer shutdown' &&
ods_ldns_testns_kill &&
return 0

## Test failed. Kill stuff
ods_ldns_testns_kill
ods_kill
return 1
