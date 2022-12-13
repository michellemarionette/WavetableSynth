#include "Headers.h"


SynthesiserSound::SynthesiserSound() {}
SynthesiserSound::~SynthesiserSound() {}

bool SynthesiserSound::appliesToNote (int midiNoteNumber) { return true; }
bool SynthesiserSound::appliesToChannel (int midiChannel) { return true; }


SynthesiserVoice::SynthesiserVoice()
{
    juce::ADSR::Parameters parameters;
    
    parameters.attack = 0.1f;
    parameters.decay = 1.f;
    parameters.sustain = 0.5f;
    parameters.release = 0.1f;

    m_adsr.setParameters (parameters);
}

SynthesiserVoice::~SynthesiserVoice() {}

void SynthesiserVoice::setPitchBend (int pitchWheelValue)
{
    if (pitchWheelValue > 8192)
    {
        m_pitchBend = float(pitchWheelValue - 8192) / (16383 - 8192);
    }
    
    else
    {
        m_pitchBend = float(8192 - pitchWheelValue) / -8192;
    }
}

void SynthesiserVoice::setEnvelope (float attack, float decay, float sustain, float release)
{
    auto parameters = m_adsr.getParameters();
    
    parameters.attack = attack;
    parameters.decay = decay;
    parameters.sustain = sustain;
    parameters.release = release;
    
    m_adsr.setParameters (parameters);
}

void SynthesiserVoice::setWavetablePosition (float position)
{
    m_voice.setFrame (position);
}

bool SynthesiserVoice::canPlaySound (juce::SynthesiserSound* sound) {
    return dynamic_cast<SynthesiserSound*> (sound) != nullptr;
}

void SynthesiserVoice::startNote (int midiNoteNumber, float velocity, juce::SynthesiserSound* sound, int currentPitchWheelPosition)
{
    if (!m_playing)
    {
        m_playing = true;
        m_frequency = juce::MidiMessage::getMidiNoteInHertz (midiNoteNumber);
        
        m_voice.setIndex (0.f);
        m_voice.setFrequency (m_frequency);
        
        m_adsr.reset();
        m_adsr.noteOn();
        
        m_velocity = velocity;
        setPitchBend (currentPitchWheelPosition);
    }
}

void SynthesiserVoice::stopNote (float velocity, bool allowTailOff)
{
    m_adsr.noteOff();
}

void SynthesiserVoice::pitchWheelMoved (int newPitchWheelValue)
{
    setPitchBend (newPitchWheelValue);
}

void SynthesiserVoice::controllerMoved (int controllerNumber, int newControllerValue) {}

void SynthesiserVoice::setCurrentPlaybackSampleRate (double newRate)
{
    if (newRate != 0)
    {
        m_voice.setSampleRate (newRate);
        m_adsr.setSampleRate (newRate);
        
        m_filters.clear();
        
        for (int filter = 0; filter < Variables::numResampleFilters; ++filter)
        {
            m_filters.add (new juce::IIRFilter);
            m_filters[filter]->setCoefficients (juce::IIRCoefficients::makeLowPass(newRate * static_cast<double> (Variables::resampleCoefficient), 20000.0));
        }
    }
}

void SynthesiserVoice::modulateFrequency (float modulationAmount)
{
    m_voice.setFrequency (m_frequency * modulationAmount);
}

void SynthesiserVoice::pitchBendModulation()
{
    if (m_pitchBend > 0.f)
    {
        modulateFrequency (m_pitchBend + 1.f);
    }
    
    else
    {
        modulateFrequency (1.f / -(m_pitchBend - 1.f));
    }
}

void SynthesiserVoice::renderNextBlock (juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples)
{
    if (m_playing)
    {
        juce::AudioBuffer<float> blockUpsample (1, numSamples * static_cast<float> (Variables::resampleCoefficient));
        juce::AudioBuffer<float> block (1, numSamples);
        blockUpsample.clear();
        block.clear();
        
        auto* blockUpsampleData = blockUpsample.getWritePointer (0);
        auto* blockData = block.getWritePointer (0);
        
        
        if (m_pitchBend != 0.f || m_frequency != m_voice.getFrequency())
        {
            pitchBendModulation();
        }
        
        // Increase sample rate to remove aliasing.
        m_voice.setSampleRate (m_voice.getSampleRate() * static_cast<float> (Variables::resampleCoefficient));
        
        for (int sample = 0; sample < blockUpsample.getNumSamples(); ++sample)
        {
            blockUpsampleData[sample] = m_voice.processSample();
        }
        
        // Filter upsampled signal.
        for (int filter = 0; filter < Variables::numResampleFilters; ++filter)
        {
            m_filters[filter]->processSamples (blockUpsampleData, blockUpsample.getNumSamples());
        }
        
        // Downsample signal and apply envelope.
        for (int sample = 0; sample < block.getNumSamples(); ++sample)
        {
            blockData[sample] = blockUpsampleData[sample * Variables::resampleCoefficient] * m_adsr.getNextSample();
        }
        
        // Reset to original sample rate.
        m_voice.setSampleRate (m_voice.getSampleRate() / static_cast<float> (Variables::resampleCoefficient));
        
        if (m_velocity != 1.f)
        {
            block.applyGain (m_velocity);
        }
        
        for (int channel = 0; channel < outputBuffer.getNumChannels(); ++channel)
        {
            outputBuffer.addFrom (channel, startSample, block, 0, 0, numSamples);
        }
        
        if (!m_adsr.isActive())
        {
            clearCurrentNote();
            m_playing = false;
        }
    }
}
