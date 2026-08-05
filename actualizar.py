from __future__ import annotations

import json
import sys
from datetime import datetime, timedelta
from pathlib import Path
from typing import Any
from zoneinfo import ZoneInfo

from zeep import Client
from zeep.helpers import serialize_object
from zeep.transports import Transport
from requests import Session

WSDL_URL = "https://www.saltogrande.org/ws.php?wsdl"
ZONA_HORARIA = ZoneInfo("America/Argentina/Buenos_Aires")
ARCHIVO_SALIDA = Path("datos.json")

ESTACIONES = {
    "rio": {
        "id": "A50012EE",
        "nombre": "Río",
    },
    "lago": {
        "id": "A5004C40",
        "nombre": "Lago",
    },
}


def crear_cliente() -> Client:
    session = Session()
    session.headers.update({"User-Agent": "RioLago-GitHubActions/1.0"})
    transport = Transport(session=session, timeout=30, operation_timeout=30)
    return Client(wsdl=WSDL_URL, transport=transport)


def normalizar_lista(respuesta: Any) -> list[dict[str, Any]]:
    datos = serialize_object(respuesta)

    if datos is None:
        return []

    if isinstance(datos, list):
        return [x for x in datos if isinstance(x, dict)]

    if isinstance(datos, dict):
        for valor in datos.values():
            if isinstance(valor, list):
                return [x for x in valor if isinstance(x, dict)]

        return [datos]

    return []


def convertir_fecha(valor: Any) -> datetime | None:
    if valor is None:
        return None

    texto = str(valor).strip()
    formatos = (
        "%Y-%m-%d %H:%M:%S",
        "%Y-%m-%dT%H:%M:%S",
        "%Y-%m-%d %H:%M:%S.%f",
        "%Y-%m-%dT%H:%M:%S.%f",
    )

    for formato in formatos:
        try:
            return datetime.strptime(texto, formato).replace(tzinfo=ZONA_HORARIA)
        except ValueError:
            continue

    try:
        fecha = datetime.fromisoformat(texto)
        return fecha if fecha.tzinfo else fecha.replace(tzinfo=ZONA_HORARIA)
    except ValueError:
        return None


def obtener_ultimo_nivel(cliente: Client, id_estacion: str) -> dict[str, Any]:
    ahora = datetime.now(ZONA_HORARIA)
    desde = ahora - timedelta(hours=24)

    respuesta = cliente.service.DatosHidrometeorologicos(
        idEstacion=id_estacion,
        fechaDesde=desde.strftime("%Y-%m-%d %H:%M:%S"),
        fechaHasta=ahora.strftime("%Y-%m-%d %H:%M:%S"),
    )

    registros = normalizar_lista(respuesta)
    candidatos: list[tuple[datetime, float]] = []

    for registro in registros:
        fecha = convertir_fecha(registro.get("Fecha"))
        nivel = registro.get("H")

        if fecha is None or nivel is None:
            continue

        try:
            candidatos.append((fecha, float(nivel)))
        except (TypeError, ValueError):
            continue

    if not candidatos:
        raise RuntimeError(f"No se encontró un nivel H válido para {id_estacion}")

    fecha, nivel = max(candidatos, key=lambda item: item[0])

    return {
        "fecha": fecha.strftime("%Y-%m-%d %H:%M:%S"),
        "nivel": round(nivel, 3),
    }


def main() -> int:
    cliente = crear_cliente()
    generado = datetime.now(ZONA_HORARIA)

    salida: dict[str, Any] = {
        "generado": generado.strftime("%Y-%m-%d %H:%M:%S"),
        "datos": {},
    }

    for clave, estacion in ESTACIONES.items():
        ultimo = obtener_ultimo_nivel(cliente, estacion["id"])
        salida["datos"][clave] = {
            "id": estacion["id"],
            "nombre": estacion["nombre"],
            **ultimo,
        }

    ARCHIVO_SALIDA.write_text(
        json.dumps(salida, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )

    print(json.dumps(salida, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"Error al actualizar datos: {exc}", file=sys.stderr)
        raise
