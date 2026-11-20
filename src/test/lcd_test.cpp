#include <Arduino.h>
#include <SPI.h>
#include <Ucglib.h>
#include "Config.h"

Ucglib_ST7735_18x128x160_HWSPI display(Config::LCD_DC, Config::LCD_CS, Config::LCD_RESET);

void setup() {
  Serial.begin(115200);
  SPI.begin(Config::LCD_SCK, -1, Config::LCD_MOSI, Config::LCD_CS);
  display.begin(UCG_FONT_MODE_TRANSPARENT);
  display.setRotate90();
  display.setFont(ucg_font_6x10_tr);
  Serial.println("LCD test: SCK18 MOSI23 RES17 DC16 CS4");

  const uint8_t colors[][3] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}};
  const char *names[] = {"RED", "GREEN", "BLUE"};
  for (uint8_t i = 0; i < 3; ++i) {
    display.setColor(colors[i][0], colors[i][1], colors[i][2]);
    display.drawBox(0, 0, display.getWidth(), display.getHeight());
    display.setColor(255, 255, 255);
    display.setPrintPos(8, 20);
    display.print(names[i]);
    Serial.println(names[i]);
    delay(1000);
  }
}

void loop() {
  static uint32_t count = 0;
  display.clearScreen();
  display.setColor(255, 255, 255);
  display.drawFrame(0, 0, display.getWidth(), display.getHeight());
  display.setPrintPos(6, 14);
  display.print("TOP LEFT   LCD TEST");
  display.setPrintPos(6, 34);
  display.print("ST7735 160 x 128");
  display.setPrintPos(6, 50);
  display.print("SCK18 MOSI23");
  display.setPrintPos(6, 66);
  display.print("RES17 DC16 CS4");
  display.setColor(255, 0, 0);
  display.drawBox(6, 76, 46, 15);
  display.setColor(0, 255, 0);
  display.drawBox(57, 76, 46, 15);
  display.setColor(0, 0, 255);
  display.drawBox(108, 76, 46, 15);
  display.setColor(255, 255, 0);
  display.setPrintPos(6, 108);
  display.print("Counter: ");
  display.print(count);
  Serial.printf("LCD test counter: %lu\n", static_cast<unsigned long>(count++));
  delay(1000);
}
