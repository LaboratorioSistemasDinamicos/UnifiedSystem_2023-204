/*
  Permite grabar y reproducir usando la placa de Comunicación Inalámbrica
  No usa el RTC para Iniciar o Detener la Grabación - Reproducción
  Programa para Protocolo Real vs Sintetico
  Cambia canto en cada ciclo (Archivo_a_Reproducir)
  Colocar los archivos a reproducir numerados consecutivamente (1...N)
  1->R; 2->S; 3->R; 4->S; .....
  22 de Diciembre de 2023
*/
#include <TMRpcm.h>
#include "RTClib.h"
#include "LowPower.h"
#include "SoftwareSerial.h"
#include "DFRobotDFPlayerMini.h"
//==============================================================================
//========================= CONFIGURACIONES de SOFTWARE ========================
const uint8_t DuracionArchivo = 2;       // Minutos que dura cada archivo de audio a grabar
const uint8_t DuracionProtocolo = 14;    // Minutos de duración del protocolo (14 minutos default)
// Multiplo entero de DuracionArchivo
const uint8_t Descanso = 20;             // Minutos de descanso entre protocolos

const uint8_t ComienzaReproduccion = 2;  // Minutos desde que empezó a grabar para iniciar la reproduccion
const uint8_t SilencioCantos = 15;       // Segundos de silencio entre cantos consecutivos
const uint8_t CantidadCantos = 4;        // Cantidad de reproducciones consecutivas de un canto

const uint16_t FreqMuestreo = 22000;     //Frecuencia de muestreo (hasta 25 kHz)
//==============================================================================
//========================= CONFIGURACIONES de HARDWARE ========================
const uint8_t RstPin = 3;           // Conectado al terminal de RST en Placa WiFi
const uint8_t LED_Work = 4;         // Led Verde
const uint8_t LED_Error = 5;        // Led Rojo
const uint8_t Arduino_SD = 8;       // Terminal de Enable en 74HC4066 para el Arduino (Placa WiFi)
const uint8_t ESP_SD = 9;           // Terminal de Enable en 74HC4066 para el NodeMCU (Placa WiFi)

#define SD_ChipSelectPin 10         // Terminal CS del Arduino con la SD
#define MIC A0                      // Terminal de entrada Analógico 0 (micrófono)
TMRpcm audio;
SoftwareSerial mySoftwareSerial(6, 7); // Terminales del DFPlayer (Rx, Tx)
DFRobotDFPlayerMini myDFPlayer;
//==============================================================================
char filename[13] = "00000000.wav";  // Tamaño máximo del array (12 caracteres, más caracter nulo '\0')

volatile boolean PrimeraVez = true;  // Inicio de ciclo
volatile boolean Siguiente = false;  // Ir al próximo fichero
volatile boolean CortarArc = false;

unsigned long tiempoFichero;
unsigned long SaltoFichero = DuracionArchivo * 60000;

unsigned long inicioReproduccion = ComienzaReproduccion * 60000;
volatile boolean Reproducir = false;
volatile boolean Ciclo = false;

unsigned long Silencio = SilencioCantos * 1000;
unsigned long TiempoUltimoCanto = 0;
uint8_t ContadorCantos = 0;
uint8_t Archivo_a_Reproducir = 1;  // Colocar los archivos numerados consecutivamente

unsigned long TiempoInicio = 0;
unsigned long TiempoTotal = DuracionProtocolo * 60000;
const uint16_t kDescanso = (Descanso * 60) / 8.3;
//==============================================================================
//========================= FUNCIONES GENERALES ================================
RTC_DS3231 rtc;
void dateTime(uint16_t* date, uint16_t* time) { // se puede evitar usar punteros, pero se hace por compatibilidad con RTClib
  DateTime now = rtc.now();
  *date = FAT_DATE(now.year(), now.month(),  now.day());
  *time = FAT_TIME(now.hour(), now.minute(), now.second());
}
//==============================================================================
void getFileName() {
  DateTime now = rtc.now();
  sprintf(filename, "%02d%02d%02d%02d.wav", now.day(), now.hour(), now.minute(), now.second());
}
//==============================================================================
void LedError() {
  digitalWrite(LED_Error, LOW);
  digitalWrite(LED_Work, LOW);
  for (int j = 1; j < 6; j++) {
    digitalWrite(LED_Error, HIGH);
    delay(100);
    digitalWrite(LED_Error, LOW);
    delay(500);
  }
  pinMode(RstPin, OUTPUT);
  digitalWrite(RstPin, LOW);
}
//====================== SET UP DEL MODULO =====================================
void setup() {

  // *** Para la placa de Sistema Unificado comentar esta parte *** //
  pinMode(Arduino_SD, OUTPUT); // SD Arduino
  digitalWrite(Arduino_SD, HIGH);
  delay(1000); // Da tiempo a habilitar Terminal de Enable en 74HC4066
  pinMode(ESP_SD, OUTPUT); //SD ESP
  digitalWrite(ESP_SD, LOW);
  delay(1000);
  //*** Fin de parte a comentar **** //

  rtc.begin();
  pinMode(LED_Work, OUTPUT);
  pinMode(LED_Error, OUTPUT);
  pinMode(MIC, INPUT);

  audio.CSPin = SD_ChipSelectPin;
  if (!SD.begin(SD_ChipSelectPin)) LedError();

  if (rtc.lostPower()) {
    //    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    rtc.adjust(DateTime(2023, 12, 22, 14, 10, 0));
  }

  mySoftwareSerial.begin(9600);
  delay(3000);
  if (!myDFPlayer.begin(mySoftwareSerial)) LedError();
  myDFPlayer.volume(5);                                   // min: 0; max: 30
  delay(1000);
  myDFPlayer.playFolder(15, 1); //Carpeta con nombre 15 y archivo con nombre 1
  delay(60000); // Tiempo de demora para encender la caja y moverse del lugar
}
//====================== LOOP GRABACION REPRODUCCION ===========================
void loop() {
  if (PrimeraVez) {
    PrimeraVez = false;
    DateTime now = rtc.now();
    if (!SD.begin(SD_ChipSelectPin)) LedError(); //redundante, pero es mejor así
    // RST si hay error en la SD, no se ejecuta nada de lo siguiente
    getFileName();
    SdFile::dateTimeCallback(dateTime);
    audio.startRecording(filename, FreqMuestreo, MIC);
    tiempoFichero = millis();
    TiempoInicio = millis();
    digitalWrite(LED_Work, HIGH);
    Ciclo = true;
    CortarArc = true;
  }

  if ((millis() - tiempoFichero > SaltoFichero) && CortarArc) {
    CortarArc = false;
    digitalWrite(LED_Work, LOW);
    audio.stopRecording(filename);
    Siguiente = true;
    Reproducir = true;
  }

  if (Siguiente) {
    Siguiente = false;
    DateTime now = rtc.now();
    if (!SD.begin(SD_ChipSelectPin)) LedError(); //redundante, pero es mejor así
    // RST si hay error en la SD, no se ejecuta nada de lo siguiente
    getFileName();
    SdFile::dateTimeCallback(dateTime);
    audio.startRecording(filename, FreqMuestreo, MIC);
    digitalWrite(LED_Work, HIGH);
    tiempoFichero = millis();
    CortarArc = true;
  }

  if (Ciclo && Reproducir && (millis() - TiempoUltimoCanto > Silencio)) {
    TiempoUltimoCanto = millis();
    myDFPlayer.volume(5);
    myDFPlayer.playFolder(15, 1); //pasarle la variable Archivo_a_Reproducir -> (carpeta,Archivo_a_Reproducir)
    ContadorCantos++;
    if (ContadorCantos >= CantidadCantos) {
      Ciclo = false;
      Reproducir = false;
      ContadorCantos = 0;
    }
  }

  if (millis() - TiempoInicio > TiempoTotal) {
    digitalWrite(LED_Work, LOW);
    audio.stopRecording(filename);
    Siguiente = false;
    CortarArc = false;
    Reproducir = false;
    Ciclo = false;
    for (uint8_t k = 1; k <= kDescanso; k++) { //con k=144 (20*60/8.33) es @ un tiempo de sueño de 20 minutos
      LowPower.idle(SLEEP_8S, ADC_OFF, TIMER2_OFF, TIMER1_OFF, TIMER0_OFF,
                    SPI_OFF, USART0_OFF, TWI_OFF);
    }
    Archivo_a_Reproducir++;
    PrimeraVez = true;
  }
}
