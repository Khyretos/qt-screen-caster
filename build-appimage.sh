#!/bin/bash
set -e

# Build the Docker image
docker build -t qt-caster-appimage-builder -f Dockerfile.appimage .

# Create output directory
mkdir -p dist

# Run container and copy the AppImage out
docker run --rm -v $(pwd)/dist:/dist qt-caster-appimage-builder \
    bash -c "cp /app/build/Qt-Caster-*.AppImage /dist/"

echo "✅ AppImage created in ./dist/"