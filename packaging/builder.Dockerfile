FROM ubuntu:26.04
COPY tools/ci-linux.sh /tmp/ci-linux.sh
RUN BLAZER_INSTALL_DEPS=1 BLAZER_DEPS_ONLY=1 bash /tmp/ci-linux.sh /tmp /tmp/build && rm /tmp/ci-linux.sh
