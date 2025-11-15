# Create container from builder image
docker create --name wasm-proj-build-cont wasm-proj-build && \

# Copy built pkg/ out to the host (alongside src/ and web/)
docker cp wasm-proj-build-cont:/app/pkg ./pkg && \

# Clean up builder container
docker rm wasm-proj-build-cont
