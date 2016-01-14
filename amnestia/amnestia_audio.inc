;=============================================================================
macro getbuf {
        mov     rcx,[audio_render_client]
        mov     edx,[audio_buffer_size_in_frames]
        lea     r8,[.ptr+rsp]
        mov     rax,[rcx]
        call    [IAudioRenderClient.GetBuffer+rax]
        test    eax,eax
        js      .error }

macro relbuf {
        mov     rcx,[audio_render_client]
        mov     edx,[audio_buffer_size_in_frames]
        xor     r8d,r8d
        mov     rax,[rcx]
        call    [IAudioRenderClient.ReleaseBuffer+rax]
        test    eax,eax
        js      .error }

macro fillbuf {
        mov     rcx,[audio_stream]
        mov     rdx,[.ptr+rsp]
        mov     r8d,[audio_buffer_size_in_frames]
        shl     r8d,2
        lea     r9,[.read_bytes+rsp]
        mov     qword[k_funcparam5+rsp],0
        call    [ReadFile] }
;=============================================================================
; out: eax - result
align 32
audio_play:
;-----------------------------------------------------------------------------
macro activ {
        mov     rcx,[audio_device]
        lea     rdx,[IID_IAudioClient]
        mov     r8d,CLSCTX_ALL
        xor     r9d,r9d
        lea     rax,[audio_client]
        mov     [k_funcparam5+rsp],rax
        mov     rax,[rcx]
        call    [IMMDevice.Activate+rax]
        test    eax,eax
        js      .error }

macro init {
        mov     rcx,[audio_client]
        mov     edx,AUDCLNT_SHAREMODE_EXCLUSIVE
        mov     r8d,AUDCLNT_STREAMFLAGS_EVENTCALLBACK
        mov     r9,[.period+rsp]
        mov     [k_funcparam5+rsp],r9
        lea     rax,[.k_format]
        mov     [k_funcparam6+rsp],rax
        mov     qword[k_funcparam7+rsp],0
        mov     rax,[rcx]
        call    [IAudioClient.Initialize+rax] }

macro bufsize {
        mov     rcx,[audio_client]
        lea     rdx,[audio_buffer_size_in_frames]
        mov     rax,[rcx]
        call    [IAudioClient.GetBufferSize+rax]
        test    eax,eax
        js      .error }

macro mkevent {
        xor     ecx,ecx
        xor     edx,edx
        xor     r8d,r8d
        mov     r9d,EVENT_ALL_ACCESS
        call    [CreateEventEx]
        test    rax,rax
        jz      .error }

virtual at 0
  rq 7
  .period dq ?
  .ptr dq ?
  .read_bytes dd ?,?
  .k_stack_size = $
end virtual
        push    rsi
        sub     rsp,.k_stack_size
        mov     rsi,rcx
        xor     ecx,ecx
        call    [CoInitialize]
        test    eax,eax
        js      .error
  ; device enumerator
        lea     rcx,[CLSID_MMDeviceEnumerator]
        xor     edx,edx
        mov     r8d,CLSCTX_ALL
        lea     r9,[IID_IMMDeviceEnumerator]
        lea     rax,[audio_enumerator]
        mov     [k_funcparam5+rsp],rax
        call    [CoCreateInstance]
        test    eax,eax
        js      .error
  ; audio endpoint
        mov     rcx,[audio_enumerator]
        mov     edx,eRender
        mov     r8d,eConsole
        lea     r9,[audio_device]
        mov     rax,[rcx]
        call    [IMMDeviceEnumerator.GetDefaultAudioEndpoint+rax]
        test    eax,eax
        js      .error
        activ
  ; check device support
        mov     rcx,[audio_client]
        mov     edx,AUDCLNT_SHAREMODE_EXCLUSIVE
        lea     r8,[.k_format]
        xor     r9d,r9d
        mov     rax,[rcx]
        call    [IAudioClient.IsFormatSupported+rax]
        test    eax,eax
        js      .error
  ; get device period
        mov     rcx,[audio_client]
        xor     edx,edx
        lea     r8,[.period+rsp]
        mov     rax,[rcx]
        call    [IAudioClient.GetDevicePeriod+rax]
        test    eax,eax
        js      .error
  ; try to initialize audio client
        init
        test    eax,eax
        jz      .initialize_ok
        cmp     eax,AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED
        jne     .error
  ; compute new period and initialize again
        bufsize
        xorpd   xmm0,xmm0
        cvtsi2sd xmm0,[audio_buffer_size_in_frames]
        mulsd   xmm0,[.k_10000000_0]
        divsd   xmm0,[.k_44100_0]
        addsd   xmm0,[.k_0_5]
        cvttsd2si rax,xmm0
        mov     [.period+rsp],rax
        mov     rcx,[audio_client]
        mov     rax,[rcx]
        call    [IAudioClient.Release+rax]
        activ
        init
        test    eax,eax
        js      .error
  .initialize_ok:
        mkevent
        mov     [audio_buffer_ready_event],rax
        mkevent
        mov     [audio_shutdown_event],rax
        mov     rcx,[audio_client]
        mov     rdx,[audio_buffer_ready_event]
        mov     rax,[rcx]
        call    [IAudioClient.SetEventHandle+rax]
        bufsize
  ; get audio render client
        mov     rcx,[audio_client]
        lea     rdx,[IID_IAudioRenderClient]
        lea     r8,[audio_render_client]
        mov     rax,[rcx]
        call    [IAudioClient.GetService+rax]
        test    eax,eax
        js      .error
  ; open audio file
        lea     rcx,[.k_audio_file]
        mov     edx,GENERIC_READ
        xor     r8d,r8d
        xor     r9d,r9d
        mov     dword[k_funcparam5+rsp],OPEN_EXISTING
        mov     dword[k_funcparam6+rsp],FILE_ATTRIBUTE_NORMAL+FILE_FLAG_SEQUENTIAL_SCAN
        mov     qword[k_funcparam7+rsp],0
        call    [CreateFile]
        cmp     rax,INVALID_HANDLE_VALUE
        je      .error
        mov     [audio_stream],rax
  ; create audio thread
        xor     ecx,ecx
        xor     edx,edx
        lea     r8,[audio_thread_func]
        xor     r9d,r9d
        mov     dword[k_funcparam5+rsp],0
        mov     qword[k_funcparam6+rsp],0
        call    [CreateThread]
        mov     [audio_thread],rax
        test    rax,rax
        jz      .error
  ; load initial audio data
        getbuf
        fillbuf
        relbuf
  ; start
        mov     rcx,[audio_client]
        mov     rax,[rcx]
        call    [IAudioClient.Start+rax]
        test    eax,eax
        js      .error
  ; success
        mov     eax,1
        add     rsp,.k_stack_size
        pop     rsi
        ret
  .error:
        xor     eax,eax
        add     rsp,.k_stack_size
        pop     rsi
        ret
align 8
  .k_10000000_0 dq 10000000.0
  .k_44100_0 dq 44100.0
  .k_0_5 dq 0.5
  .k_format WAVEFORMATEX WAVE_FORMAT_PCM,2,44100,44100*4,4,16
  .k_audio_file db 'data/flis_amnestia.raw',0
purge activ,init,bufsize,mkevent
;=============================================================================
align 32
audio_stop:
;-----------------------------------------------------------------------------
  .k_stack_size = 4*8
        push    rsi
        sub     rsp,.k_stack_size
        mov     rsi,rcx
        mov     rcx,[audio_shutdown_event]
        test    rcx,rcx
        jz      @f
        call    [SetEvent]
  @@:   mov     rcx,[audio_thread]
        mov     edx,INFINITE
        test    rcx,rcx
        jz      @f
        call    [WaitForSingleObject]
  @@:   scloseh [audio_thread]
        mov     rcx,[audio_client]
        test    rcx,rcx
        jz      @f
        mov     rax,[rcx]
        call    [IAudioClient.Stop+rax]
  @@:   scloseh [audio_stream]
        scloseh [audio_shutdown_event]
        scloseh [audio_buffer_ready_event]
        sreleac [audio_render_client]
        sreleac [audio_client]
        sreleac [audio_device]
        sreleac [audio_enumerator]
        add     rsp,.k_stack_size
        pop     rsi
        ret
;=============================================================================
align 32
audio_thread_func:
;-----------------------------------------------------------------------------
virtual at 0
  rq 5
  .ptr dq ?
  .read_bytes dd ?
  .task_index dd ?
  .wait_array dq ?,?
  .k_stack_size = $+8
end virtual
        push    rsi
        sub     rsp,.k_stack_size
        mov     rsi,rcx
        xor     ecx,ecx
        call    [CoInitialize]
        test    eax,eax
        js      .error
        lea     rcx,[.k_task_name]
        mov     [.task_index+rsp],0
        lea     rdx,[.task_index+rsp]
        call    [AvSetMmThreadCharacteristics]
        test    rax,rax
        jz      .error
        mov     rax,[audio_shutdown_event]
        mov     [.wait_array+rsp],rax
        mov     rax,[audio_buffer_ready_event]
        mov     [.wait_array+rsp+8],rax
  .play_loop:
        mov     ecx,2
        lea     rdx,[.wait_array+rsp]
        xor     r8d,r8d
        mov     r9d,INFINITE
        call    [WaitForMultipleObjects]
        test    eax,eax
        jz      .error
        getbuf
        xor     eax,eax
        mov     edx,[audio_buffer_size_in_frames]
        mov     rcx,[.ptr+rsp]
  @@:   mov     dword[rcx+rax*4],0
        add     eax,1
        cmp     eax,edx
        jne     @b
        fillbuf
        relbuf
        mov     eax,[audio_buffer_size_in_frames]
        shl     eax,2
        cmp     eax,[.read_bytes+rsp]
        jne     .error
        jmp     .play_loop
  .error:
        xor     ecx,ecx
        call    [ExitThread]
        ret
align 1
  .k_task_name db 'Playback',0
;=============================================================================
purge getbuf,relbuf,fillbuf
;=============================================================================