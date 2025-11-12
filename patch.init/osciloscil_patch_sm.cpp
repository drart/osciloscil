#include "daisysp.h"
#include "daisy_patch_sm.h"

using namespace daisy;
using namespace daisysp;
using namespace patch_sm;

DaisyPatchSM patch;

Oscillator oscLeft;
Oscillator oscRight;

uint leftWaveIndex = 0;
uint rightWaveIndex = 0;

uint waveStartIndex = 0;
uint waveWindowLength = 6;

uint numberOfWaves = 6;
uint waves[] = {0, 2, 1, 0, 5, 0}; // WAVE_SIN, WAVE_SAW, WAVE_TRI, WAVE_SIN, WAVE_POLYBLEP_TRI, WAVE_SIN

float baseFreq = 100.0f;
float phaseOffsetLeft = 0.0f;
float phaseOffsetRight = 0.0f;

void UpdateControls()
{
    patch.ProcessAllControls();
    
    // CV1: Base frequency (20Hz - 2000Hz)
    baseFreq = fmap(patch.GetAdcValue(CV_1), 20.0f, 2000.0f, Mapping::LOG);
    
    // CV5: Phase offset left (0-1)
    phaseOffsetLeft = patch.GetAdcValue(CV_5);
    
    // CV6: Phase offset right (0-1)
    phaseOffsetRight = patch.GetAdcValue(CV_6);
}

uint WrapIndex(int index, uint start, uint length)
{
    // Wrap index within the wave window
    int wrappedIndex = index % length;
    if (wrappedIndex < 0) wrappedIndex += length;
    
    uint actualIndex = (start + wrappedIndex) % numberOfWaves;
    return actualIndex;
}

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    UpdateControls();
    
    for(size_t i = 0; i < size; i++)
    {
        // Apply phase offsets
        oscLeft.PhaseAdd(phaseOffsetLeft);
        oscRight.PhaseAdd(phaseOffsetRight);
        
        // Process oscillators
        float sigLeft = oscLeft.Process();
        float sigRight = oscRight.Process();
        
        // Output to left and right channels
        out[0][i] = sigLeft;
        out[1][i] = sigRight;
        
        // Set gate outputs based on oscillator rising edges
        dsy_gpio_write(&patch.gate_out_1, oscLeft.IsRising());
        dsy_gpio_write(&patch.gate_out_2, oscRight.IsRising());
        
        // Check for End of Cycle on left oscillator
        if(oscLeft.IsEOC())
        {
            // Sample CV3 and CV4 at EOC
            waveStartIndex = static_cast<uint>(patch.GetAdcValue(CV_3) * (numberOfWaves - 1));
            waveWindowLength = static_cast<uint>(patch.GetAdcValue(CV_4) * numberOfWaves) + 1; // +1 to ensure at least 1 wave
            
            // Move left index forward
            leftWaveIndex++;
            
            // Wrap within window
            uint wrappedLeft = WrapIndex(leftWaveIndex, waveStartIndex, waveWindowLength);
            oscLeft.SetWaveform(waves[wrappedLeft]);
        }
        
        // Check for End of Cycle on right oscillator
        if(oscRight.IsEOC())
        {
            // Sample CV2, CV3 and CV4 at EOC
            float waveOffset = patch.GetAdcValue(CV_2) * (waveWindowLength - 1);
            waveStartIndex = static_cast<uint>(patch.GetAdcValue(CV_3) * (numberOfWaves - 1));
            waveWindowLength = static_cast<uint>(patch.GetAdcValue(CV_4) * numberOfWaves) + 1;
            
            // Apply wave offset and move right index backward
            int offsetIndex = static_cast<int>(waveOffset);
            rightWaveIndex--;
            
            // Wrap within window (moving backward)
            int rightIndexWithOffset = rightWaveIndex - offsetIndex;
            uint wrappedRight = WrapIndex(rightIndexWithOffset, waveStartIndex, waveWindowLength);
            oscRight.SetWaveform(waves[wrappedRight]);
        }
        
        // Update frequencies
        oscLeft.SetFreq(baseFreq);
        oscRight.SetFreq(baseFreq);
    }
}

int main(void)
{
    // Initialize Patch SM
    patch.Init();
    
    // Initialize oscillators
    float sample_rate = patch.AudioSampleRate();
    oscLeft.Init(sample_rate);
    oscRight.Init(sample_rate);
    
    oscLeft.SetFreq(baseFreq);
    oscRight.SetFreq(baseFreq);
    
    oscLeft.SetWaveform(waves[0]);
    oscRight.SetWaveform(waves[0]);
    
    // Start audio
    patch.StartAudio(AudioCallback);
    
    while(1)
    {
        // Main loop can be used for other tasks if needed
    }
}