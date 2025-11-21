#define _DEFAULT_SOURCE
#include <math.h>
#include <unistd.h>
#include <alsa/asoundlib.h>
#include <ncurses.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#include <time.h>
#include <stdint.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SAMPLE_RATE 48000
#define BUFFER_SIZE 1024
#define VOLUME_STEP 0.05f
#define HP_STEP 10.0f
#define LP_STEP 100.0f

_Atomic float volume = 0.5f;
_Atomic float highpass_hz = 1000.0f;
_Atomic float lowpass_hz = 4000.0f;
_Atomic int running = 1;
_Atomic int output_device = 0;

static uint32_t xorshift_state = 123456789;

static uint32_t xorshift32() {
    xorshift_state ^= xorshift_state << 13;
    xorshift_state ^= xorshift_state >> 17;
    xorshift_state ^= xorshift_state << 5;
    return xorshift_state;
}

float calc_alpha(float hz) {
    float rc = 1.0f / (2 * (float)M_PI * hz);
    float dt = 1.0f / SAMPLE_RATE;
    return dt / (rc + dt);
}

short generate_sample(float *prev_raw, float *prev_hp, float *prev_lp, float alpha_hp, float alpha_lp) {
    int16_t raw = (int16_t)(xorshift32() & 0xFFFF);
    float scaled = (raw - 16384) / 32768.0f; // Центрируем вокруг нуля

    // High-pass фильтр
    float hp = alpha_hp * (*prev_hp + scaled - *prev_raw);
    *prev_raw = scaled;
    *prev_hp = hp;

    // Low-pass фильтр
    *prev_lp += alpha_lp * (hp - *prev_lp);

    return (short)(*prev_lp * atomic_load(&volume) * 32767.0f);
}

void* audio_thread(void* arg) {
    int thread_device = *((int*)arg);
    snd_pcm_t* handle;
    short buffer[BUFFER_SIZE];
    const char* devices[] = {"default", "plughw:0,1"};
    snd_pcm_hw_params_t *params;
    unsigned int rate = SAMPLE_RATE;
    int dir;

    float prev_raw = 0.0f, prev_hp = 0.0f, prev_lp = 0.0f;

    while (atomic_load(&running)) {
        int err = snd_pcm_open(&handle, devices[thread_device], SND_PCM_STREAM_PLAYBACK, 0);
        if (err < 0) {
            usleep(100000);
            continue;
        }

        snd_pcm_hw_params_malloc(&params);
        snd_pcm_hw_params_any(handle, params);
        snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);
        snd_pcm_hw_params_set_format(handle, params, SND_PCM_FORMAT_S16_LE);
        snd_pcm_hw_params_set_channels(handle, params, 1);
        snd_pcm_hw_params_set_rate_near(handle, params, &rate, &dir);

        snd_pcm_uframes_t buf_size = BUFFER_SIZE * 4;
        snd_pcm_hw_params_set_buffer_size_near(handle, params, &buf_size);

        err = snd_pcm_hw_params(handle, params);
        if (err < 0) {
            snd_pcm_hw_params_free(params);
            snd_pcm_close(handle);
            usleep(100000);
            continue;
        }

        err = snd_pcm_prepare(handle);
        if (err < 0) {
            snd_pcm_hw_params_free(params);
            snd_pcm_close(handle);
            usleep(100000);
            continue;
        }

        while (atomic_load(&running) && atomic_load(&output_device) == thread_device) {
            float alpha_hp = calc_alpha(atomic_load(&highpass_hz));
            float alpha_lp = calc_alpha(atomic_load(&lowpass_hz));

            for (int i = 0; i < BUFFER_SIZE; ++i) {
                buffer[i] = generate_sample(&prev_raw, &prev_hp, &prev_lp, alpha_hp, alpha_lp);
            }

            int frames = snd_pcm_writei(handle, buffer, BUFFER_SIZE);
            if (frames == -EPIPE) {
                snd_pcm_prepare(handle);
            } else if (frames < 0) {
                break; // Переподключимся при серьезной ошибке
            }
        }

        snd_pcm_hw_params_free(params);
        snd_pcm_drain(handle);
        snd_pcm_close(handle);

        // Обновляем device для потока при изменении
        thread_device = atomic_load(&output_device);
    }
    return NULL;
}

int main() {
    initscr();
    noecho();
    curs_set(FALSE);
    nodelay(stdscr, TRUE);
    keypad(stdscr, TRUE);

    mvprintw(1, 2, "White Noise Generator (Q to quit)");
    mvprintw(3, 2, "Volume      : ");
    mvprintw(4, 2, "Highpass Hz : ");
    mvprintw(5, 2, "Lowpass Hz  : ");
    mvprintw(6, 2, "Output Device: ");
    mvprintw(8, 2, "Controls: +/- volume, W/S highpass, E/D lowpass, Tab switch device");
    refresh();

    pthread_t thread;
    int thread_device = atomic_load(&output_device);
    pthread_create(&thread, NULL, audio_thread, &thread_device);

    float last_volume = -1, last_hp = -1, last_lp = -1;
    int last_dev = -1;
    struct timespec ts = {.tv_sec = 0, .tv_nsec = 20000000};

    while (atomic_load(&running)) {
        float current_vol = atomic_load(&volume);
        if (current_vol != last_volume) {
            mvprintw(3, 15, "%.2f     ", current_vol);
            last_volume = current_vol;
        }

        float current_hp = atomic_load(&highpass_hz);
        if (current_hp != last_hp) {
            mvprintw(4, 15, "%.0f Hz    ", current_hp);
            last_hp = current_hp;
        }

        float current_lp = atomic_load(&lowpass_hz);
        if (current_lp != last_lp) {
            mvprintw(5, 15, "%.0f Hz    ", current_lp);
            last_lp = current_lp;
        }

        int current_dev = atomic_load(&output_device);
        if (current_dev != last_dev) {
            mvprintw(6, 15, "%s      ", current_dev ? "Digital" : "Analog");
            last_dev = current_dev;
        }

        refresh();

        int ch = getch();
        if (ch != ERR) {
            switch(ch) {
                case 'Q': case 'q':
                    atomic_store(&running, 0);
                    break;
                case '+':
                    atomic_store(&volume, fmin(2.0f, current_vol + VOLUME_STEP));
                    break;
                case '-':
                    atomic_store(&volume, fmax(0.0f, current_vol - VOLUME_STEP));
                    break;
                case 'W': case 'w':
                    atomic_store(&highpass_hz, current_hp + HP_STEP);
                    break;
                case 'S': case 's':
                    atomic_store(&highpass_hz, fmax(10.0f, current_hp - HP_STEP));
                    break;
                case 'E': case 'e':
                    atomic_store(&lowpass_hz, current_lp + LP_STEP);
                    break;
                case 'D': case 'd':
                    atomic_store(&lowpass_hz, fmax(current_hp + 10.0f, current_lp - LP_STEP)); // Не ниже highpass
                    break;
                case '\t':
                    atomic_store(&output_device, !current_dev);
                    break;
            }
        }

        nanosleep(&ts, NULL);
    }

    endwin();
    pthread_join(thread, NULL);
    return 0;
}
