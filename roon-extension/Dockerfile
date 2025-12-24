FROM alpine:3.22

# Version and git SHA injected at build time by CI; defaults for local builds
ARG APP_VERSION=dev
ARG GIT_SHA=unknown
ENV APP_VERSION=${APP_VERSION}
ENV GIT_SHA=${GIT_SHA}

RUN apk add --no-cache nodejs-current npm tzdata git

WORKDIR /home/node/app

COPY package.json package-lock.json* ./
RUN npm ci --omit=dev

COPY . .

# ensure runtime perms for the node user
RUN addgroup --gid 1000 node || true \
 && adduser --uid 1000 --ingroup node --shell /bin/sh --home /home/node --disabled-password node || true \
 && install -d -o node -g node /home/node/app/data \
 && chown -R node:node /home/node

USER node
CMD ["npm", "run", "start"]
