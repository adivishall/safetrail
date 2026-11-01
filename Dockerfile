# SafeTrail server for the hosted public demonstration (Render free web service;
# docs/final/DEPLOYMENT_GUIDE.md). Local use:
#
#   docker build -t safetrail .
#   docker run -p 10000:10000 -e SAFETRAIL_ADMIN_PASSWORD=... -e SAFETRAIL_OPERATOR_PASSWORD=... \
#              -e SAFETRAIL_RESPONDER_PASSWORD=... -e SAFETRAIL_TOURIST_PASSWORD=... safetrail
#
# Stage 1 compiles with g++ on Ubuntu 24.04, the compiler CI gates on. Stage 2
# holds only what the server reads at run time: the stripped binary, the web
# console, the two OpenStreetMap data files and the committed benchmark CSVs --
# no compiler, sources, tests or Git history.

FROM ubuntu:24.04 AS build
RUN apt-get update \
 && apt-get install -y --no-install-recommends g++ make findutils \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN make -j"$(nproc)" build/safetrail_server WERROR=1 && strip build/safetrail_server

FROM ubuntu:24.04
WORKDIR /app
COPY --from=build /src/build/safetrail_server /app/safetrail_server
COPY web/app /app/web/app
COPY data/zones/shillong_osm.geojson /app/data/zones/shillong_osm.geojson
COPY data/osm/roads.txt /app/data/osm/roads.txt
COPY bench/results /app/bench/results
# ubuntu:24.04's unprivileged user (UID 1000).
USER ubuntu
# Render sets PORT (10000 unless changed); the server reads it.
ENV PORT=10000
EXPOSE 10000
# --public: passwords only from the four SAFETRAIL_*_PASSWORD secrets, none
# generated or printed; Secure cookies, HSTS, rate limits; the console says the
# people are simulated and state resets. CF-Connecting-IP is the client address
# Cloudflare writes on every request to a Render web service, honoured only from
# a private (proxy) address. `exec` makes the server PID 1, so Render's SIGTERM
# reaches it. SAFETRAIL_ARGS adds options, e.g. "--scenario entry".
#
# --pbkdf2-iterations 10000: on a 0.1-CPU free instance one 120,000-iteration
# hash takes ~3.7 s, and the server is single-threaded, so every sign-in froze
# it for everyone and a scenario reload (which re-hashes the four accounts) ran
# past the host's 5 s health check. The deployed accounts' passwords are 256-bit
# random secrets (render.yaml generateValue), for which the iteration count adds
# no protection. Local runs keep the 120,000 default.
CMD ["/bin/sh", "-c", "exec /app/safetrail_server --host 0.0.0.0 --public --client-ip-header CF-Connecting-IP --pbkdf2-iterations 10000 --var /tmp/safetrail $SAFETRAIL_ARGS"]
