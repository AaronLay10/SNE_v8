FROM rust:1.84-bookworm AS build
WORKDIR /workspace
COPY Cargo.toml ./
COPY crates ./crates
COPY services/sentient-core ./services/sentient-core
COPY services/osc-bridge ./services/osc-bridge
COPY services/controller-sim ./services/controller-sim
RUN cargo build -p controller-sim --release

FROM debian:bookworm-slim
RUN useradd -r -u 10003 -g nogroup controllersim
COPY --from=build /workspace/target/release/controller-sim /usr/local/bin/controller-sim
USER controllersim
ENV RUST_LOG=info
ENTRYPOINT ["/usr/local/bin/controller-sim"]
