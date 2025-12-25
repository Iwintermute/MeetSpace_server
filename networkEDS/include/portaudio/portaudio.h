#ifndef PORTAUDIO_H
#define PORTAUDIO_H

/*
 * Minimal PortAudio header for bundled builds.
 * Based on the official PortAudio v19 API (MIT license).
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

typedef int PaError;

typedef struct PaStream PaStream;

typedef unsigned long PaSampleFormat;
#define paFloat32        ((PaSampleFormat) 0x00000001)
#define paInt32          ((PaSampleFormat) 0x00000002)
#define paInt24          ((PaSampleFormat) 0x00000004)
#define paInt16          ((PaSampleFormat) 0x00000008)
#define paInt8           ((PaSampleFormat) 0x00000010)
#define paUInt8          ((PaSampleFormat) 0x00000020)
#define paCustomFormat   ((PaSampleFormat) 0x00010000)
#define paNonInterleaved ((PaSampleFormat) 0x80000000)

typedef struct PaStreamCallbackTimeInfo {
    double inputBufferAdcTime;
    double currentTime;
    double outputBufferDacTime;
} PaStreamCallbackTimeInfo;

typedef unsigned long PaStreamCallbackFlags;

typedef enum PaStreamCallbackResult {
    paContinue = 0,
    paComplete = 1,
    paAbort    = 2
} PaStreamCallbackResult;

typedef int PaStreamCallback(
    const void* input,
    void* output,
    unsigned long frameCount,
    const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags,
    void* userData);

typedef enum PaErrorCode {
    paNoError = 0,
    paNotInitialized = -10000,
    paUnanticipatedHostError,
    paInvalidChannelCount,
    paInvalidSampleRate,
    paInvalidDevice,
    paInvalidFlag,
    paSampleFormatNotSupported,
    paBadIODeviceCombination,
    paInsufficientMemory,
    paBufferTooBig,
    paBufferTooSmall,
    paNullCallback,
    paBadStreamPtr,
    paTimedOut,
    paInternalError,
    paDeviceUnavailable,
    paIncompatibleHostApiSpecificStreamInfo,
    paStreamIsStopped,
    paStreamIsNotStopped,
    paInputOverflowed,
    paOutputUnderflowed,
    paHostApiNotFound,
    paInvalidHostApi,
    paCanNotReadFromACallbackStream,
    paCanNotWriteToACallbackStream,
    paCanNotReadFromAnOutputOnlyStream,
    paCanNotWriteToAnInputOnlyStream,
    paIncompatibleStreamHostApi,
    paBadBufferPtr
} PaErrorCode;

PaError Pa_Initialize(void);
PaError Pa_Terminate(void);
PaError Pa_OpenDefaultStream(
    PaStream** stream,
    int numInputChannels,
    int numOutputChannels,
    PaSampleFormat sampleFormat,
    double sampleRate,
    unsigned long framesPerBuffer,
    PaStreamCallback* streamCallback,
    void* userData);
PaError Pa_StartStream(PaStream* stream);
PaError Pa_StopStream(PaStream* stream);
PaError Pa_CloseStream(PaStream* stream);
const char* Pa_GetErrorText(PaError errorCode);

#ifdef __cplusplus
}
#endif

#endif /* PORTAUDIO_H */
