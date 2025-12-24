FROM alpine:3.22

# Version injected at build time by CI; defaults to 'dev' for local builds
ARG APP_VERSION=dev
ENV APP_VERSION=${APP_VERSION}

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
