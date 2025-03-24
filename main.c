#include <alsa/asoundlib.h>
#include <ncurses.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <fcntl.h>

#define SAMPLE_RATE 48000
#define FRAME_SIZE 2
#define BUFFER_SIZE 1024

volatile float volume = 0.5f;
volatile float highpass_hz = 100.0f;
volatile float lowpass_hz = 4000.0f;
volatile int running = 1;

float prev_raw = 0, prev_hp = 0, prev_lp = 0;
int urandom_fd = -1;

float calc_alpha(float hz) {
    float rc = 1.0f / (2 * M_PI * hz);
    float dt = 1.0f / SAMPLE_RATE;
    return dt / (rc + dt);
}

short generate_sample() {
    short raw_sample;
    if (read(urandom_fd, &raw_sample, sizeof(raw_sample)) != sizeof(raw_sample)) {
        raw_sample = rand() & 0xFFFF; // fallback
    }
    float scaled = raw_sample / 32768.0f;

    float alpha_hp = calc_alpha(highpass_hz);
    float alpha_lp = calc_alpha(lowpass_hz);
    float hp = alpha_hp * (prev_hp + scaled - prev_raw);
    prev_raw = scaled;
    prev_hp = hp;
    prev_lp = prev_lp + alpha_lp * (hp - prev_lp);

    return (short)(prev_lp * volume * 32767.0f);
}

void* audio_thread(void* arg) {
    snd_pcm_t* handle;
    snd_pcm_hw_params_t* params;
    int err = snd_pcm_open(&handle, "plughw:0,0", SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        fprintf(stderr, "Failed to open ALSA device: %s\n", snd_strerror(err));
        return NULL;
    }

    snd_pcm_hw_params_malloc(&params);
    snd_pcm_hw_params_any(handle, params);
    snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(handle, params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(handle, params, 1);
    snd_pcm_hw_params_set_rate(handle, params, SAMPLE_RATE, 0);
    snd_pcm_hw_params(handle, params);
    snd_pcm_prepare(handle);

    short buffer[BUFFER_SIZE];
    while (running) {
        for (int i = 0; i < BUFFER_SIZE; i++)
            buffer[i] = generate_sample();
        int frames = snd_pcm_writei(handle, buffer, BUFFER_SIZE);
        if (frames < 0) {
            fprintf(stderr, "write error: %s\n", snd_strerror(frames));
            snd_pcm_prepare(handle);
        }
    }

    snd_pcm_drain(handle);
    snd_pcm_close(handle);
    return NULL;
}

int main() {
    urandom_fd = open("/dev/urandom", O_RDONLY);
    if (urandom_fd < 0) {
        perror("Failed to open /dev/urandom");
        return 1;
    }

    initscr();
    noecho();
    curs_set(FALSE);
    nodelay(stdscr, TRUE);
    keypad(stdscr, TRUE);

    pthread_t thread;
    pthread_create(&thread, NULL, audio_thread, NULL);

    while (running) {
        clear();
        mvprintw(1, 2, "White Noise Generator (press Q to quit)");
        mvprintw(3, 2, "Volume     : %.2f (+/-)", volume);
        mvprintw(4, 2, "Highpass Hz: %.0f (W/S)", highpass_hz);
        mvprintw(5, 2, "Lowpass Hz : %.0f (E/D)", lowpass_hz);
        refresh();

        int ch = getch();
        switch (ch) {
            case 'q': case 'Q': running = 0; break;
            case '+': volume += 0.01f; if (volume > 1.0f) volume = 1.0f; break;
            case '-': volume -= 0.01f; if (volume < 0.0f) volume = 0.0f; break;
            case 'w': highpass_hz += 10.0f; break;
            case 's': highpass_hz -= 10.0f; if (highpass_hz < 10.0f) highpass_hz = 10.0f; break;
            case 'e': lowpass_hz += 100.0f; break;
            case 'd': lowpass_hz -= 100.0f; if (lowpass_hz < 100.0f) lowpass_hz = 100.0f; break;
        }
        usleep(10000);
    }

    endwin();
    pthread_join(thread, NULL);
    close(urandom_fd);
    return 0;
}
