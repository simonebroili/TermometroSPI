#include <Arduino.h>
#include <Adafruit_SSD1327.h>
#include <Fonts/FreeSerif24pt7b.h>
#include <SPI.h>
#include "icons.h"

#define TC77_CS 5
#define DISPLAY_CS 15
#define DISPLAY_DC 16
#define DISPLAY_RESET 4

SPIClass vspi(VSPI);
Adafruit_SSD1327 display(128, 128, &vspi, DISPLAY_DC, DISPLAY_RESET, DISPLAY_CS);

// inizializza il display
void initDisplay() {
    pinMode(DISPLAY_RESET, OUTPUT);
    digitalWrite(DISPLAY_RESET, HIGH);
    delayMicroseconds(500);
    digitalWrite(DISPLAY_RESET, LOW);
    delayMicroseconds(500);
    digitalWrite(DISPLAY_RESET, HIGH);
    delayMicroseconds(500);

    if(!display.begin(0x3D)) {
        Serial.println("Impossibile inizializzare il display");
        while(true);
    }
    Serial.println("Display inizializzato");
}

// inizializza il sensore
void initSensor() {
    pinMode(TC77_CS, OUTPUT);
    digitalWrite(TC77_CS, HIGH);
}

void setup() {
    Serial.begin(115200);

    vspi.begin();
    initDisplay();
    initSensor();

    // imposta tutto il display a nero
    display.clearDisplay();
    display.display();
}

// controlla se la temperatura è valida
// (il terzo bit da destra deve essere impostato)
bool isTemperatureValid(int16_t temperature) {
    return temperature & (1 << 2);
}

bool getTemperature(float *temperature) {
    // inizializza la transazione con il sensore
    vspi.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    // aspetta un attimo
    delay(10);
    // seleziona il sensore con CS
    digitalWrite(TC77_CS, LOW);
    // trasferimento di 16 bit dal sensore
    int16_t value = vspi.transfer16(0);
    // deseleziona il sensore
    digitalWrite(TC77_CS, HIGH);
    // termina la transazione
    vspi.endTransaction();

    // stampa binaria di debug
    Serial.print(value & 0xFFFC, HEX);
    Serial.println();

    // converte e assegna la temperatura al puntatore dato
    *temperature = (float)(value >> 3) * 0.0625f;
    // ritorna come output la validità della misura
    return isTemperatureValid(value);
}

// disegna icona bitmap a coordinate specifiche
void drawIcon(uint8_t startX, uint8_t startY, uint8_t *icon, bool active) {
    for(int y = 0; y < 25; y++) {
        for(int x = 0; x < 25; x++) {
            int bitIndex = y * 32 + x;
            int byteIndex = bitIndex / 8;
            int relativeBitIndex = bitIndex % 8;
            bool texel = icon[byteIndex] & (1 << (7 - relativeBitIndex));
            display.writePixel(startX + x, startY + y, texel ? (active ? 0xFFFF : 2) : 0);
        }
    }
}

// buffer del grafico sullo schermo
int plotSize = 0;
float plot[100] = { 0 };

// sposta il grafico verso sinistra
void shiftPlot() {
    for(int i = 0; i < 99; i++)
        plot[i] = plot[i + 1];
}

// aggiunge valore al grafico da destra
// e sposta il grafico se pieno
void plotValue(float value) {
    plotSize++;
    if(plotSize > 100) {
        plotSize = 100;
        shiftPlot();
    }
    plot[plotSize - 1] = value;
}

// dimensioni del grafico
#define PLOT_WIDTH 100
#define PLOT_HEIGHT 40

// disegna il grafico sullo schermo
void drawPlot() {
    int margin = (128 - PLOT_WIDTH) / 2;
    display.drawRect(margin - 1, 128 - (margin + PLOT_HEIGHT) - 1, PLOT_WIDTH + 2, PLOT_HEIGHT + 2, 0xFFFF);

    for(int i = 0; i < 100; i++) {
        display.drawPixel(margin + i, 128 - margin - plot[i], 0xFFFF);
        if(plot[i] != 0)
            display.drawFastVLine(margin + i, 128 - margin - plot[i] + 1, plot[i], 2);
    }
}

// soglie termostato
#define COOL_H 27
#define COOL_L 25
#define HEAT_H 18
#define HEAT_L 16

bool cooling = false;
bool heating = false;

// in base alla temperatura regola il termostato
void updateAC(float temperature) {
    if(temperature > COOL_H)
        cooling = true;
    if(temperature < COOL_L)
        cooling = false;
    if(temperature > HEAT_H)
        heating = false;
    if(temperature < HEAT_L)
        heating = true;
}

const float a = 0.5; // fattore costante di media
float avgTemp; // media
bool initialized = false;

float textPrintTemperature; // temperatura stampata sullo schermo
int textPrintCounter = 0; // contatore per la temperatura stampata

void loop() {
    float temperature = 0;

    // campiona la temperatura ogni 300ms e se non è valida dà errore
    if(!getTemperature(&temperature))
        Serial.println("Errore nella misurazione della temperatura: dato non valido");
    
    // calcola la media:
    // se non inizializzata viene inizializzata al valore iniziale
    // per evitare che cominci da 0 e abbia una curva di avvio indesiderata
    if(!initialized) {
        avgTemp = temperature;
    } else {
        avgTemp = a * (float)avgTemp + (1.0f - a) * temperature;
    }

    // stampa per teleplot
    Serial.printf(">Temp:%.2f\n", avgTemp);
    // aggiunge valore al grafico per il display
    plotValue(avgTemp);

    // aggiorna la temperatura stampata ogni 10 cicli quindi
    // ogni 3 secondi; se non inizializzata assume il valore iniziale
    // per non avere zero all'accensione
    if(!initialized)
        textPrintTemperature = avgTemp;
    textPrintCounter++;
    if(textPrintCounter == 10) {
        textPrintTemperature = avgTemp;
        textPrintCounter = 0;
    }
    
    // stampa della temperatura sul display
    display.clearDisplay();
    display.setTextSize(3);
    display.setCursor(0, 0);
    display.setTextColor(SSD1327_WHITE);
    display.printf("%.2fC\n", textPrintTemperature);

    updateAC(avgTemp);
    // disegna le due icone del termostato
    drawIcon(34, 35, snowflake, cooling);
    drawIcon(69, 35, sun, heating);
    
    // disegna il grafico sul display
    drawPlot();

    // invia il buffer al display
    display.display();
    
    // alla fine del primo ciclo initialized diventa true
    initialized = true;
    // ciclo ogni 300ms
    delay(300);
}