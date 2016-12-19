#!/bin/bash

pasm -b pru_timer_server.p
gcc pru_ethernet_server_loader.c -o pru_ethernet_server_loader -lprussdrv
