
/*
 * Lectura del sensor EZO EC (Atlas Scientific) en Arduino Uno
 * Aplicación: agua destilada de laboratorio
 * Usa SoftwareSerial para liberar el puerto USB (Serial) para depuración.
 * Configura TDS, Salinidad (SAL) y Gravedad Específica (SG) una sola vez.
 */
#include <Arduino.h>
#include <SoftwareSerial.h>
// static const float TDS_FACTOR = 0.5;
// Conversión EC (µS/cm) → TDS/Salinidad (ppm/ppt)
static const float TDS_PPM_FACTOR = 0.5;    // usa 0.7f si prefieres escala 700
static const float SAL_PPM_FACTOR = 0.0005; // salinidad (ppm) ≈ EC * factor
SoftwareSerial ezoSerial(3, 2);  // D3=RX (desde EZO TX), D2=TX (hacia EZO RX)
bool outputsConfigured = false;
unsigned long lastReadMs = 0;
bool streamingEnabled = false;
unsigned long readPeriodMs = 1000;
bool printRaw = false;

static void ezoSend(const char* cmd) {
  ezoSerial.print(cmd);
  ezoSerial.print('\r');  // Atlas EZO requiere '\r' como terminador
}

// Lee hasta '\r' y filtra caracteres no ASCII imprimibles
static String ezoReadLine(uint16_t timeoutMs = 500) {
  String line;
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    while (ezoSerial.available()) {
      char c = (char)ezoSerial.read();
      if (c == '\r') {
        return line;
      }
      if (c >= 32 && c <= 126) {
        line += c;
      }
    }
  }
  return line;  // puede ser vacío si expiró
}

// Envía un comando al EZO y etiqueta claramente la petición y su respuesta
static String ezoQuery(const char* cmd, uint16_t timeoutMs = 1000) {
  Serial.print("[EZO] Enviando: ");
  Serial.println(cmd);
  ezoSend(cmd);
  String resp = ezoReadLine(timeoutMs);
  Serial.print("[EZO] Respuesta: ");
  if (resp.length() == 0) {
    Serial.println("(timeout)");
  } else {
    Serial.println(resp);
  }
  return resp;
}

static void configureOutputsOnce() {
  if (outputsConfigured) return;

  // Limpia cualquier basura en el buffer del EZO
  while (ezoSerial.available()) (void)ezoSerial.read();

  // Habilita etiquetas EC/TDS/SAL/SG
  // Configuración única del EZO: temperatura y salidas
  // ezoSend("T,25.0");   // compensación a 25 °C; ajusta si tu muestra difiere
  // ezoReadLine();       // lee '*OK'
  
  // ezoSend("O,EC,1");   // muestra EC
  // ezoReadLine();
  
  // ezoSend("O,TDS,0");  // desactiva TDS (lo calculamos nosotros)
  // ezoReadLine();
  
  // ezoSend("O,SAL,0");  // muestra SAL (si la necesitas)
  // ezoReadLine();
  
  // ezoSend("O,SG,0");   // muestra SG
  // ezoReadLine();
  ezoQuery("O,EC,1", 1200);
  ezoQuery("O,TDS,0", 1200);
  ezoQuery("O,SAL,0", 1200);
  ezoQuery("O,SG,0", 1200);

  // estado de error 
  // ezoSend("O,?");
  // Serial.print("Salida O,? -> "); Serial.println(ezoReadLine());

  outputsConfigured = true;
  Serial.println("[Config] Salidas configuradas: EC ON, TDS/SAL/SG OFF.");
}

// helper para parsear líneas del EZO con o sin etiquetas (EC,TDS,SAL,SG)
bool parseEcLine(const String &line, float &ec, float &tds, float &sal, float &sg) {
    String s = line; s.trim();
    if (s.length() == 0) return false;
    if (s.startsWith("*OK")) return false; // confirmación de configuración, no es lectura

    // Formato con etiquetas: "EC,<v>,TDS,<v>,SAL,<v>,SG,<v>"
    if (s.indexOf("EC") != -1 || s.indexOf("TDS") != -1 || s.indexOf("SAL") != -1 || s.indexOf("SG") != -1) {
        float lec=0, ltds=0, lsal=0, lsg=0; int found=0; int pos=0; bool ecFound=false;
        while (pos < s.length()) {
            int c = s.indexOf(',', pos);
            String tok = (c == -1) ? s.substring(pos) : s.substring(pos, c); tok.trim();
            pos = (c == -1) ? s.length() : c + 1;
            if (tok == "EC" || tok == "TDS" || tok == "SAL" || tok == "SG") {
                int c2 = s.indexOf(',', pos);
                String val = (c2 == -1) ? s.substring(pos) : s.substring(pos, c2); val.trim();
                float f = val.toFloat();
                if (tok == "EC") { lec = f; ecFound = true; }
                else if (tok == "TDS") ltds = f;
                else if (tok == "SAL") lsal = f;
                else lsg = f;
                found++;
                pos = (c2 == -1) ? s.length() : c2 + 1;
            }
        }
        if (ecFound) { ec=lec; tds=ltds; sal=lsal; sg=lsg; return true; }
        return false;
    }

    // Formato sin etiquetas: "ec,tds,sal,sg"
    int p1 = s.indexOf(',');
    if (p1 == -1) { // solo EC
        ec = s.toFloat();
        tds = 0; sal = 0; sg = 0;
        return true;
    }
    int p2 = s.indexOf(',', p1+1); if (p2 == -1) return false;
    int p3 = s.indexOf(',', p2+1); if (p3 == -1) return false;
    String a = s.substring(0, p1); String b = s.substring(p1+1, p2);
    String c = s.substring(p2+1, p3); String d = s.substring(p3+1);
    ec = a.toFloat(); tds = b.toFloat(); sal = c.toFloat(); sg = d.toFloat();
    return true;
}

void setup() {
  Serial.begin(115200);
  ezoSerial.begin(9600);
  delay(200);

  configureOutputsOnce();  // solo una vez
  Serial.println(F("[Ayuda] Comandos disponibles (terminar con Enter):"));
  Serial.println(F("  help                 → muestra esta ayuda"));
  Serial.println(F("  r                    → lectura inmediata (EZO R)"));
  Serial.println(F("  t <C>                → compensación de temperatura, ej: t 25.0"));
  Serial.println(F("  t ?                  → consulta compensación de temperatura actual"));
  Serial.println(F("  cal clear            → borra calibración"));
  Serial.println(F("  cal dry              → calibración en seco (EC sensor)"));
  Serial.println(F("  cal low <µS/cm>      → punto bajo, ej: cal low 84.0"));
  Serial.println(F("  cal mid <µS/cm>      → punto medio, ej: cal mid 1413"));
  Serial.println(F("  cal high <µS/cm>     → punto alto, ej: cal high 12880"));
  Serial.println(F("  cal <µS/cm>          → atajo: usa punto medio (mid)"));
  Serial.println(F("  k <0.1|1.0|10.0>    → fija constante de celda de la sonda"));
  Serial.println(F("  k ?                  → consulta constante de celda actual"));
  Serial.println(F("  cal ?                → consulta estado de calibración"));
  Serial.println(F("  o ec on|off          → salida etiquetada EC"));
  Serial.println(F("  o tds on|off         → salida etiquetada TDS"));
  Serial.println(F("  o sal on|off         → salida etiquetada SAL"));
  Serial.println(F("  o sg on|off          → salida etiquetada SG"));
  Serial.println(F("  stream on|off        → habilita/deshabilita lecturas periódicas"));
  Serial.println(F("  period <ms>          → fija periodo de lectura (por defecto 1000)"));
  Serial.println(F("  raw on|off           → muestra también la respuesta cruda del EZO"));
  Serial.println(F("  o ?                  → consulta estado de salidas"));
  Serial.println(F("  i                    → información del dispositivo"));
  Serial.println(F("  status               → estado del dispositivo"));
  Serial.println(F("  led on|off           → LED del módulo"));
  Serial.println(F("  factory              → restaurar fábrica (borra calib.)"));
  Serial.println(F("  sleep                → bajo consumo (despierta con reset)"));
  Serial.println(F("  c on|off             → modo continuo del EZO (no recomendado con stream)"));
}

void loop() {
  // Si aún hay comandos de configuración en curso, evita reenviarlos
  if (!outputsConfigured) {
    configureOutputsOnce();
  }

  // Procesa líneas desde la terminal serial USB
  if (Serial.available()) {
    static String cli;
    static unsigned long cliLastByteMs = 0;
    bool terminated = false;
    while (Serial.available()) {
      char c = (char)Serial.read();
      if (c == '\n' || c == '\r') { // acepta LF o CR como fin de línea
        terminated = true;
        break;
      }
      cli += c;
      cliLastByteMs = millis();
    }
    // procesa si hubo terminador o si no llegan más bytes en ~300 ms (para "Line ending: None")
    bool idleTimeout = (cli.length() > 0) && (millis() - cliLastByteMs > 300);
    if (cli.length() > 0 && (terminated || idleTimeout)) {
      String cmd = cli; cmd.trim(); cli = "";
      int sp1 = cmd.indexOf(' ');
      String a = (sp1 == -1) ? cmd : cmd.substring(0, sp1);
      String rest = (sp1 == -1) ? "" : cmd.substring(sp1+1);
      a.toLowerCase(); rest.trim();

      if (a == "help") {
        Serial.println(F("[Ayuda] Comandos: help, r, t <C>, cal clear|dry|low|mid|high <v>, cal ?, o <canal> on|off"));
      } else if (a == "r") {
        (void)ezoQuery("R", 1000);
      } else if (a == "t") {
        if (rest == "?" || rest == "?") {
          (void)ezoQuery("T,?", 1200);
        } else {
          float tc = rest.toFloat();
          char buf[24]; dtostrf(tc, 0, 2, buf);
          String q = String("T,") + String(buf);
          (void)ezoQuery(q.c_str(), 1200);
        }
      } else if (a == "cal") {
        String b; String val; int sp2 = rest.indexOf(' ');
        b = (sp2 == -1) ? rest : rest.substring(0, sp2);
        val = (sp2 == -1) ? "" : rest.substring(sp2+1);
        b.toLowerCase(); val.trim();

        if (b == "clear") {
          (void)ezoQuery("Cal,clear", 1500);
        } else if (b == "dry") {
          (void)ezoQuery("Cal,dry", 2000);
        } else if (b == "?") {
          (void)ezoQuery("Cal,?", 1500);
        } else if (b == "low" || b == "mid" || b == "high") {
          float f = val.toFloat();
          if (val.length() == 0) {
            Serial.println(F("[Cal] Falta valor en µS/cm, ej: cal low 84.0"));
          } else {
            char buf[24]; dtostrf(f, 0, 2, buf);
            String q = String("Cal,") + b + String(",") + String(buf);
            (void)ezoQuery(q.c_str(), 4000);
          }
        } else if (b.length() > 0 && (isDigit(b[0]) || b[0] == '-' || b[0] == '+')) {
          // Atajo: "cal <valor>" → selecciona low/mid/high según magnitud (µS/cm)
          float f = b.toFloat();
          const float v = f;
          const char* mode = (v <= 200.0f ? "low" : (v <= 3000.0f ? "mid" : "high"));
          char buf[24]; dtostrf(v, 0, 2, buf);
          String q = String("Cal,") + String(mode) + String(",") + String(buf);
          (void)ezoQuery(q.c_str(), 4000);
        } else {
          Serial.println(F("[Cal] Subcomando desconocido. Usa: clear|dry|low|mid|high|? o 'cal <µS/cm>'"));
        }
      } else if (a == "o") {
        String ch; String onoff; int sp2 = rest.indexOf(' ');
        ch = (sp2 == -1) ? rest : rest.substring(0, sp2);
        onoff = (sp2 == -1) ? "" : rest.substring(sp2+1);
        ch.toLowerCase(); onoff.toLowerCase(); onoff.trim();
        if (ch == "?") { (void)ezoQuery("O,?", 1500); }
        else {
          int en = (onoff == "on") ? 1 : (onoff == "off" ? 0 : -1);
          if (en == -1) {
            Serial.println(F("[O] Usa on|off. Ej: o ec on"));
          } else if (ch == "ec" || ch == "tds" || ch == "sal" || ch == "sg") {
            String q = String("O,") + ch + String(",") + String(en);
            (void)ezoQuery(q.c_str(), 1500);
          } else {
            Serial.println(F("[O] Canal desconocido. Usa: ec|tds|sal|sg"));
          }
        }
      } else if (a == "stream") {
        rest.toLowerCase();
        if (rest == "on") { streamingEnabled = true; Serial.println(F("[Stream] ON")); }
        else if (rest == "off") { streamingEnabled = false; Serial.println(F("[Stream] OFF")); }
        else Serial.println(F("[Stream] Usa: stream on|off"));
      } else if (a == "period") {
        unsigned long ms = (unsigned long)rest.toInt();
        if (ms == 0) Serial.println(F("[Period] Debe ser > 0 ms"));
        else { readPeriodMs = ms; Serial.print(F("[Period] ")); Serial.print(readPeriodMs); Serial.println(F(" ms")); }
      } else if (a == "raw") {
        rest.toLowerCase();
        if (rest == "on") { printRaw = true; Serial.println(F("[Raw] ON")); }
        else if (rest == "off") { printRaw = false; Serial.println(F("[Raw] OFF")); }
        else Serial.println(F("[Raw] Usa: raw on|off"));
      } else if (a == "i") {
        (void)ezoQuery("I", 1500);
      } else if (a == "status") {
        (void)ezoQuery("Status", 1500);
      } else if (a == "led") {
        rest.toLowerCase();
        if (rest == "on") (void)ezoQuery("L,1", 1200);
        else if (rest == "off") (void)ezoQuery("L,0", 1200);
        else Serial.println(F("[LED] Usa: led on|off"));
      } else if (a == "factory") {
        (void)ezoQuery("Factory", 2000);
      } else if (a == "sleep") {
        (void)ezoQuery("Sleep", 1200);
      } else if (a == "c") {
        rest.toLowerCase();
        if (rest == "on") (void)ezoQuery("C,1", 1200);
        else if (rest == "off") (void)ezoQuery("C,0", 1200);
        else Serial.println(F("[C] Usa: c on|off"));
      } else if (a == "k") {
        if (rest == "?" || rest == "?") {
          (void)ezoQuery("K,?", 1200);
        } else {
          // acepta 0.1, 1.0, 10.0
          float kv = rest.toFloat();
          if (kv == 0) { Serial.println(F("[K] Usa 0.1 | 1.0 | 10.0")); }
          else {
            char buf[16]; dtostrf(kv, 0, 1, buf);
            String q = String("K,") + String(buf);
            (void)ezoQuery(q.c_str(), 1500);
          }
        }
      } else {
        Serial.print(F("[CLI] Comando desconocido: ")); Serial.println(cmd);
      }
    }
  }

  // Lecturas periódicas
  unsigned long now = millis();
  if (streamingEnabled && (now - lastReadMs >= readPeriodMs)) {
    lastReadMs = now;

    // Pedir lectura y mostrar claramente comando y respuesta
    String line = ezoQuery("R", 900);  // EZO suele responder en < 1s
    if (printRaw) { Serial.print(F("[EZO] Raw: ")); Serial.println(line); }
    // Ahora: parsea y muestra sólo si es lectura válida
    float ec = 0, tds = 0, sal = 0, sg = 0;
    if (parseEcLine(line, ec, tds, sal, sg)) {
        // EC debe estar en µS/cm; si está en mS/cm multiplícalo por 1000 antes
        const float tds_calc = ec * TDS_PPM_FACTOR;     // ppm
        const float sal_ppm  = ec * SAL_PPM_FACTOR;     // ppm (≈ TDS)
        // const float sal_ppt  = sal_ppm / 1000.0f;    // ppt, por si quieres también

        Serial.println(F("[Lectura] Interpretación:"));
        Serial.print(F("  EC: "));   Serial.print(ec, 6);       Serial.println(F(" µS/cm"));
        Serial.print(F("  TDS≈: ")); Serial.print(tds_calc, 1); Serial.println(F(" ppm"));
        Serial.print(F("  SAL≈: ")); Serial.print(sal_ppm, 1);  Serial.println(F(" ppm"));
        if (line.indexOf("SG") != -1) {
          Serial.print(F("  SG: ")); Serial.println(sg, 6);
        } else {
          Serial.println(F("  SG: n/a"));
        }
    } else if (line.startsWith("*OK")) {
        // Serial.println("Lectura: *OK (comando de configuración aceptado)");
    } else if (line.length() == 0) {
        Serial.println(F("[Lectura] (timeout)"));
    } else {
        Serial.print(F("[Lectura] Respuesta no interpretable: "));
        Serial.println(line);
    }
  }
}



