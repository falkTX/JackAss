/*
 * JackAss VST plugin
 * Copyright (C) 2013 Filipe Coelho <falktx@falktx.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any purpose with
 * or without fee is hereby granted, provided that the above copyright notice and this
 * permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD
 * TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <list>
#include <pthread.h>

#ifndef __cdecl
# define __cdecl
#endif

#include "jackbridge/JackBridge.cpp"

#include "public.sdk/source/vst2.x/audioeffect.cpp"
#include "public.sdk/source/vst2.x/audioeffectx.cpp"
#include "public.sdk/source/vst2.x/vstplugmain.cpp"

// -------------------------------------------------
// uncomment to enable midi-programs

//#define USE_PROGRAMS

// -------------------------------------------------
// Parameters

static const unsigned char kParamMap[] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
    0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F,
    0x50, 0x51, 0x52, 0x53, 0x54, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F
};

static const int kParamVolume  = 5;
static const int kParamBalance = 6;
static const int kParamPan     = 8;

static const int kParamCount   = sizeof(kParamMap);
#ifdef USE_PROGRAMS
static const int kProgramCount = 128;
#else
static const int kProgramCount = 0;
#endif

// -------------------------------------------------
// Data limits

static const int kMaxMidiEvents   = 512;
static const int kProgramNameSize = 32;

// -------------------------------------------------
// Midi data

struct midi_data_t {
    unsigned char data[4];
    unsigned char size;
    VstInt32 time;

    midi_data_t()
        : size(0),
          time(0)
    {
        std::memset(data, 0, 4*sizeof(char));
    }
};

// -------------------------------------------------
// Global JACK client

static jack_client_t* gJackClient     = nullptr;
static volatile bool  gNeedMidiResend = false;

// -------------------------------------------------
// single JackAss instance, containing 1 MIDI port

class JackAssInstance
{
public:
    JackAssInstance(jack_port_t* const port)
        : fPort(port)
    {
        pthread_mutex_init(&fMutex, nullptr);
    }

    ~JackAssInstance()
    {
        pthread_mutex_lock(&fMutex);
        pthread_mutex_unlock(&fMutex);
        pthread_mutex_destroy(&fMutex);

        if (fPort != nullptr)
        {
            if (gJackClient != nullptr)
                jackbridge_port_unregister(gJackClient, fPort);

            fPort = nullptr;
        }
    }

    void putEvent(const unsigned char data[4], const unsigned char size, const VstInt32 time)
    {
        pthread_mutex_lock(&fMutex);

        for (int i=0; i < kMaxMidiEvents; ++i)
        {
            if (fData[i].data[0] != 0)
                continue;

            fData[i].data[0] = data[0];
            fData[i].data[1] = data[1];
            fData[i].data[2] = data[2];
            fData[i].data[3] = data[3];
            fData[i].size    = size;
            fData[i].time    = time;
            break;
        }

        pthread_mutex_unlock(&fMutex);
    }

    void putEvent(const unsigned char data1, const unsigned char data2, const unsigned char data3, const unsigned char size, const VstInt32 time)
    {
        const unsigned char data[4] = { data1, data2, data3, 0 };
        putEvent(data, size, time);
    }

    void jprocess(const jack_nframes_t nframes)
    {
        void* const portBuffer(jackbridge_port_get_buffer(fPort, nframes));

        if (portBuffer == nullptr)
            return;

        jackbridge_midi_clear_buffer(portBuffer);

        pthread_mutex_lock(&fMutex);

        for (int i=0; i < kMaxMidiEvents; ++i)
        {
            if (fData[i].data[0] == 0)
                break;

            if (unsigned char* const buffer = jackbridge_midi_event_reserve(portBuffer, fData[i].time, fData[i].size))
                std::memcpy(buffer, fData[i].data, fData[i].size);

            fData[i].data[0] = 0; // set as invalid
        }

        pthread_mutex_unlock(&fMutex);
    }

private:
    jack_port_t*    fPort;
    midi_data_t     fData[kMaxMidiEvents];
    pthread_mutex_t fMutex;
};

// -------------------------------------------------
// static list of JackAss instances

static std::list<JackAssInstance*> gInstances;

// -------------------------------------------------
// JACK calls

static int jprocess_callback(const jack_nframes_t nframes, void*)
{
    for (std::list<JackAssInstance*>::iterator it = gInstances.begin(); it != gInstances.end(); ++it)
        (*it)->jprocess(nframes);
    return 0;
}

static void jconnect_callback(const jack_port_id_t a, const jack_port_id_t b, const int connect_, void*)
{
    if (connect_ == 0)
        return;

    if (jackbridge_port_is_mine(gJackClient, jackbridge_port_by_id(gJackClient, a)) ||
        jackbridge_port_is_mine(gJackClient, jackbridge_port_by_id(gJackClient, b)))
        gNeedMidiResend = true;
}

// -------------------------------------------------
// JackAss plugin

class JackAss : public AudioEffectX
{
public:
    JackAss(audioMasterCallback audioMaster)
        : AudioEffectX(audioMaster, kProgramCount, kParamCount),
          fInstance(nullptr)
    {
        for (int i=0; i < kParamCount; ++i)
            fParamBuffers[i] = 0.0f;

        fParamBuffers[kParamVolume]  = 100.0f/127.0f;
        fParamBuffers[kParamBalance] = 0.5f;
        fParamBuffers[kParamPan]     = 0.5f;

#ifdef USE_PROGRAMS
        for (int i=0; i < kProgramCount; ++i)
        {
            fProgramNames[i] = new char[kProgramNameSize+1];
            std::snprintf(fProgramNames[i], kProgramNameSize, "Program #%i", i+1);
            fProgramNames[i][kProgramNameSize] = '\0';
        }
#endif

        if (audioMaster == nullptr)
            return;

#ifdef JACKASS_SYNTH
        isSynth();
        setNumInputs(0);
        setNumOutputs(2);
        setUniqueID(CCONST('J', 'A', 's', 's'));
#else
        setNumInputs(2);
        setNumOutputs(2);
        setUniqueID(CCONST('J', 'A', 's', 'x'));
#endif

        char strBuf[0xff+1];

        // Register global JACK client if needed
        if (gJackClient == nullptr)
        {
            std::memset(strBuf, 0, sizeof(char)*0xff+1);

            if (getHostProductString(strBuf) && strBuf[0] != '\0')
            {
                char tmp[std::strlen(strBuf)+1];
                std::strcpy(tmp, strBuf);
#ifdef JACKASS_SYNTH
                std::strcpy(strBuf, "JackAss-");
#else
                std::strcpy(strBuf, "JackAssFX-");
#endif
                std::strncat(strBuf, tmp, 0xff-11);
                strBuf[0xff] = '\0';
            }
            else
            {
#ifdef JACKASS_SYNTH
                std::strcpy(strBuf, "JackAss");
#else
                std::strcpy(strBuf, "JackAssFX");
#endif
            }

            gJackClient = jackbridge_client_open(strBuf, JackNullOption, nullptr);

            if (gJackClient == nullptr)
                return;

            jackbridge_set_port_connect_callback(gJackClient, jconnect_callback, nullptr);
            jackbridge_set_process_callback(gJackClient, jprocess_callback, nullptr);
            jackbridge_activate(gJackClient);
        }

        // Create instance + jack-port for this plugin
#if defined(__MINGW64__)
        std::sprintf(strBuf, "midi-out_%02llu", gInstances.size()+1);
#elif defined(__MINGW32__)
        std::sprintf(strBuf, "midi-out_%02u", gInstances.size()+1);
#elif (defined(__LP64__) || defined(__APPLE__))
        std::sprintf(strBuf, "midi-out_%02lu", gInstances.size()+1);
#else
        std::sprintf(strBuf, "midi-out_%02u", gInstances.size()+1);
#endif

        if (jack_port_t* const jport = jackbridge_port_register(gJackClient, strBuf, JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0))
        {
            fInstance = new JackAssInstance(jport);
            gInstances.push_back(fInstance);
        }
    }

    ~JackAss() override
    {
#ifdef USE_PROGRAMS
        for (int i=0; i < kProgramCount; ++i)
        {
            if (fProgramNames[i] != nullptr)
            {
                delete[] fProgramNames[i];
                fProgramNames[i] = nullptr;
            }
        }
#endif

        if (fInstance != nullptr)
        {
            gInstances.remove(fInstance);
            delete fInstance;
            fInstance = nullptr;
        }

        // Close global JACK client if needed
        if (gJackClient != nullptr && gInstances.size() == 0)
        {
            jackbridge_deactivate(gJackClient);
            jackbridge_client_close(gJackClient);
            gJackClient = nullptr;
        }
    }

    // ---------------------------------------------

    void processReplacing(float** inputs, float** const outputs, const VstInt32 sampleFrames) override
    {
#ifdef JACKASS_SYNTH
        // Silent output
        std::memset(outputs[0], 0, sizeof(float)*sampleFrames);
        std::memset(outputs[1], 0, sizeof(float)*sampleFrames);
#else
        // Bypass
        std::memcpy(outputs[0], inputs[0], sizeof(float)*sampleFrames);
        std::memcpy(outputs[1], inputs[1], sizeof(float)*sampleFrames);
#endif

        if (gNeedMidiResend && fInstance != nullptr)
        {
            for (int i=0; i < kParamCount; ++i)
                fInstance->putEvent(0xB0, kParamMap[i], int(fParamBuffers[i]*127.0f), 3, 0);

            gNeedMidiResend = false;
        }

#ifdef JACKASS_SYNTH
        return; // unused
        (void)inputs;
#endif
    }

#ifdef JACKASS_SYNTH
    VstInt32 processEvents(VstEvents* const events) override
    {
        if (fInstance == nullptr || events == nullptr)
            return 0; // FIXME?

        for (VstInt32 i=0; i < events->numEvents; ++i)
        {
            if (events->events[i] == nullptr)
                break;
            if (events->events[i]->type != kVstMidiType)
                continue;

            VstMidiEvent* const midiEvent((VstMidiEvent*)events->events[i]);
            fInstance->putEvent(midiEvent->midiData[0], midiEvent->midiData[1], midiEvent->midiData[2], 3, midiEvent->deltaFrames);
        }

        return 0;
    }
#endif

    // ---------------------------------------------

#ifdef USE_PROGRAMS
    void setProgram(const VstInt32 program) override
    {
        if (curProgram < 0 || curProgram >= kProgramCount)
            return;

        if (fInstance != nullptr)
        {
            // bank select
            fInstance->putEvent(0xB0, 0x00, 0, 3, 0);
            // program select
            fInstance->putEvent(0xC0, program, 0, 2, 0);
        }

        AudioEffectX::setProgram(program);
    }

    void setProgramName(char* const name) override
    {
        if (curProgram < 0 || curProgram >= kProgramCount)
            return;

        std::strncpy(fProgramNames[curProgram], name, kProgramNameSize);
    }

    void getProgramName(char* const name) override
    {
        if (curProgram < 0 || curProgram >= kProgramCount)
            return AudioEffectX::getProgramName(name); // TODO: REMOVE

        std::strncpy(name, fProgramNames[curProgram], kVstMaxProgNameLen);
    }
#endif

    // ---------------------------------------------

    void setParameter(const VstInt32 index, const float value) override
    {
        if (index < 0 || index >= kParamCount)
            return;

        if (fParamBuffers[index] != value)
        {
            fParamBuffers[index] = value;

            if (fInstance != nullptr)
                fInstance->putEvent(0xB0, kParamMap[index], int(value*127.0f), 3, 0);
        }
    }

    float getParameter(const VstInt32 index) override
    {
        if (index < 0 || index >= kParamCount)
            return 0.0f;

        return fParamBuffers[index];
    }

    void getParameterLabel(const VstInt32 index, char* const label) override
    {
        // TODO
        AudioEffectX::getParameterLabel(index, label);
    }

    void getParameterDisplay(const VstInt32 index, char* const text) override
    {
        if (index < 0 || index >= kParamCount)
            return AudioEffectX::getParameterDisplay(index, text); // TODO: REMOVE

        char strBuf[kVstMaxParamStrLen+1];
        std::snprintf(strBuf, kVstMaxParamStrLen, "%i", int(fParamBuffers[index]*127.0f));
        strBuf[kVstMaxParamStrLen] = '\0';

        std::strncpy(text, strBuf, kVstMaxParamStrLen);
    }

    void getParameterName(const VstInt32 index, char* const text) override
    {
        if (index < 0 || index >= kParamCount)
            return AudioEffectX::getParameterName(index, text); // TODO: REMOVE

        static const int kMaxParamLen = 18+1; //28;

        switch (kParamMap[index])
        {
        case 0x01:
            std::strncpy(text, "0x01 Modulation", kMaxParamLen);
            break;
        case 0x02:
            std::strncpy(text, "0x02 Breath", kMaxParamLen);
            break;
        case 0x03:
            std::strncpy(text, "0x03 (Undefined)", kMaxParamLen);
            break;
        case 0x04:
            std::strncpy(text, "0x04 Foot", kMaxParamLen);
            break;
        case 0x05:
            std::strncpy(text, "0x05 Portamento", kMaxParamLen);
            break;
        case 0x07:
            std::strncpy(text, "0x07 Volume", kMaxParamLen);
            break;
        case 0x08:
            std::strncpy(text, "0x08 Balance", kMaxParamLen);
            break;
        case 0x09:
            std::strncpy(text, "0x09 (Undefined)", kMaxParamLen);
            break;
        case 0x0A:
            std::strncpy(text, "0x0A Pan", kMaxParamLen);
            break;
        case 0x0B:
            std::strncpy(text, "0x0B Expression", kMaxParamLen);
            break;
        case 0x0C:
            std::strncpy(text, "0x0C FX Control 1", kMaxParamLen);
            break;
        case 0x0D:
            std::strncpy(text, "0x0D FX Control 2", kMaxParamLen);
            break;
        case 0x0E:
            std::strncpy(text, "0x0E (Undefined)", kMaxParamLen);
            break;
        case 0x0F:
            std::strncpy(text, "0x0F (Undefined)", kMaxParamLen);
            break;
        case 0x10:
            std::strncpy(text, "0x10 Gen Purpose 1", kMaxParamLen);
            break;
        case 0x11:
            std::strncpy(text, "0x11 Gen Purpose 2", kMaxParamLen);
            break;
        case 0x12:
            std::strncpy(text, "0x12 Gen Purpose 3", kMaxParamLen);
            break;
        case 0x13:
            std::strncpy(text, "0x13 Gen Purpose 4", kMaxParamLen);
            break;
        case 0x14:
            std::strncpy(text, "0x14 (Undefined)", kMaxParamLen);
            break;
        case 0x15:
            std::strncpy(text, "0x15 (Undefined)", kMaxParamLen);
            break;
        case 0x16:
            std::strncpy(text, "0x16 (Undefined)", kMaxParamLen);
            break;
        case 0x17:
            std::strncpy(text, "0x17 (Undefined)", kMaxParamLen);
            break;
        case 0x18:
            std::strncpy(text, "0x18 (Undefined)", kMaxParamLen);
            break;
        case 0x19:
            std::strncpy(text, "0x19 (Undefined)", kMaxParamLen);
            break;
        case 0x1A:
            std::strncpy(text, "0x1A (Undefined)", kMaxParamLen);
            break;
        case 0x1B:
            std::strncpy(text, "0x1B (Undefined)", kMaxParamLen);
            break;
        case 0x1C:
            std::strncpy(text, "0x1C (Undefined)", kMaxParamLen);
            break;
        case 0x1D:
            std::strncpy(text, "0x1D (Undefined)", kMaxParamLen);
            break;
        case 0x1E:
            std::strncpy(text, "0x1E (Undefined)", kMaxParamLen);
            break;
        case 0x1F:
            std::strncpy(text, "0x1F (Undefined)", kMaxParamLen);
            break;
        case 0x46:
            std::strncpy(text, "0x46 Control 1", kMaxParamLen); // [Variation]
            break;
        case 0x47:
            std::strncpy(text, "0x47 Control 2", kMaxParamLen); // [Timbre]
            break;
        case 0x48:
            std::strncpy(text, "0x48 Control 3", kMaxParamLen); // [Release]
            break;
        case 0x49:
            std::strncpy(text, "0x49 Control 4", kMaxParamLen); // [Attack]
            break;
        case 0x4A:
            std::strncpy(text, "0x4A Control 5", kMaxParamLen); // [Brightness]
            break;
        case 0x4B:
            std::strncpy(text, "0x4B Control 6", kMaxParamLen); // [Decay]
            break;
        case 0x4C:
            std::strncpy(text, "0x4C Control 7", kMaxParamLen); // [Vib Rate]
            break;
        case 0x4D:
            std::strncpy(text, "0x4D Control 8", kMaxParamLen); // [Vib Depth]
            break;
        case 0x4E:
            std::strncpy(text, "0x4E Control 9", kMaxParamLen); // [Vib Delay]
            break;
        case 0x4F:
            std::strncpy(text, "0x4F Control 10", kMaxParamLen); // [Undefined]
            break;
        case 0x50:
            std::strncpy(text, "0x50 Gen Purpose 5", kMaxParamLen);
            break;
        case 0x51:
            std::strncpy(text, "0x51 Gen Purpose 6", kMaxParamLen);
            break;
        case 0x52:
            std::strncpy(text, "0x52 Gen Purpose 7", kMaxParamLen);
            break;
        case 0x53:
            std::strncpy(text, "0x53 Gen Purpose 8", kMaxParamLen);
            break;
        case 0x54:
            std::strncpy(text, "0x54 Portamento", kMaxParamLen);
            break;
        case 0x5B:
            std::strncpy(text, "0x5B FX 1 Depth", kMaxParamLen); // [Reverb]
            break;
        case 0x5C:
            std::strncpy(text, "0x5C FX 2 Depth", kMaxParamLen); // [Tremolo]
            break;
        case 0x5D:
            std::strncpy(text, "0x5D FX 3 Depth", kMaxParamLen); // [Chorus]
            break;
        case 0x5E:
            std::strncpy(text, "0x5E FX 4 Depth", kMaxParamLen); // [Detune]
            break;
        case 0x5F:
            std::strncpy(text, "0x5F FX 5 Depth", kMaxParamLen); // [Phaser]
            break;
        default:
            AudioEffectX::getParameterName(index, text); // TODO: REMOVE
            break;
        }
    }

    // ---------------------------------------------

    bool getEffectName(char* const name) override
    {
#ifdef JACKASS_SYNTH
        std::strncpy(name, "JackAss", kVstMaxEffectNameLen);
#else
        std::strncpy(name, "JackAssFX", kVstMaxEffectNameLen);
#endif
        return true;
    }

    bool getProductString(char* const text) override
    {
#ifdef JACKASS_SYNTH
        std::strncpy(text, "JackAss", kVstMaxProductStrLen);
#else
        std::strncpy(text, "JackAssFX", kVstMaxProductStrLen);
#endif
        return true;
    }

    bool getVendorString(char* const text) override
    {
        std::strncpy(text, "falkTX", kVstMaxVendorStrLen);
        return true;
    }

    VstInt32 getVendorVersion() override
    {
        return 1000;
    }

    VstInt32 canDo(char* const text) override
    {
#ifdef JACKASS_SYNTH
        if (std::strcmp(text, "receiveVstEvents") == 0)
            return 1;
        if (std::strcmp(text, "receiveVstMidiEvent") == 0)
            return 1;
#endif
        return -1;

        // maybe unused
        (void)text;
    }

    VstPlugCategory getPlugCategory() override
    {
#ifdef JACKASS_SYNTH
        return kPlugCategSynth;
#else
        return kPlugCategEffect;
#endif
    }

    // ---------------------------------------------

    VstInt32 getNumMidiInputChannels() override
    {
#ifdef JACKASS_SYNTH
        return 16;
#else
        return 0;
#endif
    }

    VstInt32 getNumMidiOutputChannels() override
    {
        return 0;
    }

    // ---------------------------------------------

private:
    JackAssInstance* fInstance;

    float fParamBuffers[kParamCount];
#ifdef USE_PROGRAMS
    char* fProgramNames[kProgramCount];
#endif
};

// -------------------------------------------------
// DLL entry point

AudioEffect* createEffectInstance(audioMasterCallback audioMaster)
{
    return new JackAss(audioMaster);
}

// -------------------------------------------------
