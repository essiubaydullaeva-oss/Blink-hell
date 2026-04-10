# Bas-repo för individuella uppgiften.
# Arduino LED-projekt

## Beskrivning

Projektet implementerar ett system på en Arduino Uno där fyra lysdioder (röd, grön, blå och vit) blinkar synkroniserat. En RGB-LED styrs med hjälp av en rotationsenkoder. Tidsstyrning sker med hjälp av `millis()` utan användning av delay.

## Funktionalitet

* Fyra lysdioder blinkar samtidigt med en bastid på 0,25 sekunder
* En potentiometer på A0 justerar blinktiden i intervallet 0 till 2,55 sekunder extra
* En rotationsenkoder används för att välja RGB-färg i ordningen: röd, grön, blå, vit, av
* Encoder-knappen aktiverar eller avaktiverar vald färg, där aktiv färg blinkar
* Vid aktivt läge kan färgen inte ändras
* En knapp används för att toggla motsvarande lysdiod mellan på och av
* En annan knapp återställer lysdioden till blinkläge

## Kopplingar

* Lysdioder kopplas till pin 10–13 via motstånd till GND
* RGB-LED kopplas till pin 6–8
* Rotationsenkoder kopplas till pin 2, 3 och 4
* Knappar kopplas till pin 5 och 9 med INPUT_PULLUP
* Potentiometer kopplas till A0

## Implementation

* `millis()` används för att hantera all tidsstyrning
* Debounce är implementerad i mjukvara (cirka 25 ms)
* Lysdioderna har tre lägen: blink, konstant på och konstant av

## Resultat

Samtliga krav för G-nivå är uppfyllda:

* Synkron blinkning
* Justerbar blinkhastighet
* RGB-styrning via encoder
* Knappar fungerar enligt specifikation
* Ingen användning av busy-wait

---

  
