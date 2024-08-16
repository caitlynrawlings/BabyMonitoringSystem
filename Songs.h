// File: Songs.h
// Authors: Caitlyn Rawlings, Hao Tian
// Date: 8/12/24
// Description: defines 3 songs by frequency and note duration and provides functions
// to play them on a buzzer connected to an arduino nano esp32

#ifndef SONGS_H
#define SONGS_H

#include <Arduino.h>

// Twinkle Twinkle Little Star (Song 1)
int song1Notes[] = {
  261, 261, 392, 392, 440, 440, 392,  // C C G G A A G
  349, 349, 329, 329, 294, 294, 261,  // F F E E D D C
  392, 392, 349, 349, 329, 329, 294,  // G G F F E E D
  392, 392, 349, 349, 329, 329, 294,  // G G F F E E D
  261, 261, 392, 392, 440, 440, 392,  // C C G G A A G
  349, 349, 329, 329, 294, 294, 261   // F F E E D D C
};
int song1NoteDurations[] = {
  4, 4, 4, 4, 4, 4, 2,
  4, 4, 4, 4, 4, 4, 2,
  4, 4, 4, 4, 4, 4, 2,
  4, 4, 4, 4, 4, 4, 2,
  4, 4, 4, 4, 4, 4, 2,
  4, 4, 4, 4, 4, 4, 2
};

// Mary Had a Little Lamb (Song 2)
int song2Notes[] = {
  330, 294, 262, 294, 330, 330, 330,  // E D C D E E E
  294, 294, 294, 330, 392, 392,       // D D D E G G
  330, 294, 262, 294, 330, 330, 330,  // E D C D E E E 
  330, 294, 294, 330, 294, 262        // E D D E D C
};
int song2NoteDurations[] = {
  4, 4, 4, 4, 4, 4, 2,
  4, 4, 2, 4, 4, 2,
  4, 4, 4, 4, 4, 4, 4,
  4, 4, 4, 4, 4, 1
};

// The Wheels on the Bus (Song 3)
int song3Notes[] = {
  262, 349, 349, 349, 349, 440, 262, 440, 349, 0,  // C F F F F A C A F
  392, 392, 392, 330, 294, 262,                    // G G G E D C
  262, 349, 349, 349, 349, 440, 262, 440, 349, 0,  // C F F F F A C A F
  392, 262, 349                                    // G C F
};
int song3NoteDurations[] = {
  4, 4, 8, 8, 4, 4, 4, 4, 4, 4,
  4, 4, 2, 4, 4, 4,
  4, 4, 8, 8, 4, 4, 4, 4, 2, 4,
  2, 2, 1
};

// Name: playNotes
// Description: plays each note for a given song
void playNotes(int buzzerPin, int notes[], int durations[], int length) {
  // cycle through each note of the songs note array
  for (int thisNote = 0; thisNote < length; thisNote++) {
    int noteDuration = 1000 / durations[thisNote];
    tone(buzzerPin, notes[thisNote]);
    vTaskDelay(noteDuration / portTICK_PERIOD_MS);
    noTone(buzzerPin);
    vTaskDelay(200 / portTICK_PERIOD_MS);
  }
}

// Name: playSong
// Description: Plays the song associated with given songNum using provided buzzerPin
void playSong(int buzzerPin, int songNum) {
  switch (songNum) {
    case 1:
      playNotes(buzzerPin, song1Notes, song1NoteDurations, sizeof(song1Notes) / sizeof(int));
      break;
    case 2:
      playNotes(buzzerPin, song2Notes, song2NoteDurations, sizeof(song2Notes) / sizeof(int));
      break;
    case 3:
      playNotes(buzzerPin, song3Notes, song3NoteDurations, sizeof(song3Notes) / sizeof(int));
      break;
    default:
      Serial.println("Invalid selection. Please enter 1, 2, or 3.");
      break;
    }
}

#endif
