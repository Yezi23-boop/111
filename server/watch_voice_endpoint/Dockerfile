FROM docker.1ms.run/python:3.12-slim@sha256:423ed6ab25b1921a477529254bfeeabf5855151dc2c3141699a1bfc852199fbf

ENV PYTHONDONTWRITEBYTECODE=1
ENV PYTHONUNBUFFERED=1

WORKDIR /app
COPY requirements.txt .
RUN sed -i 's|deb.debian.org|mirrors.aliyun.com|g; s|security.debian.org|mirrors.aliyun.com|g' \
        /etc/apt/sources.list.d/debian.sources
RUN apt-get update \
    && apt-get install -y --no-install-recommends ffmpeg \
    && rm -rf /var/lib/apt/lists/*
RUN pip install --no-cache-dir \
        --index-url https://mirrors.aliyun.com/pypi/simple/ \
        -r requirements.txt
RUN groupadd --gid 10001 watch-endpoint \
    && useradd --uid 10001 --gid 10001 --no-create-home watch-endpoint \
    && mkdir -p /data \
    && chown 10001:10001 /data
COPY --chown=10001:10001 app.py .
COPY --chown=10001:10001 inbox_repo.py .
COPY --chown=10001:10001 conversation_repo.py .
COPY --chown=10001:10001 session_repo.py .
COPY --chown=10001:10001 relay_transport.py .
COPY --chown=10001:10001 run_events.py .

EXPOSE 8787
USER 10001:10001
CMD ["uvicorn", "app:app", "--host", "0.0.0.0", "--port", "8787"]
