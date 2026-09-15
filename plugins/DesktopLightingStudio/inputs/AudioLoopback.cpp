/*---------------------------------------------------------*\
||| AudioLoopback.cpp                                         |
|||                                                           |
|||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "AudioLoopback.h"

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <ksmedia.h>
#include <wrl/client.h>

#include <chrono>

using Microsoft::WRL::ComPtr;

namespace studio
{

/*---------------------------------------------------------*\
||| Minimal IMMNotificationClient — we only care that the   |
||| default render endpoint changed, so the loop reopens.   |
||| Stack-allocated: AddRef/Release never delete.           |
\*---------------------------------------------------------*/
class DefaultDeviceNotifier : public IMMNotificationClient
{
public:
    explicit DefaultDeviceNotifier(std::atomic<bool>* flag) : reinit(flag) {}

    ULONG   AddRef()  override { return 1; }
    ULONG   Release() override { return 1; }
    HRESULT QueryInterface(REFIID riid, void** ppv) override
    {
        if(riid == __uuidof(IUnknown) || riid == __uuidof(IMMNotificationClient))
        {
            *ppv = this;
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    HRESULT OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override
    {
        if(flow == eRender && role == eConsole)
        {
            reinit->store(true);
        }
        return S_OK;
    }
    HRESULT OnDeviceAdded(LPCWSTR) override                          { return S_OK; }
    HRESULT OnDeviceRemoved(LPCWSTR) override                        { return S_OK; }
    HRESULT OnDeviceStateChanged(LPCWSTR, DWORD) override            { return S_OK; }
    HRESULT OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

private:
    std::atomic<bool>* reinit;
};

/*---------------------------------------------------------*\
||| Chunk energy: mean square over all channels.            |
|||   float32 interleaved and pcm16 interleaved covered;    |
|||   anything else returns false -> caller reinitializes   |
|||   with a different format request.                      |
\*---------------------------------------------------------*/
static bool ChunkEnergy(const BYTE* data, UINT32 frames,
                        const WAVEFORMATEX* wfx, DWORD flags, float& energy)
{
    if(flags & AUDCLNT_BUFFERFLAGS_SILENT || data == nullptr || frames == 0)
    {
        energy = 0.0f;
        return true;
    }
    const int ch = wfx->nChannels;
    double acc = 0.0;
    if(wfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT
       || (wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE
           && reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wfx)->SubFormat
                  == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))
    {
        const float* s = reinterpret_cast<const float*>(data);
        for(UINT32 i = 0; i < frames * (UINT32)ch; i++)
        {
            acc += (double)s[i] * s[i];
        }
    }
    else if(wfx->wFormatTag == WAVE_FORMAT_PCM && wfx->wBitsPerSample == 16)
    {
        const short* s = reinterpret_cast<const short*>(data);
        for(UINT32 i = 0; i < frames * (UINT32)ch; i++)
        {
            const double v = s[i] / 32768.0;
            acc += v * v;
        }
    }
    else
    {
        return false;
    }
    energy = (float)(acc / (frames * (UINT32)ch));
    return true;
}

AudioLoopback::~AudioLoopback()
{
    Stop();
}

std::string AudioLoopback::Status() const
{
    std::lock_guard<std::mutex> lock(status_mu);
    return status;
}

void AudioLoopback::SetStatus(const std::string& s)
{
    std::lock_guard<std::mutex> lock(status_mu);
    status = s;
}

bool AudioLoopback::Start(InputBus* b)
{
    if(running.load())
    {
        return false;
    }
    bus     = b;
    running = true;
    reinit  = false;
    onset.Reset();
    th = std::thread(&AudioLoopback::ThreadMain, this);
    return true;
}

void AudioLoopback::Stop()
{
    if(!running.load())
    {
        return;
    }
    running = false;
    if(th.joinable())
    {
        th.join();
    }
    bus = nullptr;
}

void AudioLoopback::ThreadMain()
{
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if(FAILED(hr) && hr != RPC_E_CHANGED_MODE)
    {
        SetStatus("audio: COM init failed");
        running = false;
        return;
    }
    while(running.load())
    {
        if(!Pump())
        {
            break;
        }
        /* Session ended (device change or error) — brief backoff,
           then reopen. */
        for(int i = 0; i < 5 && running.load(); i++)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    if(bus != nullptr)
    {
        bus->SetAudioLevel(0.0f);
    }
    CoUninitialize();
}

bool AudioLoopback::Pump()
{
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice>           device;
    ComPtr<IAudioClient>        client;
    ComPtr<IAudioCaptureClient> capture;
    WAVEFORMATEX*               wfx = nullptr;
    DefaultDeviceNotifier       notifier(&reinit);

    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                  CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if(FAILED(hr))
    {
        SetStatus("audio: no device enumerator");
        return running.load();
    }
    enumerator->RegisterEndpointNotificationCallback(&notifier);

    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if(FAILED(hr))
    {
        enumerator->UnregisterEndpointNotificationCallback(&notifier);
        SetStatus("audio: no default output device");
        return running.load();
    }
    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client);
    if(SUCCEEDED(hr))
    {
        hr = client->GetMixFormat(&wfx);
    }
    if(SUCCEEDED(hr))
    {
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                AUDCLNT_STREAMFLAGS_LOOPBACK,
                                0, 0, wfx, nullptr);
    }
    if(SUCCEEDED(hr))
    {
        hr = client->GetService(IID_PPV_ARGS(&capture));
    }
    if(FAILED(hr) || capture == nullptr)
    {
        enumerator->UnregisterEndpointNotificationCallback(&notifier);
        if(wfx != nullptr) { CoTaskMemFree(wfx); }
        SetStatus("audio: loopback init failed");
        return running.load();
    }
    client->Start();
    SetStatus("audio: listening");
    reinit = false;

    const double rate = (double)wfx->nSamplesPerSec;
    while(running.load() && !reinit.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        UINT32 packet = 0;
        hr = capture->GetNextPacketSize(&packet);
        while(SUCCEEDED(hr) && packet > 0 && running.load() && !reinit.load())
        {
            BYTE*   data   = nullptr;
            UINT32  frames = 0;
            DWORD   flags  = 0;
            hr = capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
            if(FAILED(hr))
            {
                break;
            }
            float energy = 0.0f;
            const bool ok = ChunkEnergy(data, frames, wfx, flags, energy);
            capture->ReleaseBuffer(frames);
            if(!ok)
            {
                SetStatus("audio: unsupported mix format");
                running = false;
                break;
            }
            onset.SetSensitivity(sens.load());
            const float hit = onset.Feed(energy, frames / rate);
            if(bus != nullptr)
            {
                bus->SetAudioLevel(onset.Level());
                if(hit > 0.0f)
                {
                    /* Position-less event — the ripple layer's origin
                       decides where shockwave rings spawn. */
                    bus->PushEvent("audio", hit);
                }
            }
            hr = capture->GetNextPacketSize(&packet);
        }
        if(FAILED(hr))
        {
            break;      /* e.g. AUDCLNT_E_DEVICE_INVALIDATED -> reopen */
        }
    }

    client->Stop();
    enumerator->UnregisterEndpointNotificationCallback(&notifier);
    if(wfx != nullptr)
    {
        CoTaskMemFree(wfx);
    }
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

} /* namespace studio */

#else /* non-Windows stub — the plugin only targets Windows today */

namespace studio
{

AudioLoopback::~AudioLoopback() { Stop(); }
std::string AudioLoopback::Status() const { return "audio: unsupported platform"; }
void AudioLoopback::SetStatus(const std::string&) {}
bool AudioLoopback::Start(InputBus*) { return false; }
void AudioLoopback::Stop() {}
void AudioLoopback::ThreadMain() {}
bool AudioLoopback::Pump() { return false; }

} /* namespace studio */

#endif
