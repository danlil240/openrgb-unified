/*---------------------------------------------------------*\
||| AudioLoopback.cpp                                         |
|||                                                           |
|||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "AudioLoopback.h"

/*---------------------------------------------------------*\
||| Shared lifecycle — identical on every platform; only  |
|||   touches std:: members. ThreadMain/Pump stay inside    |
|||   the per-OS blocks below.                              |
\*---------------------------------------------------------*/
namespace studio
{

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

} /* namespace studio */

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

#elif defined(__linux__)

#include <pulse/pulseaudio.h>

#include <chrono>
#include <string>
#include <thread>

namespace studio
{

/* One AudioLoopback exists per plugin instance; the session-scoped
   PulseAudio handles live in this file-static so Pump() (whose
   signature is fixed by the shared header) can reach them. */
static pa_threaded_mainloop* s_ml  = nullptr;
static pa_context*           s_ctx = nullptr;

/*---------------------------------------------------------*\
||| Same energy formula as ChunkEnergy — file-local copy  |
|||   for float32 interleaved only (we request that         |
|||   format ourselves).                                  |
\*---------------------------------------------------------*/
static float EnergyF32(const float* s, size_t frames, int ch)
{
    double acc = 0.0;
    for(size_t i = 0; i < frames * (size_t)ch; i++)
    {
        acc += (double)s[i] * s[i];
    }
    return (float)(acc / (frames * (size_t)ch));
}

/* Default-sink changes reopen the stream — the same contract
   the WASAPI DefaultDeviceNotifier provides. */
static void ServerEventCb(pa_context*, pa_subscription_event_type_t t,
                          uint32_t, void* ud)
{
    if((t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) == PA_SUBSCRIPTION_EVENT_SERVER
       && (t & PA_SUBSCRIPTION_EVENT_TYPE_MASK) == PA_SUBSCRIPTION_EVENT_CHANGE)
    {
        static_cast<std::atomic<bool>*>(ud)->store(true);
    }
}

static void ServerInfoCb(pa_context*, const pa_server_info* i, void* ud)
{
    *static_cast<std::string*>(ud) =
        (i != nullptr && i->default_sink_name != nullptr)
            ? i->default_sink_name : "";
}

/* Waits for a pa_operation to finish; the caller must NOT hold the
   mainloop lock (completion is signalled on the mainloop thread). */
static void WaitOp(pa_operation* op)
{
    while(op != nullptr
          && pa_operation_get_state(op) == PA_OPERATION_RUNNING)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if(op != nullptr)
    {
        pa_operation_unref(op);
    }
}

void AudioLoopback::ThreadMain()
{
    s_ml = pa_threaded_mainloop_new();
    if(s_ml == nullptr)
    {
        SetStatus("audio: pulseaudio init failed");
        running = false;
        return;
    }
    s_ctx = pa_context_new(pa_threaded_mainloop_get_api(s_ml),
                           "DesktopLightingStudio");
    if(s_ctx == nullptr)
    {
        pa_threaded_mainloop_free(s_ml);
        s_ml = nullptr;
        SetStatus("audio: pulseaudio init failed");
        running = false;
        return;
    }
    pa_context_set_subscribe_callback(s_ctx, ServerEventCb, &reinit);

    if(pa_context_connect(s_ctx, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0
       || pa_threaded_mainloop_start(s_ml) < 0)
    {
        SetStatus("audio: no pulseaudio server");
        running = false;
        goto done;
    }

    /* Wait for the context handshake. */
    for(;;)
    {
        const pa_context_state_t cs = pa_context_get_state(s_ctx);
        if(cs == PA_CONTEXT_READY)
        {
            break;
        }
        if(cs == PA_CONTEXT_FAILED || cs == PA_CONTEXT_TERMINATED
           || !running.load())
        {
            SetStatus("audio: no pulseaudio server");
            running = false;
            goto done;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    WaitOp(pa_context_subscribe(s_ctx, PA_SUBSCRIPTION_MASK_SERVER,
                                nullptr, nullptr));

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

done:
    if(s_ctx != nullptr)
    {
        pa_context_disconnect(s_ctx);
        pa_context_unref(s_ctx);
        s_ctx = nullptr;
    }
    pa_threaded_mainloop_stop(s_ml);
    pa_threaded_mainloop_free(s_ml);
    s_ml = nullptr;
    if(!running.load())
    {
        SetStatus("audio: off");
    }
}

bool AudioLoopback::Pump()
{
    /* — resolve default sink — */
    std::string sink;
    WaitOp(pa_context_get_server_info(s_ctx, ServerInfoCb, &sink));
    if(sink.empty())
    {
        SetStatus("audio: no default sink");
        return running.load();
    }

    /* — record stream on "<sink>.monitor" — */
    const pa_sample_spec ss = { PA_SAMPLE_FLOAT32NE, 48000, 2 };
    pa_channel_map cm;
    pa_channel_map_init_stereo(&cm);
    pa_stream* st = pa_stream_new(s_ctx, "DesktopLightingStudio", &ss, &cm);
    if(st == nullptr)
    {
        SetStatus("audio: stream create failed");
        return running.load();
    }
    const std::string src = sink + ".monitor";
    const pa_buffer_attr ba = { (uint32_t)-1, (uint32_t)-1, (uint32_t)-1,
                                (uint32_t)-1,
                                (uint32_t)(48000 * 2 * 4 / 100) }; /* ~10ms */
    if(pa_stream_connect_record(st, src.c_str(), &ba, PA_STREAM_NOFLAGS) < 0)
    {
        pa_stream_unref(st);
        SetStatus("audio: monitor connect failed");
        return running.load();
    }

    /* Wait for the stream to come up. */
    for(;;)
    {
        const pa_stream_state_t ss_state = pa_stream_get_state(st);
        if(ss_state == PA_STREAM_READY)
        {
            break;
        }
        if(ss_state == PA_STREAM_FAILED || ss_state == PA_STREAM_TERMINATED
           || !running.load())
        {
            pa_stream_disconnect(st);
            pa_stream_unref(st);
            SetStatus("audio: monitor connect failed");
            return running.load();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    SetStatus("audio: listening");
    reinit = false;

    /* Poll the record stream: peek -> energy -> drop, mirroring the
       WASAPI GetBuffer loop. peek/drop need the mainloop lock. */
    while(running.load() && !reinit.load())
    {
        pa_threaded_mainloop_lock(s_ml);
        for(;;)
        {
            const void* data = nullptr;
            size_t      len  = 0;
            const int   rc   = pa_stream_peek(st, &data, &len);
            if(rc < 0)
            {
                if(pa_context_errno(s_ctx) != PA_ERR_NODATA)
                {
                    reinit = true;
                }
                break;
            }
            pa_stream_drop(st);
            const size_t frames = len / (2 * sizeof(float));
            if(frames == 0)
            {
                continue;      /* timing-only chunk — nothing to feed */
            }
            /* data == nullptr means a hole — feed zero energy exactly
               like AUDCLNT_BUFFERFLAGS_SILENT. */
            const float energy =
                (data != nullptr)
                    ? EnergyF32(static_cast<const float*>(data),
                                frames, 2)
                    : 0.0f;
            onset.SetSensitivity(sens.load());
            const float hit = onset.Feed(energy, frames / 48000.0);
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
        }
        pa_threaded_mainloop_unlock(s_ml);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    pa_stream_disconnect(st);
    pa_stream_unref(st);
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

#elif defined(__APPLE__)
/* macOS backend lives in inputs/AudioLoopback_mac.mm */

#else /* unsupported platform — report honestly, never crash */

namespace studio
{

void AudioLoopback::ThreadMain()
{
    SetStatus("audio: unsupported platform");
    running = false;
}

bool AudioLoopback::Pump()
{
    return false;
}

} /* namespace studio */

#endif
