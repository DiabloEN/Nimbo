FROM debian:bookworm-slim@sha256:99c0b25c46d0214d5a2091092e7908907dcaecd6a962cde0ac700acee222b6f3
RUN apt-get update \
    && apt-get install -y --no-install-recommends clamav-daemon clamav-freshclam python3 ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --uid 65532 --user-group --no-create-home --shell /usr/sbin/nologin nimbo-scanner
COPY manager.py healthcheck.py /opt/antivirus/
COPY clamd.conf freshclam.conf /etc/nimbo-antivirus/
# The host service uses 'clamav'; the isolated container shares only a socket
# directory with Nimbo's unprivileged UID, never its data or configuration.
RUN sed -i 's/^DatabaseOwner clamav$/DatabaseOwner nimbo-scanner/' /etc/nimbo-antivirus/freshclam.conf \
    && chmod 0444 /opt/antivirus/*.py /etc/nimbo-antivirus/*.conf
USER 65532:65532
ENTRYPOINT ["python3", "/opt/antivirus/manager.py"]
