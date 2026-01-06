#include <stddef.h>

#ifdef _WIN32
#define PTT_LIBRARY_EXPORT __declspec(dllexport)
#else
#define PTT_LIBRARY_EXPORT __attribute__((visibility("default")))
#endif

typedef void (*callback_fptr)(int action, int value);

static callback_fptr callback = NULL;
static int ptt_state = 0;

PTT_LIBRARY_EXPORT int init(void* cb)
{
    callback = (callback_fptr)cb;
    return 0;
}

PTT_LIBRARY_EXPORT int uninit(void)
{
    callback = NULL;
    return 0;
}

PTT_LIBRARY_EXPORT int set_ptt(int enable)
{
    ptt_state = enable;
    if (callback != NULL)
    {
        callback(0, enable);
    }
    return 0;
}

PTT_LIBRARY_EXPORT int get_ptt(int* state)
{
    *state = ptt_state;
    if (callback != NULL)
    {
        callback(1, ptt_state);
    }
    return 0;
}