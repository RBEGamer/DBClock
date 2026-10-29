#!/bin/sh
# BUILDS ALL FIRMWARE VARIANTS IN DOCKER AND COPIES THEM TO dist/, USED BY THE GITHUB, GITEA AND GITLAB PIPELINES
# USES docker create + docker cp INSTEAD OF VOLUMES OR BUILDKIT --output, SO IT WORKS ON EVERY DOCKER RUNNER
# USAGE: ci/docker_build.sh [SOURCE REFERENCE FOR BUILDINFO.txt, E.G. main@1a2b3c4]
set -eu
cd "$(dirname "$0")/.."

REF="${1:-local}"
IMAGE="dbclock-firmware-build"

docker build -f ci/Dockerfile --target build --build-arg FIRMWARE_REF="$REF" -t "$IMAGE" .
rm -rf dist
container="$(docker create "$IMAGE")"
docker cp "$container:/dist" dist
docker rm "$container" > /dev/null
ls -l dist
