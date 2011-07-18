#!/bin/sh

set +e

systemctl --system daemon-reload
systemctl start pcmkc-cped.service

#./basic.sh rhel61 x86_64
./basic.sh F15 x86_64
#./basic.sh F14 x86_64

systemctl stop pcmkc-cped.service
systemctl stop pcmkc-vmlauncher.service
systemctl stop pcmkc-qpidd.service

