#include "sonar_wav_player.h"
#include "sonar_tx_bram.h"
#include "sonar_audio_hw.h"
#include "xil_io.h"

/* Block-design mapping (hardware/README.md): axi_gpio_1 selects the I2S source
 * (generator or BRAM player); channel 1 of axi_gpio_chirp_control holds the
 * sample count for either source. The player exposes no status register, so
 * completion is timed from the trigger in sonar_chirp.c. */
#define MODE_REG        SONAR_TX_SOURCE_BASEADDR
#define GPIO_TRI_OFFSET 4U
#define MODE_GENERATE   0U
#define MODE_PLAYBACK   1U

static bool available;

static void write_reg(uint32_t address, uint32_t value)
{
    Xil_Out32((UINTPTR)address, value);
    SYNCHRONIZE_IO;
}

bool sonar_wav_player_init(void)
{
    if (available) {
        return true;
    }
    if (MODE_REG == 0U || SONAR_CHIRP_CONTROL_BASEADDR == 0U || !sonar_tx_bram_init()) {
        return false;
    }
    /* Load the safe value before enabling the output driver. */
    write_reg(MODE_REG, MODE_GENERATE);
    write_reg(MODE_REG + GPIO_TRI_OFFSET, 0U);
    available = true;
    return true;
}

bool sonar_wav_player_available(void)
{
    return available;
}

bool sonar_wav_player_select_generate(void)
{
    if (!available) {
        return false;
    }
    write_reg(MODE_REG, MODE_GENERATE);
    return true;
}

bool sonar_wav_player_configure(uint32_t samples, uint32_t amplitude_pct)
{
    if (!available || samples == 0U || samples > sonar_tx_bram_capacity() ||
        !sonar_tx_bram_set_gain(samples, amplitude_pct)) {
        return false;
    }
    write_reg(SONAR_CHIRP_CONTROL_BASEADDR, samples);
    write_reg(MODE_REG, MODE_PLAYBACK);
    return true;
}
