#define COBJMACROS 1
#define _WIN32_WINNT 0x600
#include <malloc.h>
#include <stdlib.h>
#include <process.h>
#include <initguid.h>
#include <audioclient.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <avrt.h>
    const GUID *check1[] = {
      &IID_IAudioClient,
      &IID_IAudioRenderClient,
      &IID_IAudioClient,
      &IID_IAudioEndpointVolume,
    };
int main(void) {
    return 0;
}
