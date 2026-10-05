#include "sonar_pdm.h"

#include <stddef.h>
#include <stdint.h>

/*
 * 801-tap linear-phase low-pass FIR.
 * Design: 2.4 MHz input, 12 kHz cutoff, Kaiser beta 7.0.
 *
 * Only the center coefficient and the positive half are stored because
 * the FIR is symmetric. Coefficients are Q31 and sum to exactly 1.0
 * (2^31) when mirrored.
 *
 * The current transmitted chirp is 3-5 kHz, which is inside the flat
 * part of this filter. If the sonar transmit band is moved much higher,
 * redesign these coefficients and potentially the PCM sample rate.
 */
#define FIR_HALF 400U
#define FIR_TAPS (2U * FIR_HALF + 1U)

static const int32_t fir_q31[FIR_HALF + 1U] = {
    INT32_C(21471070), INT32_C(21467111), INT32_C(21455215), INT32_C(21435399), INT32_C(21407677),
    INT32_C(21372070), INT32_C(21328605), INT32_C(21277315), INT32_C(21218238), INT32_C(21151420),
    INT32_C(21076910), INT32_C(20994764), INT32_C(20905044), INT32_C(20807818), INT32_C(20703159),
    INT32_C(20591144), INT32_C(20471858), INT32_C(20345390), INT32_C(20211834), INT32_C(20071290),
    INT32_C(19923863), INT32_C(19769662), INT32_C(19608802), INT32_C(19441401), INT32_C(19267585),
    INT32_C(19087480), INT32_C(18901219), INT32_C(18708940), INT32_C(18510783), INT32_C(18306894),
    INT32_C(18097421), INT32_C(17882516), INT32_C(17662337), INT32_C(17437041), INT32_C(17206792),
    INT32_C(16971756), INT32_C(16732100), INT32_C(16487997), INT32_C(16239621), INT32_C(15987147),
    INT32_C(15730756), INT32_C(15470626), INT32_C(15206941), INT32_C(14939886), INT32_C(14669647),
    INT32_C(14396410), INT32_C(14120365), INT32_C(13841702), INT32_C(13560610), INT32_C(13277282),
    INT32_C(12991908), INT32_C(12704682), INT32_C(12415795), INT32_C(12125441), INT32_C(11833811),
    INT32_C(11541096), INT32_C(11247489), INT32_C(10953181), INT32_C(10658360), INT32_C(10363216),
    INT32_C(10067936), INT32_C(9772707),  INT32_C(9477714),  INT32_C(9183139),  INT32_C(8889165),
    INT32_C(8595969),  INT32_C(8303731),  INT32_C(8012624),  INT32_C(7722821),  INT32_C(7434493),
    INT32_C(7147806),  INT32_C(6862926),  INT32_C(6580014),  INT32_C(6299228),  INT32_C(6020725),
    INT32_C(5744655),  INT32_C(5471168),  INT32_C(5200409),  INT32_C(4932519),  INT32_C(4667636),
    INT32_C(4405895),  INT32_C(4147425),  INT32_C(3892352),  INT32_C(3640798),  INT32_C(3392882),
    INT32_C(3148716),  INT32_C(2908410),  INT32_C(2672070),  INT32_C(2439795),  INT32_C(2211681),
    INT32_C(1987821),  INT32_C(1768301),  INT32_C(1553204),  INT32_C(1342607),  INT32_C(1136584),
    INT32_C(935203),   INT32_C(738528),   INT32_C(546618),   INT32_C(359528),   INT32_C(177307),
    INT32_C(0),        INT32_C(-172352),  INT32_C(-339714),  INT32_C(-502055),  INT32_C(-659348),
    INT32_C(-811572),  INT32_C(-958709),  INT32_C(-1100747), INT32_C(-1237678), INT32_C(-1369498),
    INT32_C(-1496208), INT32_C(-1617812), INT32_C(-1734321), INT32_C(-1845748), INT32_C(-1952110),
    INT32_C(-2053429), INT32_C(-2149732), INT32_C(-2241049), INT32_C(-2327412), INT32_C(-2408859),
    INT32_C(-2485433), INT32_C(-2557177), INT32_C(-2624139), INT32_C(-2686372), INT32_C(-2743931),
    INT32_C(-2796874), INT32_C(-2845263), INT32_C(-2889161), INT32_C(-2928637), INT32_C(-2963761),
    INT32_C(-2994605), INT32_C(-3021245), INT32_C(-3043759), INT32_C(-3062227), INT32_C(-3076733),
    INT32_C(-3087360), INT32_C(-3094195), INT32_C(-3097328), INT32_C(-3096848), INT32_C(-3092848),
    INT32_C(-3085421), INT32_C(-3074662), INT32_C(-3060668), INT32_C(-3043537), INT32_C(-3023366),
    INT32_C(-3000256), INT32_C(-2974306), INT32_C(-2945618), INT32_C(-2914294), INT32_C(-2880436),
    INT32_C(-2844147), INT32_C(-2805528), INT32_C(-2764684), INT32_C(-2721718), INT32_C(-2676731),
    INT32_C(-2629828), INT32_C(-2581110), INT32_C(-2530680), INT32_C(-2478639), INT32_C(-2425089),
    INT32_C(-2370129), INT32_C(-2313860), INT32_C(-2256379), INT32_C(-2197785), INT32_C(-2138175),
    INT32_C(-2077643), INT32_C(-2016285), INT32_C(-1954193), INT32_C(-1891460), INT32_C(-1828175),
    INT32_C(-1764427), INT32_C(-1700303), INT32_C(-1635889), INT32_C(-1571269), INT32_C(-1506524),
    INT32_C(-1441735), INT32_C(-1376980), INT32_C(-1312335), INT32_C(-1247876), INT32_C(-1183674),
    INT32_C(-1119799), INT32_C(-1056321), INT32_C(-993304),  INT32_C(-930813),  INT32_C(-868910),
    INT32_C(-807654),  INT32_C(-747103),  INT32_C(-687311),  INT32_C(-628332),  INT32_C(-570215),
    INT32_C(-513009),  INT32_C(-456760),  INT32_C(-401511),  INT32_C(-347304),  INT32_C(-294177),
    INT32_C(-242168),  INT32_C(-191310),  INT32_C(-141635),  INT32_C(-93174),   INT32_C(-45954),
    INT32_C(0),        INT32_C(44665),    INT32_C(88019),    INT32_C(130045),   INT32_C(170726),
    INT32_C(210047),   INT32_C(247996),   INT32_C(284562),   INT32_C(319738),   INT32_C(353517),
    INT32_C(385894),   INT32_C(416868),   INT32_C(446438),   INT32_C(474605),   INT32_C(501371),
    INT32_C(526743),   INT32_C(550726),   INT32_C(573328),   INT32_C(594559),   INT32_C(614430),
    INT32_C(632953),   INT32_C(650144),   INT32_C(666017),   INT32_C(680589),   INT32_C(693878),
    INT32_C(705903),   INT32_C(716685),   INT32_C(726246),   INT32_C(734607),   INT32_C(741793),
    INT32_C(747827),   INT32_C(752736),   INT32_C(756545),   INT32_C(759281),   INT32_C(760973),
    INT32_C(761647),   INT32_C(761334),   INT32_C(760062),   INT32_C(757862),   INT32_C(754764),
    INT32_C(750798),   INT32_C(745996),   INT32_C(740389),   INT32_C(734009),   INT32_C(726887),
    INT32_C(719056),   INT32_C(710547),   INT32_C(701392),   INT32_C(691623),   INT32_C(681273),
    INT32_C(670373),   INT32_C(658954),   INT32_C(647050),   INT32_C(634689),   INT32_C(621905),
    INT32_C(608727),   INT32_C(595186),   INT32_C(581311),   INT32_C(567134),   INT32_C(552682),
    INT32_C(537984),   INT32_C(523070),   INT32_C(507966),   INT32_C(492699),   INT32_C(477297),
    INT32_C(461785),   INT32_C(446189),   INT32_C(430534),   INT32_C(414843),   INT32_C(399140),
    INT32_C(383448),   INT32_C(367788),   INT32_C(352183),   INT32_C(336652),   INT32_C(321216),
    INT32_C(305893),   INT32_C(290703),   INT32_C(275662),   INT32_C(260787),   INT32_C(246095),
    INT32_C(231601),   INT32_C(217318),   INT32_C(203262),   INT32_C(189444),   INT32_C(175878),
    INT32_C(162574),   INT32_C(149543),   INT32_C(136795),   INT32_C(124339),   INT32_C(112184),
    INT32_C(100337),   INT32_C(88805),    INT32_C(77594),    INT32_C(66710),    INT32_C(56158),
    INT32_C(45942),    INT32_C(36066),    INT32_C(26531),    INT32_C(17341),    INT32_C(8497),
    INT32_C(0),        INT32_C(-8150),    INT32_C(-15952),   INT32_C(-23407),   INT32_C(-30518),
    INT32_C(-37284),   INT32_C(-43710),   INT32_C(-49796),   INT32_C(-55548),   INT32_C(-60967),
    INT32_C(-66059),   INT32_C(-70827),   INT32_C(-75277),   INT32_C(-79413),   INT32_C(-83242),
    INT32_C(-86768),   INT32_C(-89999),   INT32_C(-92940),   INT32_C(-95598),   INT32_C(-97981),
    INT32_C(-100094),  INT32_C(-101946),  INT32_C(-103544),  INT32_C(-104896),  INT32_C(-106009),
    INT32_C(-106892),  INT32_C(-107552),  INT32_C(-107998),  INT32_C(-108237),  INT32_C(-108278),
    INT32_C(-108130),  INT32_C(-107801),  INT32_C(-107298),  INT32_C(-106631),  INT32_C(-105807),
    INT32_C(-104836),  INT32_C(-103724),  INT32_C(-102481),  INT32_C(-101114),  INT32_C(-99631),
    INT32_C(-98041),   INT32_C(-96350),   INT32_C(-94568),   INT32_C(-92700),   INT32_C(-90755),
    INT32_C(-88740),   INT32_C(-86662),   INT32_C(-84528),   INT32_C(-82345),   INT32_C(-80118),
    INT32_C(-77856),   INT32_C(-75563),   INT32_C(-73246),   INT32_C(-70910),   INT32_C(-68562),
    INT32_C(-66206),   INT32_C(-63848),   INT32_C(-61493),   INT32_C(-59145),   INT32_C(-56810),
    INT32_C(-54490),   INT32_C(-52191),   INT32_C(-49917),   INT32_C(-47671),   INT32_C(-45456),
    INT32_C(-43276),   INT32_C(-41134),   INT32_C(-39032),   INT32_C(-36973),   INT32_C(-34960),
    INT32_C(-32995),   INT32_C(-31080),   INT32_C(-29216),   INT32_C(-27406),   INT32_C(-25649),
    INT32_C(-23949),   INT32_C(-22306),   INT32_C(-20720),   INT32_C(-19193),   INT32_C(-17725),
    INT32_C(-16316),   INT32_C(-14967),   INT32_C(-13677),   INT32_C(-12447),   INT32_C(-11276),
    INT32_C(-10164),   INT32_C(-9111),    INT32_C(-8115),    INT32_C(-7177),    INT32_C(-6295),
    INT32_C(-5469),    INT32_C(-4697),    INT32_C(-3979),    INT32_C(-3313),    INT32_C(-2698),
    INT32_C(-2133),    INT32_C(-1615),    INT32_C(-1145),    INT32_C(-720),     INT32_C(-339),
    INT32_C(0)};

/* The output sample at PDM index c is sum(h[t] * s[c - FIR_HALF + t]) over the
 * FIR_TAPS taps, with s = +1/-1. Because s is one bit, eight consecutive taps
 * collapse into one table entry indexed by eight PDM bits: 101 lookups per
 * output sample instead of 801 multiply-accumulates. The sum is unchanged, so
 * the result is bit-identical to sonar_pdm_reference_sample(). */
#define LUT_GROUPS    ((FIR_TAPS + 7U) / 8U)
#define CHANNEL_WORDS ((SONAR_PDM_SAMPLES / 32U) + 2U) /* +2: zero padding read by window() */

static int32_t lut[LUT_GROUPS][256];
static bool lut_ready;
static uint32_t channel_bits[CHANNEL_WORDS];

static int32_t tap(uint32_t t)
{
    if (t >= FIR_TAPS) {
        return 0;
    }
    return fir_q31[t > FIR_HALF ? t - FIR_HALF : FIR_HALF - t];
}

static void build_lut(void)
{
    for (uint32_t group = 0U; group < LUT_GROUPS; ++group) {
        for (uint32_t bits = 0U; bits < 256U; ++bits) {
            int32_t sum = 0;
            for (uint32_t k = 0U; k < 8U; ++k) {
                int32_t h = tap(group * 8U + k);
                sum += ((bits >> k) & 1U) != 0U ? h : -h;
            }
            lut[group][bits] = sum;
        }
    }
    lut_ready = true;
}

static uint32_t instant(const uint32_t *words, uint32_t index)
{
    uint32_t word = words[index >> 1U];
    return (index & 1U) != 0U ? word >> 16U : word & UINT32_C(0xffff);
}

/* Eight PDM bits of the current channel starting at bit position p. */
static uint32_t window(uint32_t p)
{
    uint64_t pair = ((uint64_t)channel_bits[(p >> 5U) + 1U] << 32U) | channel_bits[p >> 5U];
    return (uint32_t)(pair >> (p & 31U)) & 0xffU;
}

static int16_t q31_to_i16(int64_t value)
{
    int64_t scaled;

    if (value >= 0) {
        scaled = (value + INT64_C(32768)) / INT64_C(65536);
    } else {
        scaled = -(((-value) + INT64_C(32768)) / INT64_C(65536));
    }

    if (scaled > INT16_MAX) {
        return INT16_MAX;
    }
    if (scaled < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)scaled;
}

int16_t sonar_pdm_reference_sample(const uint32_t *words, uint32_t pdm_samples, uint32_t frame,
                                   unsigned channel)
{
    int64_t center = (int64_t)frame * SONAR_PDM_DECIMATION;
    int64_t acc = 0;

    for (uint32_t t = 0U; t < FIR_TAPS; ++t) {
        int64_t index = center - (int64_t)FIR_HALF + (int64_t)t;
        if (index >= 0 && index < (int64_t)pdm_samples) {
            /* Samples outside the capture are zero padding. */
            int32_t h = tap(t);
            acc += ((instant(words, (uint32_t)index) >> channel) & 1U) != 0U ? h : -h;
        }
    }
    return q31_to_i16(acc);
}

bool sonar_pdm_to_pcm16_interleaved(const uint8_t *pdm, uint32_t pdm_bytes, int16_t *pcm,
                                    uint32_t pcm_sample_capacity, uint32_t *pcm_frames_out)
{
    if (pdm == NULL || pcm == NULL || pcm_frames_out == NULL ||
        (((uintptr_t)pdm) & (sizeof(uint32_t) - 1U)) != 0U || (pdm_bytes & 3U) != 0U) {
        return false;
    }

    const uint32_t pdm_samples = (pdm_bytes / 4U) * 2U;
    const uint32_t pcm_frames = pdm_samples / SONAR_PDM_DECIMATION;

    if (pdm_samples > SONAR_PDM_SAMPLES || (pdm_samples % SONAR_PDM_DECIMATION) != 0U ||
        pcm_sample_capacity < pcm_frames * SONAR_ACQ_CHANNELS) {
        return false;
    }
    if (!lut_ready) {
        build_lut();
    }

    const uint32_t *words = (const uint32_t *)(const void *)pdm;

    for (unsigned channel = 0U; channel < SONAR_ACQ_CHANNELS; ++channel) {
        /* Transpose this channel into a contiguous bit stream. */
        for (uint32_t i = 0U; i < CHANNEL_WORDS; ++i) {
            channel_bits[i] = 0U;
        }
        for (uint32_t i = 0U; i < pdm_samples; ++i) {
            channel_bits[i >> 5U] |= ((instant(words, i) >> channel) & 1U) << (i & 31U);
        }
        for (uint32_t frame = 0U; frame < pcm_frames; ++frame) {
            uint32_t center = frame * SONAR_PDM_DECIMATION;
            int16_t sample;
            if (center < FIR_HALF || center + FIR_HALF >= pdm_samples) {
                sample = sonar_pdm_reference_sample(words, pdm_samples, frame, channel);
            } else {
                uint32_t start = center - FIR_HALF;
                int64_t acc = 0;
                for (uint32_t group = 0U; group < LUT_GROUPS; ++group) {
                    acc += lut[group][window(start + group * 8U)];
                }
                sample = q31_to_i16(acc);
            }
            pcm[frame * SONAR_ACQ_CHANNELS + channel] = sample;
        }
    }

    *pcm_frames_out = pcm_frames;
    return true;
}
