#pragma once

#include "fl/audio/audio.h"
#include "fl/audio/audio_input.h"

namespace myAudio {

    // I2S Configuration
    #define I2S_CLK_PIN 7 // Serial Clock (SCK) (BLUE)
    #define I2S_WS_PIN 8 // Word Select (WS) (GREEN)
    #define I2S_SD_PIN 9  // Serial Data (SD) (YELLOW)

    // INMP441 L/R pin determines output channel:
    //   L/R pin LOW  (or GND) → outputs on Left channel
    //   L/R pin HIGH (or VCC) → outputs on Right channel
    #define I2S_CHANNEL fl::Left 

    // declare fl::audio_input.h objects
    //fl::AudioConfig config = fl::AudioConfig::CreateInmp441(I2S_WS_PIN, I2S_SD_PIN, I2S_CLK_PIN, I2S_CHANNEL);
    
    fl::AudioConfigI2S i2sConfig(I2S_WS_PIN, I2S_SD_PIN, I2S_CLK_PIN, 0, fl::Left, 44100, 16, fl::Philips);
    fl::AudioConfig config(i2sConfig);
        
    fl::shared_ptr<fl::IAudioInput> audioSource;
    bool audioInputInitialized = false;

    //=========================================================================
        
    void initAudioInput() {

        if (audioInputInitialized && audioSource) {
            return;
        }

        fl::string errorMsg;
        myAudio::audioSource = fl::IAudioInput::create(config, &errorMsg);

        Serial.println("Waiting 2000ms for audio device to stdout initialization...");
        delay(2000);

        if (!audioSource) {
            Serial.print("Failed to create audio source: ");
            Serial.println(errorMsg.c_str());
            return;
        }

        // Start audio capture
        Serial.println("Starting audio capture...");
        audioSource->start();

        // Check for start errors
        fl::string startErrorMsg;
        if (audioSource->error(&startErrorMsg)) {
            Serial.print("Audio start error: ");
            Serial.println(startErrorMsg.c_str());
            return;
        }

        Serial.println("Audio capture started!");
        audioInputInitialized = true;

    } // initAudioInput

    void checkAudioInput() {
  
        // Check if audio source is valid
        if (!audioSource) {
            Serial.println("Audio source is null!");
            delay(1000);
            return;
        }

        // Check for audio errors
        fl::string errorMsg;
        if (audioSource->error(&errorMsg)) {
            Serial.print("Audio error: ");
            Serial.println(errorMsg.c_str());
            delay(100);
            return;
        }
    }

} // namespace myAudio