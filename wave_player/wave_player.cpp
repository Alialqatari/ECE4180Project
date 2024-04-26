#include "wave_player.h"
#define DAC_BUFFER_SIZE 256

// Constructor -- sets initial values and configures DAC and PWM outputs
wave_player::wave_player(AnalogOut *_dac) {
    wave_DAC = _dac;
    wave_DAC->write_u16(32768);  // Center the DAC output to mid-range
    // wave_PWM = _pwm;
    // wave_PWM->write(0.0f);
    verbosity = 0;
    paused = 0;
    DAC_wptr = 0;
    DAC_rptr = 0;
    DAC_on = 0;
    sample = 0;

    for (int i = 0; i < DAC_BUFFER_SIZE; i++) {
        DAC_fifo[i] = 32768;  // Set to mid-range to represent silence
    }
}

// Set verbosity for debugging
void wave_player::set_verbosity(int v) {
    verbosity = v;
}

// Play WAV file from a file pointer
void wave_player::play(FILE *wavefile) {
    unsigned chunk_id, chunk_size, channel;
    unsigned data, samp_int;
    short unsigned dac_data;
    long long slice_value;
    char *slice_buf;
    short *data_sptr;
    unsigned char *data_bptr;
    int *data_wptr;
    FMT_STRUCT wav_format;
    long slice, num_slices;

    DAC_wptr=4;
    DAC_on=0;

    fread(&chunk_id, 4, 1, wavefile);
    fread(&chunk_size, 4, 1, wavefile);

    while (!feof(wavefile)) {
        if (verbosity) {
            printf("Read chunk ID 0x%x, size 0x%x\r\n", chunk_id, chunk_size);
        }

        switch (chunk_id) {
            case 0x46464952: // "RIFF"
                fread(&data, 4, 1, wavefile);
                if (verbosity) {
                    printf("RIFF chunk\r\n");
                    printf("  chunk size %d (0x%x)\r\n", chunk_size, chunk_size);
                    printf("  RIFF type 0x%x\r\n", data);
                }
                break;
            case 0x20746d66: // "fmt "
                fread(&wav_format, sizeof(wav_format), 1, wavefile);
                if (chunk_size > sizeof(wav_format)) {
                    fseek(wavefile, chunk_size - sizeof(wav_format), SEEK_CUR);
                }
                if (verbosity) {
                    printf("FORMAT chunk\r\n");
                    printf("  compression code %d\r\n", wav_format.comp_code);
                    printf("  %d channels\r\n", wav_format.num_channels);
                    printf("  %d samples/sec\r\n", wav_format.sample_rate);
                    printf("  %d bytes/sec\r\n", wav_format.avg_Bps);
                    printf("  block align %d\r\n", wav_format.block_align);
                    printf("  %d bits per sample\r\n", wav_format.sig_bps);
                }
                break;
            case 0x61746164: // "data"
                slice_buf = (char *)malloc(wav_format.block_align);
                if (!slice_buf) {
                    printf("Unable to malloc slice buffer\r\n");
                    exit(1);
                }
                num_slices = chunk_size / wav_format.block_align;
                samp_int = 1000000 / wav_format.sample_rate;
                if (verbosity) {
                    printf("DATA chunk\r\n");
                    printf("  chunk size %d (0x%x)\r\n", chunk_size, chunk_size);
                    printf("  %ld slices\r\n", num_slices);
                    printf("  Ideal sample interval=%d\r\n", (unsigned)(1000000.0 / wav_format.sample_rate));
                    printf("  programmed interrupt tick interval=%d\r\n", samp_int);
                }

                // Start ticker to write samples
                if (!paused) {
                    tick.attach_us(callback(this, &wave_player::dac_out), samp_int);
                    sample = samp_int;
                    DAC_on = 1;
                }

                // Start reading and processing slices
                for (slice = 0; slice < num_slices; slice++) {
                    // if (paused) {
                    //     pause();
                    //     // if (!paused) {
                    //     //     resume();
                    //     // }
                    // }
                    fread(slice_buf, wav_format.block_align, 1, wavefile);
                    if (feof(wavefile)) {
                        printf("Not enough slices in the wave file\r\n");
                        exit(1);
                    }

                    // Process each slice based on the sample size and type
                    data_sptr = (short *)slice_buf; // 16 bit samples
                    data_bptr = (unsigned char *)slice_buf; // 8 bit samples
                    data_wptr = (int *)slice_buf; // 32 bit samples
                    slice_value = 0;
                    for (channel = 0; channel < wav_format.num_channels; channel++) {
                        switch (wav_format.sig_bps) {
                            case 16:
                                slice_value += data_sptr[channel];
                                break;
                            case 32:
                                slice_value += data_wptr[channel];
                                break;
                            case 8:
                                slice_value += data_bptr[channel];
                                break;
                        }
                    }

                    // Average and scale the sample value to match DAC input
                    slice_value /= wav_format.num_channels;
                    switch (wav_format.sig_bps) {
                        case 8:
                            slice_value <<= 8;
                            break;
                        case 16:
                            slice_value += 32768;
                            break;
                        case 32:
                            slice_value >>= 16;
                            slice_value += 32768;
                            break;
                    }

                    dac_data = (short unsigned)slice_value;
                    DAC_fifo[DAC_wptr] = dac_data;
                    DAC_wptr = (DAC_wptr + 1) % DAC_BUFFER_SIZE;

                    // Handle DAC buffer overflow
                    while (DAC_wptr == DAC_rptr && DAC_on) {
                        // Wait if buffer is full and DAC is still on
                    }
                }

                DAC_on = 0;
                tick.detach();
                free(slice_buf);
                break;
            default:
                printf("Unknown chunk type 0x%x, size %d\r\n", chunk_id, chunk_size);
                data = fseek(wavefile, chunk_size, SEEK_CUR);
                break;
        }

        fread(&chunk_id, 4, 1, wavefile);
        fread(&chunk_size, 4, 1, wavefile);
    }
}

// Output handler for DAC
void wave_player::dac_out() {
    if (DAC_on && paused == 0) {
        // if (DAC_rptr != DAC_wptr) {  // Ensure there is data to read
            wave_DAC->write_u16(DAC_fifo[DAC_rptr]);
            LPC_DAC->DACR = DAC_fifo[DAC_rptr] & 0XFFC0; // Apply mask to DAC data  // Apply mask correctly before writing
            DAC_rptr = (DAC_rptr + 1) % DAC_BUFFER_SIZE;  // Ensure it wraps around correctly
        // } else {
            // Handle buffer underrun scenario, maybe by writing a default silent value:
            //wave_DAC->write_u16(32768);  // Write mid-point value assuming 16-bit DAC for silence
        // }
    }
}

// Pause playback
void wave_player::pause() {
    paused = 1;
    DAC_on = 0;
    tick.detach(); // Stop the ticker to freeze DAC output
}

// Resume playback
void wave_player::resume() {
    if (paused == 1) {
        paused = 0;
        if (DAC_rptr != DAC_wptr) {
            DAC_on = 1;
            tick.attach_us(callback(this, &wave_player::dac_out), sample);
        }
        // Resume DAC output at correct sample rate
    }
}