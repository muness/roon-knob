#!/bin/sh
docker container inspect roon-extension-knob > /dev/null 2>&1

if [ $? -eq 0 ]; then
    docker stop roon-extension-knob
    docker rm roon-extension-knob
fi

docker run -d --network host --restart unless-stopped --name roon-extension-knob -v roon-knob-data:/home/node/app/data muness/roon-extension-knob:latest
