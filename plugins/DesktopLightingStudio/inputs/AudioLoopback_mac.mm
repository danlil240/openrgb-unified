/*---------------------------------------------------------*\
||| AudioLoopback_mac.mm                                    |
|||   macOS system-audio capture via CoreAudio process    |
|||   tap (macOS 14.2+): CATapDescription over the        |
|||   system object -> private aggregate device ->        |
|||   IOProc block computing chunk energy -> OnsetDetect  |
|||   -> InputBus. Same contract as the WASAPI/Pulse      |
|||   backends; lifecycle methods are shared in           |
|||   AudioLoopback.cpp. No audio is retained — only      |
|||   energies and derived events cross the boundary.     |
|||   SPDX-License-Identifier: GPL-2.0-or-later           |
\*---------------------------------------------------------*/

#include "AudioLoopback.h"

#include <CoreAudio/CoreAudio.h>
#import <CoreAudio/CATapDescription.h>
#include <Foundation/Foundation.h>
#include <dispatch/dispatch.h>

#include <chrono>
#include <thread>

namespace studio
{

void AudioLoopback::ThreadMain()
{
    if(@available(macOS 14.2, *))
    {
        while(running.load())
        {
            if(!Pump())
            {
                break;
            }
            /* Session ended — brief backoff, then reopen. */
            for(int i = 0; i < 5 && running.load(); i++)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }
    else
    {
        SetStatus("audio: requires macOS 14.2+");
    }
    if(bus != nullptr)
    {
        bus->SetAudioLevel(0.0f);
    }
    running = false;
}

bool AudioLoopback::Pump()
{
    if(@available(macOS 14.2, *))
    {
        AudioObjectID       tap  = kAudioObjectUnknown;
        AudioObjectID       agg  = kAudioObjectUnknown;
        AudioDeviceIOProcID proc = nullptr;

        @autoreleasepool
        {
            /* — mixdown tap over every process on the system object — */
            CATapDescription* desc =
                [[CATapDescription alloc] initStereoMixdownOfProcesses:
                    @[@(kAudioObjectSystemObject)]];
            desc.muteBehavior = CATapUnmuted;
            OSStatus err = AudioHardwareCreateProcessTap(desc, &tap);
            [desc release];
            if(err != noErr || tap == kAudioObjectUnknown)
            {
                SetStatus("audio: process tap denied — grant Screen & "
                          "System Audio Recording in System Settings");
                running = false;      /* permission — don't retry-spin */
                return false;
            }

            CFStringRef tapUID = nullptr;
            AudioObjectPropertyAddress pa = {
                kAudioTapPropertyUID,
                kAudioObjectPropertyScopeGlobal,
                kAudioObjectPropertyElementMain };
            UInt32 sz = sizeof(CFStringRef);
            err = AudioObjectGetPropertyData(tap, &pa, 0, nullptr,
                                             &sz, &tapUID);
            if(err != noErr || tapUID == nullptr)
            {
                AudioHardwareDestroyProcessTap(tap);
                SetStatus("audio: tap uid query failed");
                return running.load();
            }

            /* — private aggregate device wrapping the tap — */
            NSDictionary* tapEntry = @{
                @(kAudioSubTapUIDKey): (__bridge NSString*)tapUID,
                @(kAudioSubTapDriftCompensationKey): @YES };
            NSDictionary* aggDesc = @{
                @(kAudioAggregateDeviceNameKey): @"DesktopLightingStudio",
                @(kAudioAggregateDeviceUIDKey):
                    @"org.openrgb.studio.loopback",
                @(kAudioAggregateDeviceMainSubDeviceKey):
                    (__bridge NSString*)tapUID,
                @(kAudioAggregateDeviceIsPrivateKey):  @YES,
                @(kAudioAggregateDeviceIsStackedKey):  @NO,
                @(kAudioAggregateDeviceTapListKey):    @[ tapEntry ],
                @(kAudioAggregateDeviceTapAutoStartKey): @YES,
                @(kAudioAggregateDeviceSubDeviceListKey): @[],
            };
            err = AudioHardwareCreateAggregateDevice(
                (__bridge CFDictionaryRef)aggDesc, &agg);
            if(err != noErr || agg == kAudioObjectUnknown)
            {
                AudioHardwareDestroyProcessTap(tap);
                SetStatus("audio: aggregate device failed");
                return running.load();
            }

            /* — input format: accept Float32 / Int16 only, matching
               the WASAPI ChunkEnergy coverage rule — */
            AudioStreamBasicDescription asbd = {};
            pa.mSelector = kAudioDevicePropertyStreamFormat;
            pa.mScope    = kAudioObjectPropertyScopeInput;
            pa.mElement  = kAudioObjectPropertyElementMain;
            sz = sizeof(asbd);
            AudioObjectGetPropertyData(agg, &pa, 0, nullptr, &sz, &asbd);
            const bool isFloat =
                asbd.mFormatID == kAudioFormatLinearPCM
                && (asbd.mFormatFlags & kAudioFormatFlagIsFloat);
            const bool isInt16 =
                asbd.mFormatID == kAudioFormatLinearPCM
                && !(asbd.mFormatFlags & kAudioFormatFlagIsFloat)
                && asbd.mBitsPerChannel == 16;
            if(!isFloat && !isInt16)
            {
                AudioHardwareDestroyAggregateDevice(agg);
                AudioHardwareDestroyProcessTap(tap);
                SetStatus("audio: unsupported tap format");
                running = false;
                return false;
            }
            const double rate =
                asbd.mSampleRate > 0 ? asbd.mSampleRate : 48000.0;

            /* — IOProc: per-buffer mean-square energy -> onset -> bus — */
            AudioLoopback* self = this;
            err = AudioDeviceCreateIOProcIDWithBlock(
                &proc, agg,
                dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0),
                ^(const AudioTimeStamp*, const AudioBufferList* in,
                  const AudioTimeStamp*, AudioBufferList*,
                  const AudioTimeStamp*)
                {
                    double acc = 0.0;
                    UInt32 total  = 0;
                    UInt32 frames = 0;
                    for(UInt32 b = 0; b < in->mNumberBuffers; b++)
                    {
                        const AudioBuffer& buf = in->mBuffers[b];
                        const UInt32 n =
                            buf.mDataByteSize / (isFloat ? 4 : 2);
                        if(isFloat)
                        {
                            const float* s =
                                static_cast<const float*>(buf.mData);
                            for(UInt32 i = 0; i < n; i++)
                            {
                                acc += (double)s[i] * s[i];
                            }
                        }
                        else
                        {
                            const short* s =
                                static_cast<const short*>(buf.mData);
                            for(UInt32 i = 0; i < n; i++)
                            {
                                const double v = s[i] / 32768.0;
                                acc += v * v;
                            }
                        }
                        total += n;
                        frames = buf.mNumberChannels > 0
                                     ? n / buf.mNumberChannels : n;
                    }
                    if(total == 0 || frames == 0)
                    {
                        return;
                    }
                    const float energy = (float)(acc / total);
                    self->onset.SetSensitivity(self->sens.load());
                    const float hit =
                        self->onset.Feed(energy, frames / rate);
                    if(self->bus != nullptr)
                    {
                        self->bus->SetAudioLevel(self->onset.Level());
                        if(hit > 0.0f)
                        {
                            /* Position-less event — ripple origin is
                               the layer's choice. */
                            self->bus->PushEvent("audio", hit);
                        }
                    }
                });
            if(err != noErr || proc == nullptr)
            {
                AudioHardwareDestroyAggregateDevice(agg);
                AudioHardwareDestroyProcessTap(tap);
                SetStatus("audio: IOProc install failed");
                return running.load();
            }
            AudioDeviceStart(agg, proc);
            SetStatus("audio: listening");
            reinit = false;
        }

        while(running.load() && !reinit.load())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        AudioDeviceStop(agg, proc);
        AudioDeviceDestroyIOProcID(agg, proc);
        AudioHardwareDestroyAggregateDevice(agg);
        AudioHardwareDestroyProcessTap(tap);
        if(!running.load())
        {
            SetStatus("audio: off");
        }
        else
        {
            SetStatus("audio: reinitializing");
        }
        return running.load();
    }
    SetStatus("audio: requires macOS 14.2+");
    running = false;
    return false;
}

} /* namespace studio */
