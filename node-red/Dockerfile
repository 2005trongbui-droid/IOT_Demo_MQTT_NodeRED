FROM nodered/node-red:latest

USER root
COPY package.json /data/package.json
RUN npm install --unsafe-perm --prefix /data --omit=dev \
    && chown -R node-red:node-red /data
USER node-red
