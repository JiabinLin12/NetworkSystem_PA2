#!/bin/bash


if [ $# -eq 0 ]
  then
    echo "No arguments supplied"
fi


if [ "$1" -eq "0" ]
  then
    echo "testing wrong directory"
    (echo -en "GET hello HTTP/1.1\r\nHost: localhost\r\nConnection: Keep-alive\r\n\r\n") | nc 127.0.0.1 8888
fi

if [ "$1" -eq "1" ]
  then
    echo "testing close: Connection: close"
    (echo -en "GET /index.html HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n") | nc 127.0.0.1 8888
fi

if [ "$1" -eq "2" ]
  then
    echo "testing close: no Connection: close"
    (echo -en "GET /index.html HTTP/1.1\r\nHost: localhost\r\nnoConnection: close\r\n\r\n") | nc 127.0.0.1 8888
fi

if [ "$1" -eq "3" ]
  then
    echo "testing POST"
    (echo -en "POST /index.html HTTP/1.1\r\nHost: localhost\r\nConnection: Keep-alive\r\n\r\n") | nc 127.0.0.1 8888
fi









