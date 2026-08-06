#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Arduino_GFX_Library.h>
#include <esp_arduino_version.h>

struct RedWiFi {
  const char* ssid;
  const char* password;
};

// Completar localmente. No publicar claves en un repositorio público.
RedWiFi redesWiFi[] = {
  {"TDR", "PONER_CLAVE_TDR"},
  {"invitados", "PONER_CLAVE_INVITADOS"}
};

const int CANTIDAD_REDES = sizeof(redesWiFi) / sizeof(redesWiFi[0]);
const unsigned long TIEMPO_ESPERA_WIFI = 12000;

const char* DATOS_URL = "https://elteto.github.io/RioLago/datos.json";
const unsigned long INTERVALO_ACTUALIZACION = 10UL * 60UL * 1000UL;
const unsigned long INTERVALO_REINTENTO = 60UL * 1000UL;
const unsigned long TIEMPO_POR_PANTALLA = 10000;

const uint8_t BRILLO_NORMAL = 100;
const uint8_t BRILLO_ATENUADO = 30;
const unsigned long TIEMPO_PARA_ATENUAR = 60UL * 1000UL;
const uint8_t PASO_BRILLO = 4;
const unsigned long RETARDO_FUNDIDO = 8;
const uint32_t FRECUENCIA_PWM_LCD = 5000;
const uint8_t RESOLUCION_PWM_LCD = 8;
const uint8_t CANAL_PWM_LCD = 0;

#define LCD_MOSI  6
#define LCD_SCLK  7
#define LCD_CS    14
#define LCD_DC    15
#define LCD_RST   21
#define LCD_BL    22
#define BOTON_BOOT 9

#define COLOR_FONDO       0x0000
#define COLOR_BLANCO      0xFFFF
#define COLOR_GRIS        0x8410
#define COLOR_CELESTE     0x07FF
#define COLOR_VERDE       0x07E0
#define COLOR_AMARILLO    0xFFE0
#define COLOR_ROJO        0xF800
#define COLOR_SEPARADOR   0x39E7

Arduino_DataBus* bus = new Arduino_ESP32SPI(
  LCD_DC, LCD_CS, LCD_SCLK, LCD_MOSI, GFX_NOT_DEFINED
);

Arduino_GFX* gfx = new Arduino_ST7789(
  bus, LCD_RST, 1, true, 172, 320, 34, 0, 34, 0
);

struct Estacion {
  String id;
  String nombre;
  String fecha;
  float nivel;
  bool valida;
};

Estacion rio = {"", "RIO", "", 0.0, false};
Estacion lago = {"", "LAGO", "", 0.0, false};

enum PantallaActual {
  PANTALLA_RIO,
  PANTALLA_LAGO
};

PantallaActual pantallaActual = PANTALLA_RIO;

String fechaGeneracion = "";
String ultimaGeneracionRecibida = "";
String ultimoError = "";
String wifiConectado = "";

unsigned long ultimoCambioPantalla = 0;
unsigned long ultimoIntentoDatos = 0;
unsigned long momentoUltimoDatoNuevo = 0;
unsigned long intervaloConsultaActual = INTERVALO_ACTUALIZACION;
unsigned long ultimoReboteBoton = 0;

uint8_t brilloActual = BRILLO_NORMAL;
bool pantallaAtenuada = false;
bool estadoAnteriorBoton = HIGH;

const unsigned long TIEMPO_REBOTE_BOTON = 50;

void escribirBrillo(uint8_t brillo) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(LCD_BL, brillo);
#else
  ledcWrite(CANAL_PWM_LCD, brillo);
#endif
  brilloActual = brillo;
}

void configurarBrillo() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(LCD_BL, FRECUENCIA_PWM_LCD, RESOLUCION_PWM_LCD);
#else
  ledcSetup(CANAL_PWM_LCD, FRECUENCIA_PWM_LCD, RESOLUCION_PWM_LCD);
  ledcAttachPin(LCD_BL, CANAL_PWM_LCD);
#endif
  escribirBrillo(BRILLO_NORMAL);
}

void fundidoBrillo(uint8_t destino) {
  int brillo = brilloActual;
  if (brillo == destino) return;

  if (brillo < destino) {
    while (brillo < destino) {
      brillo += PASO_BRILLO;
      if (brillo > destino) brillo = destino;
      escribirBrillo((uint8_t)brillo);
      delay(RETARDO_FUNDIDO);
    }
  } else {
    while (brillo > destino) {
      brillo -= PASO_BRILLO;
      if (brillo < destino) brillo = destino;
      escribirBrillo((uint8_t)brillo);
      delay(RETARDO_FUNDIDO);
    }
  }
}

void restaurarBrillo() {
  if (pantallaAtenuada || brilloActual != BRILLO_NORMAL) {
    fundidoBrillo(BRILLO_NORMAL);
  }
  pantallaAtenuada = false;
  momentoUltimoDatoNuevo = millis();
}

void verificarAtenuacion() {
  if (!pantallaAtenuada &&
      momentoUltimoDatoNuevo > 0 &&
      millis() - momentoUltimoDatoNuevo >= TIEMPO_PARA_ATENUAR) {
    fundidoBrillo(BRILLO_ATENUADO);
    pantallaAtenuada = true;
    Serial.println("Pantalla atenuada");
  }
}

void verificarBoton() {
  bool estadoActual = digitalRead(BOTON_BOOT);

  if (estadoAnteriorBoton == HIGH &&
      estadoActual == LOW &&
      millis() - ultimoReboteBoton >= TIEMPO_REBOTE_BOTON) {
    ultimoReboteBoton = millis();
    Serial.println("Boton BOOT: restaurando brillo");
    restaurarBrillo();
  }

  estadoAnteriorBoton = estadoActual;
}

String obtenerHora(const String& fecha) {
  if (fecha.length() >= 16) return fecha.substring(11, 16);
  return "--:--";
}

String obtenerFechaCorta(const String& fecha) {
  if (fecha.length() >= 10) {
    return fecha.substring(8, 10) + "/" + fecha.substring(5, 7);
  }
  return "--/--";
}

void textoCentradoEnPagina(const String& texto, int desplazamientoX, int y,
                           uint8_t tamanio, uint16_t color) {
  int16_t x1, y1;
  uint16_t anchoTexto, altoTexto;

  gfx->setTextSize(tamanio);
  gfx->setTextColor(color);
  gfx->getTextBounds(texto.c_str(), 0, y, &x1, &y1, &anchoTexto, &altoTexto);

  int x = desplazamientoX + ((gfx->width() - anchoTexto) / 2);
  gfx->setCursor(x, y);
  gfx->print(texto);
}

void mostrarInicio() {
  gfx->fillScreen(COLOR_FONDO);
  textoCentradoEnPagina("SALTO GRANDE", 0, 35, 3, COLOR_CELESTE);
  textoCentradoEnPagina("Iniciando...", 0, 105, 2, COLOR_BLANCO);
}

void mostrarConectandoWiFi(const String& nombreRed, int numeroRed) {
  gfx->fillScreen(COLOR_FONDO);
  textoCentradoEnPagina("SALTO GRANDE", 0, 15, 2, COLOR_CELESTE);
  textoCentradoEnPagina("Conectando WiFi", 0, 60, 2, COLOR_BLANCO);
  textoCentradoEnPagina(nombreRed, 0, 95, 2, COLOR_AMARILLO);
  textoCentradoEnPagina(
    "Red " + String(numeroRed) + " de " + String(CANTIDAD_REDES),
    0, 135, 1, COLOR_GRIS
  );
}

void mostrarError(const String& mensaje) {
  gfx->fillScreen(COLOR_FONDO);
  textoCentradoEnPagina("ERROR", 0, 20, 3, COLOR_ROJO);
  textoCentradoEnPagina("No se obtuvieron datos", 0, 70, 2, COLOR_BLANCO);

  String mensajeCorto = mensaje;
  if (mensajeCorto.length() > 45) mensajeCorto = mensajeCorto.substring(0, 45);

  textoCentradoEnPagina(mensajeCorto, 0, 115, 1, COLOR_GRIS);
  textoCentradoEnPagina("Reintentando...", 0, 145, 1, COLOR_AMARILLO);
}

void dibujarPantallaEstacion(const Estacion& estacion, int desplazamientoX) {
  const int anchoPantalla = gfx->width();
  const int altoPantalla = gfx->height();

  gfx->fillRect(desplazamientoX, 0, anchoPantalla, altoPantalla, COLOR_FONDO);
  textoCentradoEnPagina("SALTO GRANDE", desplazamientoX, 8, 2, COLOR_CELESTE);
  gfx->drawFastHLine(desplazamientoX + 15, 34, anchoPantalla - 30, COLOR_SEPARADOR);

  gfx->setTextSize(3);
  gfx->setTextColor(estacion.nombre == "LAGO" ? COLOR_VERDE : COLOR_CELESTE);
  gfx->setCursor(desplazamientoX + 25, 61);
  gfx->print(estacion.nombre);

  String nivelTexto = estacion.valida
    ? String(estacion.nivel, 2) + " m"
    : "--.-- m";

  gfx->setTextSize(4);
  gfx->setTextColor(COLOR_BLANCO);

  int16_t x1, y1;
  uint16_t anchoNivel, altoNivel;
  gfx->getTextBounds(nivelTexto.c_str(), 0, 0, &x1, &y1, &anchoNivel, &altoNivel);

  int xNivel = desplazamientoX + anchoPantalla - anchoNivel - 20;
  gfx->setCursor(xNivel, 54);
  gfx->print(nivelTexto);

  gfx->drawFastHLine(desplazamientoX + 15, 112, anchoPantalla - 30, COLOR_SEPARADOR);

  String textoActualizado = "Actualizado";
  gfx->setTextSize(1);
  gfx->setTextColor(COLOR_GRIS);

  uint16_t anchoActualizado, altoActualizado;
  gfx->getTextBounds(
    textoActualizado.c_str(), 0, 0,
    &x1, &y1, &anchoActualizado, &altoActualizado
  );

  gfx->setCursor(
    desplazamientoX + anchoPantalla - anchoActualizado - 22,
    118
  );
  gfx->print(textoActualizado);

  String fechaTexto = obtenerFechaCorta(estacion.fecha) + " " + obtenerHora(estacion.fecha);
  gfx->setTextSize(2);
  gfx->setTextColor(COLOR_BLANCO);

  uint16_t anchoFecha, altoFecha;
  gfx->getTextBounds(fechaTexto.c_str(), 0, 0, &x1, &y1, &anchoFecha, &altoFecha);

  gfx->setCursor(
    desplazamientoX + anchoPantalla - anchoFecha - 22,
    138
  );
  gfx->print(fechaTexto);

  gfx->setTextSize(1);
  gfx->setTextColor(COLOR_GRIS);
  gfx->setCursor(desplazamientoX + 22, 145);
  gfx->print(pantallaActual == PANTALLA_RIO ? "1 / 2" : "2 / 2");
}

void mostrarPantallaActual() {
  gfx->fillScreen(COLOR_FONDO);

  if (pantallaActual == PANTALLA_RIO) {
    dibujarPantallaEstacion(rio, 0);
  } else {
    dibujarPantallaEstacion(lago, 0);
  }

  ultimoCambioPantalla = millis();
}

void cambiarPantalla() {
  if (!rio.valida || !lago.valida) return;

  pantallaActual =
    pantallaActual == PANTALLA_RIO ? PANTALLA_LAGO : PANTALLA_RIO;

  mostrarPantallaActual();
}

void desconectarWiFi() {
  WiFi.disconnect(true);
  delay(400);
}

bool probarRedWiFi(const RedWiFi& red, int numeroRed) {
  mostrarConectandoWiFi(red.ssid, numeroRed);

  Serial.println();
  Serial.print("Probando WiFi: ");
  Serial.println(red.ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(red.ssid, red.password);

  unsigned long inicio = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - inicio < TIEMPO_ESPERA_WIFI) {
    Serial.print(".");
    delay(500);
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    wifiConectado = red.ssid;
    Serial.print("Conectado a: ");
    Serial.println(wifiConectado);
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("RSSI: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    return true;
  }

  Serial.print("No se pudo conectar a: ");
  Serial.println(red.ssid);
  desconectarWiFi();
  return false;
}

bool conectarWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;

  wifiConectado = "";
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);

  for (int i = 0; i < CANTIDAD_REDES; i++) {
    if (probarRedWiFi(redesWiFi[i], i + 1)) return true;
  }

  Serial.println("No se pudo conectar a ninguna red WiFi");
  return false;
}

bool leerEstacion(JsonObject objeto, Estacion& estacion,
                   const char* nombrePredeterminado) {
  if (objeto.isNull()) {
    estacion.valida = false;
    return false;
  }

  estacion.id = objeto["id"] | "";
  estacion.nombre = objeto["nombre"] | nombrePredeterminado;
  estacion.nombre.replace("í", "i");
  estacion.nombre.replace("Í", "I");
  estacion.nombre.toUpperCase();
  estacion.fecha = objeto["fecha"] | "";

  if (objeto["nivel"].isNull()) {
    estacion.nivel = 0.0;
    estacion.valida = false;
    return false;
  }

  estacion.nivel = objeto["nivel"].as<float>();
  estacion.valida = true;
  return true;
}

bool descargarDatos() {
  ultimoError = "";

  if (!conectarWiFi()) {
    ultimoError = "Sin conexion WiFi";
    return false;
  }

  WiFiClientSecure cliente;
  cliente.setInsecure();

  HTTPClient http;

  Serial.println();
  Serial.println("Consultando:");
  Serial.println(DATOS_URL);

  if (!http.begin(cliente, DATOS_URL)) {
    ultimoError = "No se pudo iniciar HTTPS";
    return false;
  }

  http.setConnectTimeout(15000);
  http.setTimeout(15000);
  http.addHeader("Cache-Control", "no-cache");

  int codigoHTTP = http.GET();

  if (codigoHTTP != HTTP_CODE_OK) {
    ultimoError = "Error HTTP " + String(codigoHTTP);
    Serial.println(ultimoError);
    http.end();
    return false;
  }

  String contenido = http.getString();
  http.end();

  Serial.println("JSON recibido:");
  Serial.println(contenido);

  JsonDocument documento;
  DeserializationError error = deserializeJson(documento, contenido);

  if (error) {
    ultimoError = "JSON invalido: " + String(error.c_str());
    Serial.println(ultimoError);
    return false;
  }

  fechaGeneracion = documento["generado"] | "";
  JsonObject datos = documento["datos"].as<JsonObject>();

  if (datos.isNull()) {
    ultimoError = "No existe objeto datos";
    return false;
  }

  bool rioValido = leerEstacion(datos["rio"].as<JsonObject>(), rio, "RIO");
  bool lagoValido = leerEstacion(datos["lago"].as<JsonObject>(), lago, "LAGO");

  if (!rioValido && !lagoValido) {
    ultimoError = "No hay niveles validos";
    return false;
  }

  return true;
}

void actualizarDatos() {
  ultimoIntentoDatos = millis();
  bool resultado = descargarDatos();

  if (resultado) {
    Serial.println("Datos actualizados correctamente");
    intervaloConsultaActual = INTERVALO_ACTUALIZACION;

    if (fechaGeneracion.length() > 0 &&
        fechaGeneracion != ultimaGeneracionRecibida) {
      Serial.print("Nuevo JSON: ");
      Serial.println(fechaGeneracion);
      ultimaGeneracionRecibida = fechaGeneracion;
      restaurarBrillo();
    }

    mostrarPantallaActual();
  } else {
    Serial.print("Error: ");
    Serial.println(ultimoError);
    intervaloConsultaActual = INTERVALO_REINTENTO;

    if (!rio.valida && !lago.valida) {
      mostrarError(ultimoError);
    } else {
      mostrarPantallaActual();
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(BOTON_BOOT, INPUT_PULLUP);
  estadoAnteriorBoton = digitalRead(BOTON_BOOT);

  configurarBrillo();

  if (!gfx->begin(40000000)) {
    Serial.println("Error iniciando la pantalla");
  }

  mostrarInicio();
  momentoUltimoDatoNuevo = millis();
  delay(1000);
  actualizarDatos();
}

void loop() {
  if (millis() - ultimoIntentoDatos >= intervaloConsultaActual) {
    actualizarDatos();
  }

  if (rio.valida && lago.valida &&
      millis() - ultimoCambioPantalla >= TIEMPO_POR_PANTALLA) {
    cambiarPantalla();
  }

  verificarAtenuacion();
  verificarBoton();

  delay(50);
}
