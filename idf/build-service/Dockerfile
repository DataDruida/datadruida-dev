# Servicio de compilación de DataDruida Developers (para el VPS).
#
#   docker build -f idf/build-service/Dockerfile -t dd-build idf/
#   docker run -d --name dd-build --restart unless-stopped \
#     -e DD_BUILD_TOKEN=... -p 8080:8080 \
#     --cpus 3 --memory 4g --pids-limit 512 dd-build
#
# El contexto de build es la carpeta idf/. La imagen ya trae las dos apps
# compiladas con el app.c vacío ("calientes"): un pedido sólo recompila
# app.c y enlaza.
#
# Aislamiento: corre como usuario sin privilegios, sin secretos en la imagen
# (el único es DD_BUILD_TOKEN, en el entorno) y con topes de CPU/memoria/
# procesos. Para más aislamiento, la compilación podría correr sin red
# (todo lo que necesita ya está en la imagen).
FROM espressif/idf:v5.5.4

RUN useradd -m -u 1000 dd
WORKDIR /srv/idf
COPY --chown=dd:dd . /srv/idf
RUN rm -rf apps/*/build apps/*/sdkconfig apps/*/build-log.txt

USER dd
ENV DD_IDF_ROOT=/srv/idf DD_PORT=8080 PYTHONUNBUFFERED=1

# Precompilar la base de cada placa (baja las dependencias de LVGL una vez).
RUN bash -c '. $IDF_PATH/export.sh >/dev/null && \
    for a in dd-pantalla dd-reles; do (cd apps/$a && idf.py set-target esp32s3 >/dev/null && idf.py build >/dev/null) || exit 1; done'

EXPOSE 8080
CMD ["bash", "-c", ". $IDF_PATH/export.sh >/dev/null && exec python3 build-service/server.py --no-warm"]
