# Build with the APL repository as context:
# docker build -f apl_studio/Dockerfile -t apl-studio .
FROM node:22-bookworm-slim AS web-build
WORKDIR /opt/apl/apl_studio
COPY apl_studio/package.json apl_studio/package-lock.json ./
COPY apl_studio/apps/server/package.json ./apps/server/
COPY apl_studio/apps/web/package.json ./apps/web/
COPY apl_studio/packages/api/package.json ./packages/api/
COPY apl_studio/packages/build/package.json ./packages/build/
COPY apl_studio/packages/definition/package.json ./packages/definition/
COPY apl_studio/packages/model/package.json ./packages/model/
COPY apl_studio/packages/product/package.json ./packages/product/
COPY apl_studio/packages/templates/package.json ./packages/templates/
# tsx is also the server's runtime loader; it must remain installed.
RUN npm ci --include=dev
COPY apl_studio/ ./
RUN npm run build:web

FROM node:22-bookworm-slim AS runtime
RUN apt-get update \
    && apt-get install -y --no-install-recommends gcc libc6-dev ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /opt/apl/apl_studio
COPY --from=web-build /opt/apl/apl_studio /opt/apl/apl_studio
COPY apl_lib/ /opt/apl/apl_lib/
COPY apl_sdk_template/ /opt/apl/apl_sdk_template/
RUN mkdir -p /var/lib/apl-studio && chown node:node /var/lib/apl-studio
ENV NODE_ENV=production \
    APL_STUDIO_HOST=0.0.0.0 \
    APL_STUDIO_PORT=9190 \
    APL_STUDIO_DATA=/var/lib/apl-studio \
    APL_STUDIO_CC=/usr/bin/gcc
USER node
EXPOSE 9190
VOLUME ["/var/lib/apl-studio"]
HEALTHCHECK --interval=30s --timeout=5s --start-period=20s --retries=3 \
    CMD node -e "fetch('http://127.0.0.1:'+process.env.APL_STUDIO_PORT+'/health').then(r=>{if(!r.ok)process.exit(1)}).catch(()=>process.exit(1))"
CMD ["node", "--import", "tsx", "apps/server/src/main.ts"]
