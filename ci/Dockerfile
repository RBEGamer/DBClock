# FIRMWARE BUILD FOR ALL PICO AND MEGA VARIANTS, THE RESULT IS WRITTEN TO A LOCAL FOLDER:
#   docker build -f ci/Dockerfile --output type=local,dest=dist .
# NO VOLUME MOUNTS NEEDED, SO IT ALSO WORKS WITH DOCKER-IN-DOCKER (GITLAB) AND ACT RUNNERS (GITEA)

FROM python:3.12-slim AS build
# GIT IS NEEDED FOR THE PICO PLATFORM (INSTALLED FROM A GIT REPOSITORY)
RUN apt-get update \
 && apt-get install -y --no-install-recommends git ca-certificates \
 && rm -rf /var/lib/apt/lists/*
RUN pip install --no-cache-dir platformio==6.2.0
WORKDIR /build

# PLATFORMS AND TOOLCHAINS FIRST, THIS LAYER IS CACHED AS LONG AS THE platformio.ini FILES DO NOT CHANGE
COPY src/picowinterface/platformio.ini src/picowinterface/
COPY src/bu190t/platformio.ini src/bu190t/
RUN pio pkg install -d src/picowinterface && pio pkg install -d src/bu190t

COPY ci ci
COPY src/picowinterface src/picowinterface
COPY src/bu190t src/bu190t
ARG FIRMWARE_REF=local
RUN FIRMWARE_REF="$FIRMWARE_REF" sh ci/build_firmware.sh /dist

FROM scratch AS artifacts
COPY --from=build /dist /
