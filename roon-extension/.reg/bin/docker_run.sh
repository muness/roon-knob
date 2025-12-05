#!/bin/sh
docker container inspect roon-extension-knob > /dev/null 2>&1

if [ $? -eq 0 ]; then
    docker stop roon-extension-knob
    docker rm roon-extension-knob
fi

docker run -d --network host --restart unless-stopped --name roon-extension-knob -v /Users/muness1/src/roon-extension-generator/out/knob/.reg/etc/config.json:/home/node/config.json muness/roon-extension-knob:latest-arm64v8
