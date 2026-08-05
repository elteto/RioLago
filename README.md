# RioLago

Publica en `datos.json` el último nivel disponible de dos estaciones hidrométricas de CTM Salto Grande:

- Río: `A50012EE`
- Lago: `A5004C40`

Los datos se actualizan automáticamente cada 10 minutos mediante GitHub Actions.

## Archivos

- `actualizar.py`: consulta el WebService SOAP y genera `datos.json`.
- `datos.json`: archivo que consumirá el ESP32.
- `index.html`: página simple para verificar los valores.
- `.github/workflows/actualizar-datos.yml`: actualización automática.

## GitHub Pages

Configurar GitHub Pages para publicar desde:

- Rama: `main`
- Carpeta: `/ (root)`

La URL esperada del JSON es:

`https://elteto.github.io/RioLago/datos.json`

## Ejecución manual

Desde la pestaña **Actions**, abrir **Actualizar datos RioLago** y ejecutar **Run workflow**.
