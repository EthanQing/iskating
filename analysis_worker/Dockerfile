FROM nvcr.io/nvidia/deepstream:9.0-triton-multiarch

RUN apt-get update \
    && apt-get install -y --no-install-recommends cmake g++ pkg-config libgstreamer1.0-dev zlib1g-dev python3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/iskating/src
COPY analysis_worker/custom_parser custom_parser
COPY analysis_worker/native native

RUN cmake -S custom_parser -B build/parser -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build/parser --parallel \
    && cmake -S native -B build/native -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build/native --parallel \
    && install -Dm755 build/parser/libiskating_yolo26x_parser.so /opt/iskating/lib/libiskating_yolo26x_parser.so \
    && install -Dm755 build/native/iskating-fullrate /opt/iskating/bin/iskating-fullrate

COPY analysis_worker /opt/iskating/python/analysis_worker
COPY analysis_worker/config /opt/iskating/config
ENV PYTHONPATH=/opt/iskating/python \
    ISKATING_DEEPSTREAM_BINARY=/opt/iskating/bin/iskating-fullrate
WORKDIR /opt/iskating
ENTRYPOINT ["python3", "-m", "analysis_worker.worker"]
