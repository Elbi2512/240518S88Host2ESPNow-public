# 240518 S88-host naar ESP-NOW

## Doel

ESP32-host die S88-ingangen uitleest, veranderingen debounced en de actuele moduletoestand via ESP-NOW naar een ontvanger verstuurt.

## Werking volgens `src/main.cpp`

- De actieve configuratie is Pforzheim: 13 modules met offset 14. Andere aantallen en offsets zijn als compile-time configuraties aanwezig.
- Leest de S88-bus in en accepteert een nieuwe toestand pas nadat die 20 ms stabiel is.
- Stuurt een update bij gewijzigde bits en periodiek een heartbeat.
- De ESP-NOW-payload bevat `ID`, `OFFSET`, `AANTAL` en twee bytes per module. De bits worden MSB-first en per module ingepakt.
- Een OLED toont de omgeving en recente veranderingen.

## Hardware en bouwen

- PlatformIO-target: `esp32doit-devkit-v1`
- S88-reset: GPIO 2
- S88-clock: GPIO 23
- S88-load: GPIO 18
- S88-data-ingang: GPIO 19
- OLED I2C: SDA GPIO 22, SCL GPIO 21
- Seriële debug: 115200 baud

Bouwen vanuit deze projectmap:

```sh
pio run
```

## Koppeling

De huidige bytepayload is bedoeld voor `251111HostESPNowWEB`. Zorg dat beide apparaten hetzelfde ESP-NOW-kanaal gebruiken en dat het ingestelde bestemmings-MAC-adres bij de ontvanger hoort. Controleer de compile-time `doelomgeving`, `BOARDID`, `OFFSET1` en `NUMBER_OF_S88MODULES` voordat je een andere baansectie configureert.
