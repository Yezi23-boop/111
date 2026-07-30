FROM docker.1ms.run/python:3.12-slim@sha256:423ed6ab25b1921a477529254bfeeabf5855151dc2c3141699a1bfc852199fbf

ENV PYTHONDONTWRITEBYTECODE=1
ENV PYTHONUNBUFFERED=1

WORKDIR /app
COPY requirements.txt .
RUN sed -i 's|deb.debian.org|mirrors.aliyun.com|g; s|security.debian.org|mirrors.aliyun.com|g' \
        /etc/apt/sources.list.d/debian.sources \
    && pip install --no-cache-dir --index-url https://mirrors.aliyun.com/pypi/simple/ -r requirements.txt
RUN groupadd --gid 10002 watch-relay \
    && useradd --uid 10002 --gid 10002 --no-create-home watch-relay
COPY --chown=10002:10002 app.py relay_spool.py ./

EXPOSE 9080
USER 10002:10002
CMD ["uvicorn", "app:app", "--host", "0.0.0.0", "--port", "9080"]
