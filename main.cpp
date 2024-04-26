#include "mbed.h"
#include "stdio.h"
#include "string.h"
#include "SDFileSystem.h"
#include "wave_player.h"
#include "PinDetect.h"
#include <cstring>
#include "uLCD_4DGL.h"

SDFileSystem sd(p5, p6, p7, p8, "sd"); // mosi, miso, sclk, cs
AnalogOut Speaker(p18);
wave_player waver(&Speaker);
PinDetect play(p15), pause(p16), forward(p19), back(p20);
uLCD_4DGL uLCD(p9, p10, p11);  // tx, rx, reset

// Playlist and navigation
char songName[4][50] = {"intro", "song"};
int songCount = 2;  // Total number of songs
volatile int currentIndex = 0;  // Current song index
FILE *wave_file;
bool skip = 0;

void playSong(const char* song) {
    char filePath[100] = "/sd/";
    strcat(filePath, song);
    strcat(filePath, ".wav");

    if (wave_file != NULL) {

        // if (fclose(wave_file) != 0) {
        //     printf("Error closing file.\n");
        // }
        fclose(wave_file);
        wave_file = NULL;
        printf("Previous file closed\n");
        
        
    }
    printf("Trying to open: %s\n", filePath);
    wave_file = fopen(filePath, "r");
    if (wave_file == NULL) {
        printf("Failed to open file: %s\n", filePath);
        return;
    }
    fseek(wave_file, 0, SEEK_SET);
    
    printf("File opened successfully: %s\n", filePath);
    waver.play(wave_file);
    fclose(wave_file);

    // #if defined(__MICROLIB) && defined(__ARMCC_VERSION) // with microlib and ARM compiler
    //         free(wave_file);
    // #endif
}

void songDisplay() {
    uLCD.locate(6, 11);
    uLCD.color(0xFFFFFF);
    uLCD.printf("%s", songName[currentIndex]);
}

void playIcon() {
    uLCD.cls();
    uLCD.triangle(60, 56, 60, 72, 70, 64, 0xFFFFFF);
    songDisplay();
}

void pauseIcon() {
    uLCD.cls();
    uLCD.filled_rectangle(59, 72, 61, 56, 0xFFFFFF);   
    uLCD.filled_rectangle(67, 72, 69, 56, 0xFFFFFF);
    songDisplay();  
}

int loadIndex() {
    FILE* file = fopen("/sd/index.txt", "r");
    if (file) {
        int index;
        fscanf(file, "%d", &index);
        printf("index: %i", index);
        fclose(file);
        return index;
    }
    return 0; // Default to the first song if file not found
}

// New function to save the current index to a file
void saveIndex(int index) {
    FILE* file = fopen("/sd/index.txt", "w");
    if (file) {
        fprintf(file, "%d", index);
        fclose(file);
    }
}

void pausePress() {
    printf("pause pressed\n");
    pauseIcon();
    waver.pause();
}

void playPress() {
    printf("play pressed\n");
    playIcon();
    waver.resume();
}
  
void skipForward() {
    currentIndex = (currentIndex + 1) % songCount;
    saveIndex(currentIndex);  // Save the new index before resetting
    printf("index: %d\n", currentIndex);
    NVIC_SystemReset();  // Reset will be called, so playSong won't execute afterwards
}

void skipBackward() {
    currentIndex = (currentIndex - 1 + songCount) % songCount;
    saveIndex(currentIndex);  // Save the new index before resetting
    printf("index: %d\n", currentIndex);
    NVIC_SystemReset();  // Reset will be called, so playSong won't execute afterwards
}


int main() {
     currentIndex = loadIndex();
    pause.attach_deasserted(&pausePress);
    pause.mode(PullUp);
    pause.setSampleFrequency();

    play.attach_deasserted(&playPress);
    play.mode(PullUp);
    play.setSampleFrequency();

  
    forward.attach_deasserted(&skipForward);
    forward.mode(PullUp);
    forward.setSampleFrequency();

    back.mode(PullUp);
    back.attach_deasserted(&skipBackward);
    back.setSampleFrequency();

    songDisplay();
    playIcon();
    playSong(songName[currentIndex]);
    

}